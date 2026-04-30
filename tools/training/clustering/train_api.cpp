// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/clustering/train_api.h"
#include "tools/logger/Logger.h"
#include "tools/training/clustering/compression_utils.h"
#include "tools/training/clustering/trainers/bottom_up_trainer.h"
#include "tools/training/clustering/trainers/full_split_trainer.h"
#include "tools/training/clustering/trainers/greedy_trainer.h"
#include "tools/training/clustering/utils.h"

#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"

namespace openzl::training {

using namespace openzl::tools::logger;

ZL_GraphID train_cluster(
        ZL_Compressor* compressor,
        Arena& arena,
        const std::vector<MultiInput>& samples,
        const std::vector<ZL_GraphID>& successors,
        const std::vector<ZL_NodeID>& clusteringCodecs,
        const std::map<std::pair<ZL_Type, size_t>, size_t>&
                typeToDefaultSuccessorIdxMap,
        const TrainParams& trainParams)
{
    // Check the starting graph is ZL_GRAPH_CLUSTERING
    ZL_GraphID startingGraph;
    if (!ZL_Compressor_getStartingGraphID(compressor, &startingGraph)) {
        throw Exception("Error getting starting graph ID");
    }
    auto baseGraph =
            ZL_Compressor_Graph_getBaseGraphID(compressor, startingGraph);
    // The starting graph must either be ZL_GRAPH_CLUSTERING or a parametrized
    // version of it.
    if (baseGraph.gid != ZL_GRAPH_CLUSTERING.gid
        && startingGraph.gid != ZL_GRAPH_CLUSTERING.gid) {
        throw Exception(
                "Starting graph for train_cluster's base graph must be ZL_GRAPH_CLUSTERING");
    }

    std::unique_ptr<Trainer> trainer;
    auto maxThreads =
            trainParams.threads.value_or(std::thread::hardware_concurrency());
    if (!trainParams.clusteringTrainer.has_value()) {
        Logger::log(
                INFO,
                "Selected greedy trainer by default since no trainer was specified");
        trainer = std::make_unique<GreedyTrainer>(
                maxThreads, trainParams.maxTimeSecs);
    } else {
        switch (trainParams.clusteringTrainer.value()) {
            case ClusteringTrainer::Greedy: {
                Logger::log(INFO, "Selected greedy trainer");
                trainer = std::make_unique<GreedyTrainer>(
                        maxThreads, trainParams.maxTimeSecs);
            } break;
            case ClusteringTrainer::FullSplit: {
                Logger::log(INFO, "Selected full-split trainer");
                trainer = std::make_unique<FullSplitTrainer>(
                        maxThreads, trainParams.maxTimeSecs);
            } break;
            case ClusteringTrainer::BottomUp: {
                Logger::log(INFO, "Selected bottom-up trainer");
                trainer = std::make_unique<BottomUpTrainer>(
                        maxThreads, trainParams.maxTimeSecs);
            } break;
        }
    }

    // Train the clustering config on the scratch compressor and the
    auto configBuilder = trainer->getTrainedClusteringConfig(
            compressor,
            samples,
            successors,
            clusteringCodecs,
            typeToDefaultSuccessorIdxMap);
    Logger::log(VERBOSE1, "Best config details: ");
    Utils::printClusteringConfig(configBuilder.build());
    // Make successors unique so they are ace compatible
    auto uniqueSuccessors = successors;
    configBuilder.makeSuccessorIndicesUnique(uniqueSuccessors);
    auto config = configBuilder.build();

    // Register the same config on the original compressor with new IDs
    return ZL_Clustering_registerGraphWithCustomClusteringCodecs(
            compressor,
            config.get(),
            uniqueSuccessors.data(),
            uniqueSuccessors.size(),
            clusteringCodecs.data(),
            clusteringCodecs.size());
}
} // namespace openzl::training
