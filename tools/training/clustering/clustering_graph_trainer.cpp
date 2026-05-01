// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <vector>

#include "openzl/common/allocation.h"
#include "openzl/compress/cgraph.h"
#include "openzl/compress/graphs/generic_clustering_graph.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_reflection.h"

#include "tools/logger/Logger.h"
#include "tools/training/clustering/clustering_graph_trainer.h"
#include "tools/training/clustering/train_api.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/sample_collection/training_sample_collector.h"
#include "tools/training/utils/utils.h"

using namespace openzl::training::graph_mutation;
using namespace openzl::tools::logger;

namespace openzl::training {

const std::string CLUSTERING_GRAPH_NAME = "zl.cluster";

namespace {

/**
 * Add a new parameterized version of the clustering graph to the compressor
 * which has ACE successors instead of the original successors.
 * NOTE: this function does not swap this new clustering graph into the
 * original graph, it just registers it on the compressor.
 */
void setAceSuccessors(Compressor& compressor, GraphID trainedClusteringGraphID)
{
    // Add the same number of ACE successors as there are clustering
    // successors
    const size_t numClusteringSuccessors =
            ZL_Compressor_Graph_getCustomGraphs(
                    compressor.get(), trainedClusteringGraphID)
                    .nbGraphIDs;

    std::vector<ZL_GraphID> aceGraphIds;
    aceGraphIds.reserve(numClusteringSuccessors);
    for (size_t i = 0; i < numClusteringSuccessors; i++) {
        aceGraphIds.push_back(ZL_Compressor_buildACEGraph(compressor.get()));
    }

    ZL_GraphParameters params = {
        .customGraphs   = aceGraphIds.data(),
        .nbCustomGraphs = aceGraphIds.size(),
    };

    compressor.unwrap(ZL_Compressor_overrideGraphParams(
            compressor.get(), trainedClusteringGraphID, &params));
}

/**
 * Add a new parameterized version of the clustering graph to the compressor
 * which has clustered successors.
 * NOTE: this function does not swap this new clustered graph into the
 * original graph, it just registers it on the compressor.
 */
ZL_GraphID clusterSuccessors(
        const std::vector<MultiInput>& inputs,
        Compressor& compressor,
        const TrainParams& trainParams,
        GraphID clusteringGraphUniqueIDUntrained)
{
    auto cctx = refCCtxForTraining(compressor);

    const std::string clusteringGraphUniqueNameUntrained =
            ZL_Compressor_Graph_getName(
                    compressor.get(), clusteringGraphUniqueIDUntrained);

    // Get successors for training
    const auto successorsVec =
            getCustomGraphs(compressor, clusteringGraphUniqueIDUntrained);

    // Get clustering codecs for training
    std::vector<ZL_NodeID> clusteringCodecs =
            getCustomNodes(compressor, clusteringGraphUniqueIDUntrained);

    auto samples = collectInputStreamsForGraph(
            inputs, clusteringGraphUniqueNameUntrained, cctx);
    Logger::log_c(
            VERBOSE1, "Training cluster with %zu samples", samples.size());

    // Train clustering graph
    const auto arena = detail::NonNullUniqueCPtr<Arena>(
            ALLOC_HeapArena_create(), ALLOC_Arena_freeArena);

    ZL_GraphID startingGraphId;
    ZL_Compressor_getStartingGraphID(compressor.get(), &startingGraphId);

    compressor.selectStartingGraph(ZL_GRAPH_CLUSTERING);
    auto trainedClusteringGraphID = train_cluster(
            compressor.get(),
            *arena,
            samples,
            successorsVec,
            clusteringCodecs,
            {}, // TODO: Compute this inside of train_cluster
            trainParams);
    compressor.selectStartingGraph(startingGraphId);

    return trainedClusteringGraphID;
}

/**
 * Get the unique name of the clustering graph in the compressor's
 * graph. There is expected to be exactly one clustering graph.
 */
ZL_GraphID getClusteringGraphUniqueID(const Compressor& compressor)
{
    auto clusteringGraphs =
            findAllGraphsWithPrefix(compressor, CLUSTERING_GRAPH_NAME);
    if (clusteringGraphs.size() != 1) {
        throw Exception(
                "Graph must contain a single clustering graph, instead it contains "
                + std::to_string(clusteringGraphs.size())
                + " clustering graphs");
    }
    auto baseGraph = ZL_Compressor_Graph_getBaseGraphID(
            compressor.get(), clusteringGraphs[0]);
    if (baseGraph != ZL_GRAPH_CLUSTERING) {
        throw Exception(
                "Clustering graph is not a parameterized version of ZL_GRAPH_CLUSTERING");
    }

    return clusteringGraphs[0];
}

void overrideClusteringGraphParams(
        Compressor& compressor,
        GraphID untrainedGraph,
        GraphID trainedGraph)
{
    assert(ZL_Compressor_Graph_getBaseGraphID(compressor.get(), untrainedGraph)
           == ZL_GRAPH_CLUSTERING);
    assert(ZL_Compressor_Graph_getBaseGraphID(compressor.get(), trainedGraph)
           == ZL_GRAPH_CLUSTERING);

    auto graphs = getCustomGraphs(compressor, trainedGraph);
    auto nodes  = getCustomNodes(compressor, trainedGraph);
    auto localParams =
            ZL_Compressor_Graph_getLocalParams(compressor.get(), trainedGraph);

    const ZL_GraphParameters params = {
        .customGraphs   = graphs.data(),
        .nbCustomGraphs = graphs.size(),
        .customNodes    = nodes.data(),
        .nbCustomNodes  = nodes.size(),
        .localParams    = &localParams,
    };
    compressor.unwrap(ZL_Compressor_overrideGraphParams(
            compressor.get(), untrainedGraph, &params));
}

} // namespace

std::shared_ptr<const std::string_view> trainClusteringGraph(
        const std::vector<MultiInput>& inputs,
        Compressor& compressor,
        const TrainParams& trainParams)
{
    // Get name of untrained clustering graph which will be replaced in the
    // final trained graph
    const auto clusteringGraphUniqueIDUntrained =
            getClusteringGraphUniqueID(compressor);

    // Cluster successors (or don't cluster)
    ZL_GraphID trainedClusteringGraphID = trainParams.noClustering
            ? clusteringGraphUniqueIDUntrained
            : clusterSuccessors(
                      inputs,
                      compressor,
                      trainParams,
                      clusteringGraphUniqueIDUntrained);

    if (!trainParams.noAceSuccessors) {
        // Replace default successors with ACE successors by overriding
        // graph parameters
        setAceSuccessors(compressor, trainedClusteringGraphID);
    }

    // Replace original clustering graph with the new one that uses
    // clustering and/or ACE successors by overriding graph parameters
    overrideClusteringGraphParams(
            compressor,
            clusteringGraphUniqueIDUntrained,
            trainedClusteringGraphID);

    return createSharedStringView(compressor.serialize());
}

} // namespace openzl::training
