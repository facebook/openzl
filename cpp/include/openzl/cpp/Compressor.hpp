// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <ostream>

#include "openzl/cpp/CParam.hpp"
#include "openzl/cpp/LocalParams.hpp"
#include "openzl/cpp/detail/NonNullUniqueCPtr.hpp"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

namespace openzl {
class CustomEncoder;
class FunctionGraph;
class Selector;

using DataID  = ZL_DataID;
using NodeID  = ZL_NodeID;
using GraphID = ZL_GraphID;

struct StaticGraphParameters {
    poly::optional<std::string> name;
    poly::optional<LocalParams> localParams;
};

struct NodeParameters {
    poly::optional<std::string> name;
    poly::optional<LocalParams> localParams;
};

struct GraphParameters {
    poly::optional<std::string> name;
    poly::optional<std::vector<GraphID>> customGraphs;
    poly::optional<std::vector<NodeID>> customNodes;
    poly::optional<LocalParams> localParams;
};

class Compressor {
   public:
    /**
     * Creates a new Compressor object owned by the @p Compressor object.
     * @throws on allocation failure.
     */
    Compressor();

    Compressor(const Compressor&) = delete;
    Compressor(Compressor&&)      = default;

    Compressor& operator=(const Compressor&) = delete;
    Compressor& operator=(Compressor&&)      = default;

    ~Compressor() = default;

    /// @returns pointer to the underlying ZL_Compressor* object.
    ZL_Compressor* get()
    {
        return compressor_.get();
    }
    /// @returns const pointer to the underlying ZL_Compressor* object.
    const ZL_Compressor* get() const
    {
        return compressor_.get();
    }

    void setParameter(CParam param, int value);
    int getParameter(CParam param) const;

    poly::string_view getErrorContextString(ZL_Error error) const;

    template <typename ResultType>
    poly::string_view getErrorContextString(ResultType result) const
    {
        return getErrorContextString(ZL_RES_error(result));
    }

    template <typename ResultType>
    typename ResultType::ValueType unwrap(
            ResultType result,
            poly::string_view msg = {},
            poly::source_location location =
                    poly::source_location::current()) const
    {
        return openzl::unwrap(
                result, std::move(msg), this, std::move(location));
    }

    GraphID buildStaticGraph(
            NodeID node,
            const std::initializer_list<const GraphID>& successors,
            const poly::optional<StaticGraphParameters>& params = poly::nullopt)
    {
        return buildStaticGraph(
                node, { successors.begin(), successors.end() }, params);
    }
    GraphID buildStaticGraph(
            NodeID node,
            poly::span<const GraphID> successors,
            const poly::optional<StaticGraphParameters>& params =
                    poly::nullopt);

    GraphID registerFunctionGraph(const ZL_FunctionGraphDesc& desc);
    GraphID registerFunctionGraph(std::shared_ptr<FunctionGraph> graph);

    GraphID registerSelectorGraph(const ZL_SelectorDesc& desc);
    GraphID registerSelectorGraph(std::shared_ptr<Selector> selector);

    GraphID parameterizeGraph(GraphID graph, const GraphParameters& params);

    NodeID parameterizeNode(NodeID node, const NodeParameters& desc);

    NodeID registerCustomEncoder(const ZL_MIEncoderDesc& desc);
    NodeID registerCustomEncoder(std::shared_ptr<CustomEncoder> encoder);

    poly::optional<NodeID> getNode(const char* name) const;
    poly::optional<NodeID> getNode(const std::string& name) const
    {
        return getNode(name.c_str());
    }

    poly::optional<GraphID> getGraph(const char* name) const;
    poly::optional<GraphID> getGraph(const std::string& name) const
    {
        return getGraph(name.c_str());
    }

    void selectStartingGraph(GraphID graph);

    GraphID getStartingGraph() const;

    /// @returns a serialized representation of this compressor.
    ///
    /// @note consult zl_compressor_serialization.h for discussion of the
    ///       semantics of this operation.
    std::string serialize() const;

    /// @returns a JSON-serialized representation of this compressor.
    ///
    /// @note consult zl_compressor_serialization.h for discussion of the
    ///       semantics of this operation.
    std::string serializeToJson() const;

    /// Helper function to translate a serialized compressor to a
    /// human-readable representation for debugging.
    static std::string convertSerializedToJson(poly::string_view serialized);

    /// Ingests @p serialized and materializes the compressor it represents
    /// into this compressor.
    ///
    /// @note consult zl_compressor_serialization.h for discussion of the
    ///       semantics of this operation.
    void deserialize(poly::string_view serialized);

    struct UnmetDependencies {
        std::vector<std::string> graphNames;
        std::vector<std::string> nodeNames;
    };

    /// Compares the serialized compressor in @p serialized against the state
    /// of the compressor this object manages, and returns any custom
    /// components required by @p serialized that are not currently present in
    /// the compressor this object manages.
    UnmetDependencies getUnmetDependencies(poly::string_view serialized) const;

   protected:
    Compressor(
            ZL_Compressor* compressor,
            detail::NonNullUniqueCPtr<ZL_Compressor>::DeleterFn deleter)
            : compressor_(compressor, deleter)
    {
    }

   private:
    detail::NonNullUniqueCPtr<ZL_Compressor> compressor_;
};

class CompressorRef : public Compressor {
   public:
    explicit CompressorRef(ZL_Compressor* compressor)

            : Compressor(compressor, nullptr)
    {
    }
};

} // namespace openzl

////////////////////////////////////////
// Operators for Zstrong Opaque Types
////////////////////////////////////////

// in root namespace

constexpr inline bool operator==(
        const openzl::DataID& a,
        const openzl::DataID& b)
{
    return a.sid == b.sid;
}
constexpr inline bool operator==(
        const openzl::NodeID& a,
        const openzl::NodeID& b)
{
    return a.nid == b.nid;
}
constexpr inline bool operator==(
        const openzl::GraphID& a,
        const openzl::GraphID& b)
{
    return a.gid == b.gid;
}

constexpr inline bool operator!=(
        const openzl::DataID& a,
        const openzl::DataID& b)
{
    return !(a == b);
}
constexpr inline bool operator!=(
        const openzl::NodeID& a,
        const openzl::NodeID& b)
{
    return !(a == b);
}
constexpr inline bool operator!=(
        const openzl::GraphID& a,
        const openzl::GraphID& b)
{
    return !(a == b);
}

inline std::ostream& operator<<(std::ostream& os, const openzl::DataID& did)
{
    os << "(ZL_DataID){" << did.sid << "}";
    return os;
}
inline std::ostream& operator<<(std::ostream& os, const openzl::NodeID& nid)
{
    os << "(ZL_NodeID){" << nid.nid << "}";
    return os;
}
inline std::ostream& operator<<(std::ostream& os, const openzl::GraphID& gid)
{
    os << "(ZL_GraphID){" << gid.gid << "}";
    return os;
}
