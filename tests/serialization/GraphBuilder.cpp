// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>

#include "openzl/common/errors_internal.h"
#include "openzl/compress/implicit_conversion.h"
#include "openzl/zl_reflection.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/serialization/GraphBuilder.h"

namespace openzl::tests {

namespace {

} // namespace

GraphBuilder::GraphBuilder(
        datagen::DataGen& gen,
        Compressor& compressor,
        GraphBuilderConfig config)
        : gen_(gen), compressor_(compressor), config_(std::move(config))
{
}

void GraphBuilder::addComponent(std::unique_ptr<OpenZLComponent> component)
{
    component->registerComponent(compressor_);
    components_.push_back(std::move(component));
}

void GraphBuilder::addAllComponents()
{
    for (int i = 0; i < static_cast<int>(OpenZLComponentID::NumComponents);
         ++i) {
        auto component = makeOpenZLComponent(static_cast<OpenZLComponentID>(i));
        // Only use components that support serialization
        if (component->supportsSerialization()) {
            component->registerComponent(compressor_);
            components_.push_back(std::move(component));
        }
    }
}

void GraphBuilder::buildCompressor()
{
    nodeIDs_.clear();
    graphIDs_.clear();
    // Generate graphs for each component
    for (auto& component : components_) {
        std::mt19937 urbg(gen_.u32("component_shuffle_seed"));
        auto nodes          = component->predefinedNodes(compressor_);
        auto generatedNodes = component->generateNodes(
                compressor_, gen_, config_.numGeneratedComponents);
        nodes.insert(nodes.end(), generatedNodes.begin(), generatedNodes.end());
        std::shuffle(nodes.begin(), nodes.end(), urbg);
        size_t numNodes = std::min(config_.numComponentsPerId, nodes.size());
        for (size_t i = 0; i < numNodes; i++) {
            nodeIDs_.push_back(nodes[i]);
        }

        auto graphs          = component->predefinedGraphs(compressor_);
        auto generatedGraphs = component->generateGraphs(
                compressor_, gen_, config_.numGeneratedComponents);
        graphs.insert(
                graphs.end(), generatedGraphs.begin(), generatedGraphs.end());
        std::shuffle(graphs.begin(), graphs.end(), urbg);
        size_t numGraphs = std::min(config_.numComponentsPerId, graphs.size());
        for (size_t i = 0; i < numGraphs; i++) {
            graphIDs_.push_back(graphs[i]);
        }
    }

    // Build a random graph recursively
    size_t nodesInGraph = 0;
    auto type           = gen_.choices<ZL_Type>(
            "start type",
            { ZL_Type_serial,
                        ZL_Type_numeric,
                        ZL_Type_struct,
                        ZL_Type_string });
    GraphID graph = buildGraph(&nodesInGraph, type, config_.maxDepth);

    // Select the starting graph
    compressor_.selectStartingGraph(graph);
}

GraphID
GraphBuilder::buildGraph(size_t* nodesInGraph, ZL_Type inType, size_t maxDepth)
{
    // Stop at max nodes or max depth to avoid running out of space
    if (*nodesInGraph > config_.maxNodesInGraph || maxDepth == 0) {
        return buildStoreGraph(inType);
    }

    ++*nodesInGraph;

    // Random chance to stop with store. If out of bytes, coin would yield 0 and
    // store would be chosen.
    if (gen_.coin("use_store", config_.stopProbability)) {
        return buildStoreGraph(inType);
    }

    // Choose between a graph or a node
    bool const useNode = gen_.coin("use_node", config_.nodeProbability);

    if (!useNode) {
        auto graphs = getCompatibleGraphs(inType);
        if (graphs.empty()) {
            throw std::runtime_error(
                    "No compatible graphs found for type "
                    + std::to_string(inType));
        }
        size_t idx = gen_.usize_range("graph_index", 0, graphs.size() - 1);
        return graphs[idx];
    }

    auto nodes = getCompatibleNodes(inType);
    if (nodes.empty()) {
        throw std::runtime_error(
                "No compatible nodes found for type " + std::to_string(inType));
    }

    size_t idx  = gen_.usize_range("node_index", 0, nodes.size() - 1);
    NodeID node = nodes[idx];

    // Build successor graphs recursively
    size_t const nbSuccessors =
            ZL_Compressor_Node_getNumOutcomes(compressor_.get(), node);
    std::vector<ZL_GraphID> successors(nbSuccessors);
    for (size_t i = 0; i < successors.size(); ++i) {
        ZL_Type outType = (ZL_Type)ZL_Compressor_Node_getOutputType(
                compressor_.get(), node, i);
        successors[i] = buildGraph(nodesInGraph, outType, maxDepth - 1);
    }

    return GraphID(ZL_Compressor_registerStaticGraph_fromNode(
            compressor_.get(), node, successors.data(), successors.size()));
}

GraphID GraphBuilder::buildStoreGraph(ZL_Type inType)
{
    if (inType == ZL_Type_string) {
        return GraphID(ZL_Compressor_registerStaticGraph_fromNode(
                compressor_.get(),
                ZL_NODE_SEPARATE_STRING_COMPONENTS,
                ZL_GRAPHLIST(ZL_GRAPH_STORE, ZL_GRAPH_STORE)));
    }
    return GraphID(ZL_GRAPH_STORE);
}

std::vector<NodeID> GraphBuilder::getCompatibleNodes(ZL_Type inType)
{
    std::vector<NodeID> result;

    for (auto node : nodeIDs_) {
        auto const nodeType =
                ZL_Compressor_Node_getInput0Type(compressor_.get(), node);
        if (ICONV_isCompatible(inType, nodeType)) {
            result.push_back(node);
        }
    }

    return result;
}

std::vector<GraphID> GraphBuilder::getCompatibleGraphs(ZL_Type inType)
{
    std::vector<GraphID> result;

    for (auto graph : graphIDs_) {
        auto const graphType =
                ZL_Compressor_Graph_getInput0Mask(compressor_.get(), graph);
        if (ICONV_isCompatible(inType, graphType)) {
            result.push_back(graph);
        }
    }

    return result;
}

} // namespace openzl::tests
