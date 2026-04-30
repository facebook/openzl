// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <dlfcn.h>

#include <map>
#include <memory>
#include <vector>

namespace openzl {
enum class UseCustomData : bool { Enable = true, Disable = false };

struct CustomData {
    std::string data;
    size_t eltWidth;
};

struct Config {
    unsigned formatVersion;
    unsigned eltWidth;
    bool zeroAllowed;
    UseCustomData customData;
    bool compressionMayFail;

    bool operator<(Config const& o) const
    {
        return std::tie(
                       formatVersion,
                       eltWidth,
                       zeroAllowed,
                       customData,
                       compressionMayFail)
                < std::tie(
                        o.formatVersion,
                        o.eltWidth,
                        o.zeroAllowed,
                        o.customData,
                        o.compressionMayFail);
    }

    bool operator==(Config const& o) const
    {
        return std::tie(
                       formatVersion,
                       eltWidth,
                       zeroAllowed,
                       customData,
                       compressionMayFail)
                == std::tie(
                        o.formatVersion,
                        o.eltWidth,
                        o.zeroAllowed,
                        o.customData,
                        o.compressionMayFail);
    }

    friend std::ostream& operator<<(std::ostream& out, const Config& cfg)
    {
        out << "formatVersion: " << cfg.formatVersion
            << ", eltWidth: " << cfg.eltWidth
            << ", zeroAllowed: " << cfg.zeroAllowed << ", customData: "
            << (cfg.customData == UseCustomData::Enable ? "Enable" : "Disable")
            << ", compressionMayFail: " << cfg.compressionMayFail;
        return out;
    }
};

struct NodeID {
    int id;

    bool operator<(NodeID const o) const
    {
        return id < o.id;
    }
    bool operator==(NodeID const o) const
    {
        return id == o.id;
    }
};

struct GraphID {
    int id;

    bool operator<(GraphID const o) const
    {
        return id < o.id;
    }
    bool operator==(GraphID const o) const
    {
        return id == o.id;
    }
};

struct TransformID {
    int id;

    bool operator<(TransformID const o) const
    {
        return id < o.id;
    }
    bool operator==(TransformID const o) const
    {
        return id == o.id;
    }
};

struct Graph {
    GraphID id;
    Config config;

    bool operator<(Graph const& o) const
    {
        return std::tie(id, config) < std::tie(o.id, o.config);
    }

    bool operator==(Graph const& o) const
    {
        return std::tie(id, config) == std::tie(o.id, o.config);
    }
};

struct Node {
    /// Changes between versions
    NodeID id;
    /// Stable between versions
    TransformID transformID;
    Config config;

    bool operator<(Node const& o) const
    {
        return std::tie(id, transformID, config)
                < std::tie(o.id, transformID, o.config);
    }

    bool operator==(Node const& o) const
    {
        return std::tie(id, transformID, config)
                == std::tie(o.id, transformID, o.config);
    }

    friend std::ostream& operator<<(std::ostream& out, const Node& node)
    {
        out << "id: " << node.id.id << ", transformID: " << node.transformID.id
            << ", config: " << node.config;
        return out;
    }
};

class VersionTestInterface {
   public:
    VersionTestInterface(char const* libVersionTestInterfaceSO);

    unsigned majorVersion() const;
    unsigned minorVersion() const;
    unsigned patchVersion() const;

    unsigned minFormatVersion() const;
    unsigned maxFormatVersion() const;

    std::vector<Node> const& nodes() const
    {
        return nodes_;
    }
    std::vector<Graph> const& graphs() const
    {
        return graphs_;
    }

    /// @returns custom data for the given node, if any exists
    const std::vector<CustomData>& customData(NodeID node);

    /// @returns custom data for the given graph, if any exists
    const std::vector<CustomData>& customData(GraphID node);

    std::string compress(
            std::string_view source,
            unsigned eltWidth,
            unsigned formatVersion,
            NodeID id) const;

    std::string compress(
            std::string_view source,
            unsigned eltWidth,
            unsigned formatVersion,
            GraphID id) const;

    std::string compress(
            std::string_view source,
            unsigned eltWidth,
            unsigned formatVersion,
            std::string_view entropy) const;

    std::string decompress(std::string_view source) const;

   private:
    using GetZStrongVersion = unsigned (*)(int);
    using GetNbIDs          = size_t (*)();
    using GetAllNodeIDs     = void (*)(int*, int*, size_t);
    using GetAllGraphIDs    = void (*)(int*, size_t);
    using CompressBound     = size_t (*)(size_t);
    using CompressWithID    = size_t (*)(
            void*,
            size_t,
            void const*,
            size_t,
            unsigned,
            unsigned,
            int);
    using CompressWithEntropy = size_t (*)(
            void*,
            size_t,
            void const*,
            size_t,
            unsigned,
            unsigned,
            void const*,
            size_t);
    using DecompressedSize = size_t (*)(void const*, size_t);
    using Decompress       = size_t (*)(void*, size_t, void const*, size_t);
    using IsError          = bool (*)(size_t);
    using CustomDataFn     = size_t (*)(char**, size_t**, size_t**, int);

    struct VTable {
        GetZStrongVersion getZStrongVersion;
        GetNbIDs getNbNodeIDs;
        GetAllNodeIDs getAllNodeIDs;
        GetNbIDs getNbGraphIDs;
        GetAllGraphIDs getAllGraphIDs;
        IsError isError;
        CompressBound compressBound;
        CompressWithID compressWithNodeID;
        CompressWithID compressWithGraphID;
        CompressWithEntropy compressWithGraphFromEntropy;
        DecompressedSize decompressedSize;
        Decompress decompress;
        CustomDataFn customNodeData;
        CustomDataFn customGraphData;
    };

    template <typename SymbolT>
    SymbolT loadSymbol(char const* symbolName)
    {
        void* const symbol = dlsym(handle_.get(), symbolName);
        if (symbol == nullptr) {
            throw std::runtime_error(
                    std::string("Failed to load symbol: ") + symbolName);
        }
        return reinterpret_cast<SymbolT>(symbol);
    }

    std::vector<Node> getAllNodes();
    std::vector<Graph> getAllGraphs();

    std::vector<CustomData> getCustomData(CustomDataFn customDataFn, int id)
            const;

    struct DlcloseDeleter {
        void operator()(void* handle) const
        {
            dlclose(handle);
        }
    };
    std::unique_ptr<void, DlcloseDeleter> handle_;
    VTable vtable_;
    std::vector<Node> nodes_;
    std::vector<Graph> graphs_;
    std::map<NodeID, std::vector<CustomData>> nodeCustomDataCache_;
    std::map<GraphID, std::vector<CustomData>> graphCustomDataCache_;
};

} // namespace openzl
