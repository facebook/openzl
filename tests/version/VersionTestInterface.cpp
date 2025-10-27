// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "VersionTestInterface.h"
#include "VersionTestInterfaceABI.h"

#include <dlfcn.h>

#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <gflags/gflags.h>

namespace zstrong {

VersionTestInterface::VersionTestInterface(
        char const* libVersionTestInterfaceSO)
{
    // Shut down command line flags before dlopening the version test
    // interface. This is because the version test interface can register
    // gflags.
    // WARNING: This is super super hacky, and gflags says accessing flags after
    // this UB. But, the fuzzers are running with this change, so it seems to be
    // working. A cleaner fix would be to remove the gflags dependency in the
    // version test interface shared library that we are dlopening. But, this
    // makes it harder to develop for the version test library, because we would
    // have to pre-generate some data, instead of generating it inline. So as
    // long as this is working, we will just do this.
    gflags::ShutDownCommandLineFlags();
    int const flags = RTLD_LAZY | RTLD_LOCAL;
    handle_.reset(dlopen(libVersionTestInterfaceSO, flags));
    if (handle_ == nullptr) {
        throw std::runtime_error(
                std::string("Failed to link shared library: ") + dlerror());
    }
    vtable_.getZStrongVersion = loadSymbol<GetZStrongVersion>(
            "VersionTestInterface_getZStrongVersion");

    vtable_.getNbNodeIDs =
            loadSymbol<GetNbIDs>("VersionTestInterface_getNbNodeIDs");
    vtable_.getAllNodeIDs =
            loadSymbol<GetAllNodeIDs>("VersionTestInterface_getAllNodeIDs");

    vtable_.getNbGraphIDs =
            loadSymbol<GetNbIDs>("VersionTestInterface_getNbGraphIDs");
    vtable_.getAllGraphIDs =
            loadSymbol<GetAllGraphIDs>("VersionTestInterface_getAllGraphIDs");

    vtable_.isError = loadSymbol<IsError>("VersionTestInterface_isError");

    vtable_.compressBound =
            loadSymbol<CompressBound>("VersionTestInterface_compressBound");
    vtable_.compressWithNodeID = loadSymbol<CompressWithID>(
            "VersionTestInterface_compressWithNodeID");
    vtable_.compressWithGraphID = loadSymbol<CompressWithID>(
            "VersionTestInterface_compressWithGraphID");
    vtable_.compressWithGraphFromEntropy = loadSymbol<CompressWithEntropy>(
            "VersionTestInterface_compressWithGraphFromEntropy");

    vtable_.decompressedSize = loadSymbol<DecompressedSize>(
            "VersionTestInterface_decompressedSize");
    vtable_.decompress =
            loadSymbol<Decompress>("VersionTestInterface_decompress");
    vtable_.customNodeData =
            loadSymbol<CustomDataFn>("VersionTestInterface_customNodeData");
    vtable_.customGraphData =
            loadSymbol<CustomDataFn>("VersionTestInterface_customGraphData");

    nodeCustomDataCache_  = {};
    graphCustomDataCache_ = {};
    nodes_                = getAllNodes();
    graphs_               = getAllGraphs();
}

unsigned VersionTestInterface::majorVersion() const
{
    return vtable_.getZStrongVersion(
            static_cast<int>(detail::VersionType::MAJOR));
}

unsigned VersionTestInterface::minorVersion() const
{
    return vtable_.getZStrongVersion(
            static_cast<int>(detail::VersionType::MINOR));
}

unsigned VersionTestInterface::patchVersion() const
{
    return vtable_.getZStrongVersion(
            static_cast<int>(detail::VersionType::PATCH));
}

unsigned VersionTestInterface::minFormatVersion() const
{
    return vtable_.getZStrongVersion(
            static_cast<int>(detail::VersionType::MIN_FORMAT));
}

unsigned VersionTestInterface::maxFormatVersion() const
{
    return vtable_.getZStrongVersion(
            static_cast<int>(detail::VersionType::MAX_FORMAT));
}

template <typename IDType>
bool compressionSucceeds(
        VersionTestInterface const& vti,
        IDType id,
        Config config,
        std::vector<CustomData> const& customData)
{
    char c = config.zeroAllowed ? 0 : 1;
    try {
        if (customData.empty()) {
            std::string src(config.eltWidth, c);
            vti.compress(src, config.eltWidth, config.formatVersion, id);
            std::string src100(config.eltWidth * 100, c);
            vti.compress(src100, config.eltWidth, config.formatVersion, id);
            std::string src100FF(config.eltWidth * 100, char(0xFF));
            vti.compress(src100FF, config.eltWidth, config.formatVersion, id);
            std::string srcIota(config.eltWidth * 2, '\0');
            for (size_t i = 0; i < srcIota.size(); ++i) {
                srcIota[i] = (char)i;
            }
            vti.compress(srcIota, config.eltWidth, config.formatVersion, id);
            return true;
        } else {
            bool atLeastOne = false;
            for (auto const& data : customData) {
                if (data.eltWidth == config.eltWidth) {
                    vti.compress(
                            data.data,
                            config.eltWidth,
                            config.formatVersion,
                            id);
                    atLeastOne = true;
                }
            }
            return atLeastOne;
        }
    } catch (...) {
        return false;
    }
}

template <typename IDType>
std::vector<Config> getValidConfigs(
        VersionTestInterface& vti,
        IDType id,
        unsigned minVersion,
        unsigned maxVersion)
{
    std::array<unsigned, 5> const eltWidths = { 1, 2, 4, 8, 1000 };

    std::vector<Config> configs;
    for (unsigned version = minVersion; version <= maxVersion; ++version) {
        bool hasAnyConfigs = false;
        for (auto const eltWidth : eltWidths) {
            for (auto const zeroAllowed : { true, false }) {
                Config config{ version,
                               eltWidth,
                               zeroAllowed,
                               UseCustomData::Disable,
                               false };
                if (compressionSucceeds(vti, id, config, {})) {
                    hasAnyConfigs = true;
                    configs.push_back(config);
                    break;
                }
            }
        }
        auto customData = vti.customData(id);
        std::unordered_set<unsigned> customEltWidths;
        for (auto const& data : customData) {
            customEltWidths.insert(data.eltWidth);
        }
        for (auto const eltWidth : customEltWidths) {
            Config config{
                version, eltWidth, false, UseCustomData::Enable, false
            };
            if (compressionSucceeds(vti, id, config, customData)) {
                hasAnyConfigs = true;
                configs.push_back(config);
            }
        }
        if (!hasAnyConfigs) {
            for (auto const eltWidth : eltWidths) {
                configs.push_back(
                        Config{ version,
                                eltWidth,
                                true,
                                UseCustomData::Disable,
                                true });
            }
            if (!customData.empty()) {
                for (auto const eltWidth : customEltWidths) {
                    configs.push_back(
                            Config{ version,
                                    eltWidth,
                                    true,
                                    UseCustomData::Enable,
                                    true });
                }
            }
        }
    }
    return configs;
}

std::vector<Node> VersionTestInterface::getAllNodes()
{
    std::vector<int> nodeIDs(vtable_.getNbNodeIDs());
    std::vector<int> transformIDs(nodeIDs.size());
    vtable_.getAllNodeIDs(nodeIDs.data(), transformIDs.data(), nodeIDs.size());
    std::vector<Node> nodes;
    nodes.reserve(nodeIDs.size());
    for (size_t i = 0; i < nodeIDs.size(); ++i) {
        NodeID nodeID{ nodeIDs[i] };
        TransformID transformID{ transformIDs[i] };
        auto configs = getValidConfigs(
                *this, nodeID, minFormatVersion(), maxFormatVersion());
        for (auto const& config : configs) {
            nodes.push_back({ nodeID, transformID, config });
        }
    }
    return nodes;
}

std::vector<Graph> VersionTestInterface::getAllGraphs()
{
    std::vector<int> graphIDs(vtable_.getNbGraphIDs());
    vtable_.getAllGraphIDs(graphIDs.data(), graphIDs.size());
    std::vector<Graph> graphs;
    graphs.reserve(graphIDs.size());
    for (auto const id : graphIDs) {
        GraphID graphID{ id };
        auto configs = getValidConfigs(
                *this, graphID, minFormatVersion(), maxFormatVersion());
        for (auto const& config : configs) {
            graphs.push_back({ graphID, config });
        }
    }
    return graphs;
}

std::vector<CustomData> VersionTestInterface::getCustomData(
        CustomDataFn customDataFn,
        int id) const
{
    std::unique_ptr<char[]> buffer;
    std::unique_ptr<size_t[]> eltWidths;
    std::unique_ptr<size_t[]> sizes;
    size_t numData;
    {
        char* cBuffer      = nullptr;
        size_t* cEltWidths = nullptr;
        size_t* cSizes     = nullptr;
        numData            = customDataFn(&cBuffer, &cEltWidths, &cSizes, id);
        buffer.reset(cBuffer);
        eltWidths.reset(cEltWidths);
        sizes.reset(cSizes);
    }
    std::vector<CustomData> data;
    data.reserve(numData);
    for (size_t i = 0, offset = 0; i < numData; ++i) {
        std::string datum(buffer.get() + offset, sizes[i]);
        offset += datum.size();
        data.push_back(CustomData{ std::move(datum), eltWidths[i] });
    }
    return data;
}

const std::vector<CustomData>& VersionTestInterface::customData(NodeID node)
{
    if (nodeCustomDataCache_.find(node) != nodeCustomDataCache_.end()) {
        return nodeCustomDataCache_.at(node);
    }
    return nodeCustomDataCache_[node] =
                   getCustomData(vtable_.customNodeData, node.id);
}

const std::vector<CustomData>& VersionTestInterface::customData(GraphID graph)
{
    if (graphCustomDataCache_.find(graph) != graphCustomDataCache_.end()) {
        return graphCustomDataCache_.at(graph);
    }
    return graphCustomDataCache_[graph] =
                   getCustomData(vtable_.customGraphData, graph.id);
}

std::string VersionTestInterface::compress(
        std::string_view source,
        unsigned eltWidth,
        unsigned formatVersion,
        NodeID id) const
{
    std::string out;
    out.resize(vtable_.compressBound(source.size()));
    size_t const ret = vtable_.compressWithNodeID(
            out.data(),
            out.size(),
            source.data(),
            source.size(),
            eltWidth,
            formatVersion,
            id.id);
    if (vtable_.isError(ret)) {
        throw std::runtime_error("Compression failed!");
    }
    out.resize(ret);
    return out;
}

std::string VersionTestInterface::compress(
        std::string_view source,
        unsigned eltWidth,
        unsigned formatVersion,
        GraphID id) const
{
    std::string out;
    out.resize(vtable_.compressBound(source.size()));
    size_t const ret = vtable_.compressWithGraphID(
            out.data(),
            out.size(),
            source.data(),
            source.size(),
            eltWidth,
            formatVersion,
            id.id);
    if (vtable_.isError(ret)) {
        throw std::runtime_error("Compression failed!");
    }
    out.resize(ret);
    return out;
}

std::string VersionTestInterface::compress(
        std::string_view source,
        unsigned eltWidth,
        unsigned formatVersion,
        std::string_view entropy) const
{
    std::string out;
    out.resize(1000 + 2 * vtable_.compressBound(source.size()));
    size_t const ret = vtable_.compressWithGraphFromEntropy(
            out.data(),
            out.size(),
            source.data(),
            source.size(),
            eltWidth,
            formatVersion,
            entropy.data(),
            entropy.size());
    if (vtable_.isError(ret)) {
        throw std::runtime_error("Compression failed!");
    }
    out.resize(ret);
    return out;
}

std::string VersionTestInterface::decompress(std::string_view source) const
{
    std::string out;
    size_t ret = vtable_.decompressedSize(source.data(), source.size());
    if (vtable_.isError(ret)) {
        throw std::runtime_error("Failed to get decompressed size");
    }
    out.resize(ret);

    ret = vtable_.decompress(
            out.data(), out.size(), source.data(), source.size());
    if (vtable_.isError(ret)) {
        throw std::runtime_error("Decompression failed!");
    }
    out.resize(ret);
    return out;
}
} // namespace zstrong
