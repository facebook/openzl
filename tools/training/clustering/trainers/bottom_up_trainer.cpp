// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <chrono>

#include "tools/training/clustering/trainers/bottom_up_trainer.h"
#include "tools/logger/Logger.h"
#include "tools/training/clustering/clustering_config_builder.h"

namespace openzl::training {

using namespace openzl::tools::logger;

ClusteringConfigBuilder BottomUpTrainer::buildTrainedFullSplitConfig(
        const CompressionUtils& cUtils,
        const ColumnMetadata& metadata,
        const std::map<std::pair<ZL_Type, size_t>, size_t>&
                typeToDefaultSuccessorIdxMap)
{
    auto config = ClusteringConfigBuilder::buildFullSplitConfig(
            metadata,
            typeToDefaultSuccessorIdxMap,
            cUtils.getTypeToClusteringCodecIdxsMap());

    // Pick successor and clustering codec for each cluster in full split
    std::vector<std::future<ClusterInfo>> futures;
    futures.reserve(config.clusters().size());
    const auto& clusters = config.clusters();
    for (const auto& cluster : clusters) {
        const auto& tags = cluster.memberTags;
        auto type        = cluster.typeSuccessor.type;
        auto width       = cluster.typeSuccessor.eltWidth;
        auto task        = [&cUtils, &metadata, tags, type, width]() {
            auto clusterInfo =
                    cUtils.getBestClusterInfo(tags, type, width, metadata);
            return clusterInfo;
        };
        futures.emplace_back(this->threadPool_->run(task));
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

ClusteringConfigBuilder BottomUpTrainer::buildTrainedConfigAddInputToCluster(
        const CompressionUtils& cUtils,
        const ColumnMetadata& metadata,
        const ClusteringConfigBuilder& config,
        int tag,
        ZL_Type type,
        size_t eltWidth,
        size_t clusterIdx)
{
    auto candidate = config.buildConfigAddInputToCluster(
            tag, type, eltWidth, clusterIdx);
    auto clusterInfo = cUtils.getBestClusterInfo(
            candidate.clusters()[clusterIdx].memberTags,
            type,
            eltWidth,
            metadata);
    candidate.setClusterSuccessor(clusterIdx, clusterInfo.successorIdx);
    candidate.setClusteringCodec(clusterIdx, clusterInfo.clusteringCodecIdx);
    return candidate;
}

ClusteringConfigBuilder BottomUpTrainer::getTrainedClusteringConfig(
        const ZL_Compressor* compressor,
        const std::vector<MultiInput>& samples,
        const std::vector<ZL_GraphID>& successors,
        const std::vector<ZL_NodeID>& clusteringCodecs,
        const std::map<std::pair<ZL_Type, size_t>, size_t>&
                typeToDefaultSuccessorIdxMap)
{
    auto start  = std::chrono::high_resolution_clock::now();
    auto cUtils = CompressionUtils(
            compressor,
            samples,
            successors,
            clusteringCodecs,
            this->threadPool_);
    auto metadata = cUtils.aggregateInputMetadata();
    auto config   = buildTrainedFullSplitConfig(
            cUtils, metadata, typeToDefaultSuccessorIdxMap);
    Logger::log_c(
            INFO,
            "Created trained full split config with %zu inputs",
            metadata.size());

    // Build clusters up iteratively from full split state
    size_t nbClusters = 0;
    auto bestCost     = cUtils.tryCompress(config.build()).get();
    for (const auto& data : metadata) {
        if (nbClusters == 0) {
            // No cluster has been built yet for the first tag. Track it in the
            // set of clusters
            nbClusters++;
            continue;
        }
        auto now = std::chrono::high_resolution_clock::now();
        auto duration =
                std::chrono::duration_cast<std::chrono::seconds>(now - start);
        if (maxTimeSecs_.has_value()
            && (size_t)duration.count() > maxTimeSecs_.value()) {
            Logger::log_c(
                    INFO,
                    "Stopping training early after %zu s. Exceeded max time of %zu s.",
                    duration.count(),
                    maxTimeSecs_.value());
            return config;
        }
        auto tag            = data.tag;
        auto typeInfo       = std::make_pair(data.type, data.width);
        bool hasImprovement = false;
        // Try add tag to existing clusters only if it hasn't been visited
        std::vector<std::future<ClusteringConfigBuilder>> candidateFutures;
        candidateFutures.reserve(nbClusters);
        for (size_t i = 0; i < nbClusters; i++) {
            if (!config.typeIsCompatibleWithClusterIdx(
                        data.type, data.width, (int)i)) {
                continue;
            }
            auto task =
                    [this, &cUtils, &metadata, &config, tag, typeInfo, i]() {
                        return buildTrainedConfigAddInputToCluster(
                                cUtils,
                                metadata,
                                config,
                                tag,
                                typeInfo.first,
                                typeInfo.second,
                                i);
                    };
            candidateFutures.emplace_back(this->threadPool_->run(task));
        }
        std::vector<std::future<SizeTimePair>> costFutures;
        std::vector<ClusteringConfigBuilder> candidates;
        costFutures.reserve(candidateFutures.size());
        candidates.reserve(candidateFutures.size());
        for (auto& candidateFuture : candidateFutures) {
            auto res = candidateFuture.get();
            costFutures.emplace_back(cUtils.tryCompress(res.build()));
            candidates.emplace_back(std::move(res));
        }
        for (size_t i = 0; i < candidates.size(); i++) {
            assert(i < costFutures.size());
            auto cost = costFutures[i].get();
            if (cost < bestCost) {
                bestCost       = cost;
                config         = std::move(candidates[i]);
                hasImprovement = true;
            }
        }

        if (hasImprovement) {
            // A candidate has been found that adds to a tracked cluster.
            // Therefore we do not increment the number of clusters.
            Logger::log_c(VERBOSE1, "New cost: %zu", bestCost.compressedSize);
        } else {
            // Increment the number of clusters to track the new tag as its own
            // cluster
            nbClusters++;
            Logger::log_c(VERBOSE1, "No improvement found using tag: %zu", tag);
        }
    }
    Logger::log_c(
            VERBOSE1,
            "Final config found with cost: ",
            bestCost.compressedSize);
    return config;
}
} // namespace openzl::training
