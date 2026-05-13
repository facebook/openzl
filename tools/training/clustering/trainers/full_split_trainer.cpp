// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/clustering/trainers/full_split_trainer.h"

#include "tools/training/clustering/clustering_config_builder.h"

namespace openzl::training {
ClusteringConfigBuilder FullSplitTrainer::getTrainedClusteringConfig(
        const ZL_Compressor* compressor,
        const std::vector<MultiInput>& samples,
        const std::vector<ZL_GraphID>& successors,
        const std::vector<ZL_NodeID>& clusteringCodecs,
        const std::map<std::pair<ZL_Type, size_t>, size_t>&
                typeToDefaultSuccessorIdxMap)
{
    auto cUtils = CompressionUtils(
            compressor,
            samples,
            successors,
            clusteringCodecs,
            this->threadPool_);
    auto metadata = cUtils.aggregateInputMetadata();
    auto config   = ClusteringConfigBuilder::buildFullSplitConfig(
            metadata,
            typeToDefaultSuccessorIdxMap,
            cUtils.getTypeToClusteringCodecIdxsMap());

    std::vector<std::future<ClusterInfo>> futures;
    futures.reserve(config.clusters().size());
    for (const auto& cluster : config.clusters()) {
        auto tag   = *cluster.memberTags.begin();
        auto type  = cluster.typeSuccessor.type;
        auto width = cluster.typeSuccessor.eltWidth;
        auto task  = [&cUtils, &metadata, tag, type, width]() {
            std::unordered_set<int> splitCluster = { tag };
            auto clusterInfo                     = cUtils.getBestClusterInfo(
                    splitCluster, type, width, metadata);
            return clusterInfo;
        };
        futures.emplace_back(this->threadPool_.run(task));
    }
    size_t clusterIdx = 0;
    for (auto& future : futures) {
        auto clusterInfo = future.get();
        config.setClusterSuccessor(clusterIdx, clusterInfo.successorIdx);
        config.setClusteringCodec(clusterIdx, clusterInfo.clusteringCodecIdx);
        clusterIdx++;
    }
    return config;
}
} // namespace openzl::training
