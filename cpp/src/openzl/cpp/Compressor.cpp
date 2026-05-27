// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/Compressor.hpp"

#include "openzl/zl_compressor.h"
#include "openzl/zl_compressor_serialization.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_reflection.h"

#include "openzl/cpp/CustomEncoder.hpp"
#include "openzl/cpp/FunctionGraph.hpp"
#include "openzl/cpp/Selector.hpp"
#include "openzl/cpp/detail/NonNullUniqueCPtr.hpp"

namespace openzl {

namespace {
using CompressorSerializerPtr =
        detail::NonNullUniqueCPtr<ZL_CompressorSerializer>;

using CompressorDeserializerPtr =
        detail::NonNullUniqueCPtr<ZL_CompressorDeserializer>;

CompressorSerializerPtr make_serializer()
{
    CompressorSerializerPtr serializer{ ZL_CompressorSerializer_create(),
                                        ZL_CompressorSerializer_free };
    return serializer;
}

CompressorDeserializerPtr make_deserializer()
{
    CompressorDeserializerPtr deserializer{ ZL_CompressorDeserializer_create(),
                                            ZL_CompressorDeserializer_free };
    return deserializer;
}
} // anonymous namespace

Compressor::Compressor()
        : Compressor(ZL_Compressor_create(), ZL_Compressor_free)
{
}

void Compressor::setParameter(CParam param, int value)
{
    unwrap(ZL_Compressor_setParameter(
            get(), static_cast<ZL_CParam>(param), value));
}

int Compressor::getParameter(CParam param) const
{
    return ZL_Compressor_getParameter(get(), static_cast<ZL_CParam>(param));
}

poly::string_view Compressor::getErrorContextString(ZL_Error error) const
{
    return ZL_Compressor_getErrorContextString_fromError(get(), error);
}

NodeID Compressor::registerCustomEncoder(const ZL_MIEncoderDesc& desc)
{
    return unwrap(ZL_Compressor_registerMIEncoder2(get(), &desc));
}

GraphID Compressor::registerFunctionGraph(const ZL_FunctionGraphDesc& desc)
{
    return unwrap(ZL_Compressor_registerFunctionGraph2(get(), &desc));
}

GraphID Compressor::registerSelectorGraph(const ZL_SelectorDesc& desc)
{
    return unwrap(ZL_Compressor_registerSelectorGraph2(get(), &desc));
}

GraphID Compressor::parameterizeGraph(
        GraphID graph,
        const GraphParameters& params)
{
    ZL_GraphParameters cParams = {
        .name        = params.name.has_value() ? params.name->c_str() : nullptr,
        .localParams = params.localParams.has_value()
                ? params.localParams->get()
                : nullptr,
    };
    if (params.customGraphs.has_value()) {
        cParams.customGraphs   = params.customGraphs->data();
        cParams.nbCustomGraphs = params.customGraphs->size();
    }
    if (params.customNodes.has_value()) {
        cParams.customNodes   = params.customNodes->data();
        cParams.nbCustomNodes = params.customNodes->size();
    }
    return unwrap(ZL_Compressor_parameterizeGraph(get(), graph, &cParams));
}

GraphID Compressor::registerFunctionGraph(std::shared_ptr<FunctionGraph> graph)
{
    return FunctionGraph::registerFunctionGraph(*this, std::move(graph));
}

GraphID Compressor::registerSelectorGraph(std::shared_ptr<Selector> selector)
{
    return Selector::registerSelector(*this, std::move(selector));
}

NodeID Compressor::parameterizeNode(NodeID node, const NodeParameters& params)
{
    ZL_NodeParameters cParams = {
        .name        = params.name.has_value() ? params.name->c_str() : nullptr,
        .localParams = params.localParams.has_value()
                ? params.localParams->get()
                : nullptr,
    };
    return unwrap(ZL_Compressor_parameterizeNode(get(), node, &cParams));
}

GraphID Compressor::buildStaticGraph(
        NodeID headNode,
        poly::span<const GraphID> successorGraphs,
        const poly::optional<StaticGraphParameters>& params)
{
    ZL_StaticGraphParameters cParams = {};
    if (params.has_value()) {
        if (params->name.has_value()) {
            cParams.name = params->name->c_str();
        }
        if (params->localParams.has_value()) {
            cParams.localParams = params->localParams->get();
        }
    }
    return unwrap(ZL_Compressor_buildStaticGraph(
            get(),
            headNode,
            successorGraphs.data(),
            successorGraphs.size(),
            params.has_value() ? &cParams : nullptr));
}

NodeID Compressor::registerCustomEncoder(std::shared_ptr<CustomEncoder> encoder)
{
    return CustomEncoder::registerCustomEncoder(*this, std::move(encoder));
}

poly::optional<NodeID> Compressor::getNode(const char* name) const
{
    auto node = ZL_Compressor_getNode(get(), name);
    if (node.nid == ZL_NODE_ILLEGAL.nid) {
        return poly::nullopt;
    } else {
        return node;
    }
}
poly::optional<GraphID> Compressor::getGraph(const char* name) const
{
    auto graph = ZL_Compressor_getGraph(get(), name);
    if (graph.gid == ZL_GRAPH_ILLEGAL.gid) {
        return poly::nullopt;
    } else {
        return graph;
    }
}

void Compressor::selectStartingGraph(GraphID graph)
{
    unwrap(ZL_Compressor_selectStartingGraphID(get(), graph));
}

GraphID Compressor::getStartingGraph() const
{
    GraphID gid;
    ZL_Compressor_getStartingGraphID(get(), &gid);
    return gid;
}

std::string Compressor::serialize() const
{
    const auto serializer = make_serializer();
    void* dst             = nullptr;
    size_t dstSize        = 0;
    openzl::unwrap(
            ZL_CompressorSerializer_serialize(
                    serializer.get(), get(), &dst, &dstSize),
            "Call to ZL_CompressorSerializer_serialize() failed.",
            serializer.get());
    std::string serialized{ static_cast<const char*>(dst), dstSize };
    return serialized;
}

std::string Compressor::serializeToJson() const
{
    const auto serializer = make_serializer();
    void* dst             = nullptr;
    size_t dstSize        = 0;
    openzl::unwrap(
            ZL_CompressorSerializer_serializeToJson(
                    serializer.get(), get(), &dst, &dstSize),
            "Call to ZL_CompressorSerializer_serializeToJson() failed.",
            serializer.get());
    std::string serialized{ static_cast<const char*>(dst), dstSize };
    return serialized;
}

/* static */ std::string Compressor::convertSerializedToJson(
        poly::string_view cbor)
{
    const auto serializer = make_serializer();
    void* dst             = nullptr;
    size_t dstSize        = 0;
    openzl::unwrap(
            ZL_CompressorSerializer_convertToJson(
                    serializer.get(), &dst, &dstSize, cbor.data(), cbor.size()),
            "Call to ZL_CompressorSerializer_convertToJson() failed.",
            serializer.get());
    std::string json{ static_cast<const char*>(dst), dstSize };
    return json;
}

void Compressor::deserialize(
        poly::string_view serialized,
        poly::string_view fatBundle)
{
    const auto deserializer = make_deserializer();

    openzl::unwrap(
            ZL_CompressorDeserializer_deserialize(
                    deserializer.get(),
                    get(),
                    serialized.data(),
                    serialized.size(),
                    fatBundle.empty() ? nullptr : fatBundle.data(),
                    fatBundle.size()),
            "Call to ZL_CompressorDeserializer_deserialize() failed.",
            deserializer.get());
}

Compressor::UnmetDependencies Compressor::getUnmetDependencies(
        poly::string_view serialized) const
{
    const auto deserializer = make_deserializer();

    const auto raw_deps = openzl::unwrap(
            ZL_CompressorDeserializer_getDependencies(
                    deserializer.get(),
                    get(),
                    serialized.data(),
                    serialized.size()),
            "Call to ZL_CompressorDeserializer_deserialize() failed.",
            deserializer.get());

    UnmetDependencies deps;
    for (size_t i = 0; i < raw_deps.num_graphs; i++) {
        deps.graphNames.emplace_back(raw_deps.graph_names[i]);
    }
    for (size_t i = 0; i < raw_deps.num_nodes; i++) {
        deps.nodeNames.emplace_back(raw_deps.node_names[i]);
    }
    if (ZL_UniqueID_isValid(&raw_deps.bundle_id.id)) {
        deps.bundleID = raw_deps.bundle_id;
    }
    return deps;
}
} // namespace openzl
