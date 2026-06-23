// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/FunctionGraph.hpp"

#include <type_traits>

#include "openzl/cpp/Opaque.hpp"

namespace openzl {
namespace {
void fillRuntimeGraphParameters(
        ZL_RuntimeGraphParameters& zlParams,
        const poly::optional<GraphParameters>& params)
{
    if (params.has_value()) {
        zlParams.name =
                params->name.has_value() ? params->name->c_str() : nullptr;
        if (params->customGraphs.has_value()) {
            zlParams.customGraphs   = params->customGraphs->data();
            zlParams.nbCustomGraphs = params->customGraphs->size();
        }
        if (params->customNodes.has_value()) {
            zlParams.customNodes   = params->customNodes->data();
            zlParams.nbCustomNodes = params->customNodes->size();
        }
        zlParams.localParams = params->localParams.has_value()
                ? params->localParams->get()
                : nullptr;
    }
}
} // namespace

/* static */ std::vector<Edge> Edge::convert(poly::span<ZL_Edge*> edges)
{
    std::vector<Edge> result;
    result.reserve(edges.size());
    for (auto edge : edges) {
        result.emplace_back(edge);
    }
    return result;
}

/* static */ std::vector<ZL_Edge*> Edge::convert(poly::span<Edge> edges)
{
    std::vector<ZL_Edge*> result;
    result.reserve(edges.size());
    for (auto& edge : edges) {
        result.emplace_back(edge.get());
    }
    return result;
}

Edge::Edge(ZL_Edge* edge)
        : edge_(edge), input_(const_cast<ZL_Input*>(ZL_Edge_getData(edge)))
{
}

std::vector<Edge> Edge::runNode(
        NodeID node,
        const poly::optional<NodeParameters>& params)
{
    return Edge::runMultiInputNode({ this, 1 }, node, params);
}

/* static */ std::vector<Edge> Edge::runMultiInputNode(
        poly::span<Edge> inputs,
        NodeID node,
        const poly::optional<NodeParameters>& params)
{
    // TODO(terrelln): Pass params.name to OpenZL once it supports it
    const ZL_LocalParams* localParams = nullptr;
    if (params.has_value() && params->localParams.has_value()) {
        localParams = params->localParams->get();
    }
    auto edges = Edge::convert(inputs);
    auto out   = unwrap(ZL_Edge_runMultiInputNode_withParams(
            edges.data(), edges.size(), node, localParams));
    return Edge::convert(out);
}

void Edge::setIntMetadata(int key, int value)
{
    unwrap(ZL_Edge_setIntMetadata(get(), key, value));
}

void Edge::setDestination(
        GraphID graph,
        const poly::optional<GraphParameters>& params)
{
    Edge::setMultiInputDestination({ this, 1 }, graph, params);
}

/* static */ void Edge::setMultiInputDestination(
        poly::span<Edge> inputs,
        GraphID graph,
        const poly::optional<GraphParameters>& params)
{
    auto edges = Edge::convert(inputs);
    ZL_RuntimeGraphParameters zlParams{};
    fillRuntimeGraphParameters(zlParams, params);
    unwrap(ZL_Edge_setParameterizedDestination(
            edges.data(),
            edges.size(),
            graph,
            params.has_value() ? &zlParams : nullptr));
}

poly::span<const GraphID> GraphState::customGraphs() const
{
    auto graphs = ZL_Graph_getCustomGraphs(graph_);
    return { graphs.graphids, graphs.nbGraphIDs };
}

poly::span<const NodeID> GraphState::customNodes() const
{
    auto nodes = ZL_Graph_getCustomNodes(graph_);
    return { nodes.nodeids, nodes.nbNodeIDs };
}

int GraphState::getCParam(CParam param) const
{
    return ZL_Graph_getCParam(graph_, static_cast<ZL_CParam>(param));
}

poly::optional<int> GraphState::getLocalIntParam(int key) const
{
    auto param = ZL_Graph_getLocalIntParam(graph_, key);
    if (param.paramId == ZL_LP_INVALID_PARAMID) {
        return poly::nullopt;
    }
    return param.paramValue;
}

poly::optional<poly::span<const uint8_t>> GraphState::getLocalParam(
        int key) const
{
    auto param = ZL_Graph_getLocalRefParam(graph_, key);
    if (param.paramId == ZL_LP_INVALID_PARAMID) {
        return poly::nullopt;
    }
    return poly::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(param.paramRef), param.paramSize
    };
}

void* GraphState::getScratchSpace(size_t size)
{
    return ZL_Graph_getScratchSpace(graph_, size);
}

bool GraphState::isNodeSupported(NodeID node) const
{
    return ZL_Graph_isNodeSupported(graph_, node);
}

poly::optional<GraphPerformance> GraphState::tryGraph(
        const Input& input,
        GraphID graph,
        const poly::optional<GraphParameters>& params) const
{
    auto inputPtr = input.get();
    return tryGraph({ &inputPtr, 1 }, graph, params);
}

/* static */ poly::optional<GraphPerformance> GraphState::tryGraph(
        poly::span<const ZL_Input*> inputs,
        GraphID graph,
        const poly::optional<GraphParameters>& params) const
{
    ZL_RuntimeGraphParameters zlParams{};
    fillRuntimeGraphParameters(zlParams, params);
    auto report = ZL_Graph_tryMultiInputGraph(
            get(),
            inputs.data(),
            inputs.size(),
            graph,
            params.has_value() ? &zlParams : nullptr);
    if (ZL_RES_isError(report)) {
        return poly::nullopt;
    } else {
        return ZL_RES_value(report);
    }
}

static ZL_Report
graphFn(ZL_Graph* graph, ZL_Edge* edges[], size_t nbEdges) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    try {
        GraphState state(graph, { edges, nbEdges });
        const FunctionGraph* functionGraph =
                (const FunctionGraph*)ZL_Graph_getOpaquePtr(graph);
        functionGraph->graph(state);
    } catch (const Exception& e) {
        return ZL_WRAP_ERROR_NO_FRAME(e.toError(graph));
    } catch (const std::exception& e) {
        ZL_ERR(GENERIC, "C++ std::exception: %s", e.what());
    } catch (...) {
        ZL_ERR(GENERIC, "C++ unknown exception");
    }
    return ZL_returnSuccess();
}

/* static */ GraphID FunctionGraph::registerFunctionGraph(
        Compressor& compressor,
        std::shared_ptr<const FunctionGraph> functionGraph)
{
    const auto& desc               = functionGraph->functionGraphDescription();
    auto inputTypeMasks            = typesMasksToCTypes(desc.inputTypeMasks);
    ZL_FunctionGraphDesc graphDesc = {
        .name           = desc.name.has_value() ? desc.name->c_str() : nullptr,
        .graph_f        = graphFn,
        .inputTypeMasks = inputTypeMasks.data(),
        .nbInputs       = inputTypeMasks.size(),
        .lastInputIsVariable = desc.lastInputIsVariable,
        .customGraphs        = desc.customGraphs.data(),
        .nbCustomGraphs      = desc.customGraphs.size(),
        .customNodes         = desc.customNodes.data(),
        .nbCustomNodes       = desc.customNodes.size(),
        .opaque              = moveToOpaquePtr(std::move(functionGraph)),
    };
    if (desc.localParams.has_value()) {
        graphDesc.localParams = **desc.localParams;
    }
    return compressor.registerFunctionGraph(graphDesc);
}

poly::string_view GraphState::getErrorContextString(ZL_Error error) const
{
    return ZL_Graph_getErrorContextString_fromError(get(), error);
}

} // namespace openzl
