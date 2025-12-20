// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/clustering/trainers/greedy_trainer.h"

#include <algorithm>
#include <chrono>

#include "tools/logger/Logger.h"

#include "tools/training/clustering/clustering_config_builder.h"

namespace openzl::training {
using namespace openzl::tools::logger;

ClusteringConfigBuilder GreedyTrainer::getTrainedClusteringConfig(
        const ZL_Compressor* compressor,
        const std::vector<MultiInput>& samples,
        const std::vector<ZL_GraphID>& successors,
        const std::vector<ZL_NodeID>& clusteringCodecs,
        const std::map<std::pair<ZL_Type, size_t>, size_t>&
                typeToDefaultSuccessorIdxMap)
{
    reset();
    auto start  = std::chrono::high_resolution_clock::now();
    auto cUtils = CompressionUtils(
            compressor,
            samples,
            successors,
            clusteringCodecs,
            this->threadPool_);
    auto metadata = cUtils.aggregateInputMetadata();
    initTopInputs(cUtils, metadata);
    initSimilarInputs(cUtils, typeToDefaultSuccessorIdxMap, metadata);
    auto bestConfig = ClusteringConfigBuilder::buildStartingConfig(
            metadata,
            cUtils,
            typeToDefaultSuccessorIdxMap,
            cUtils.getTypeToClusteringCodecIdxsMap());
    auto bestCost = cUtils.tryCompress(bestConfig.build()).get();
    for (size_t iteration = 0; iteration < numGreedyIters_; iteration++) {
        bool foundImprovementInIteration = false;
        Logger::log_c(
                VERBOSE1,
                "Starting iteration %zu at cost %zu",
                iteration,
                bestCost.compressedSize);
        for (size_t columnItr = 0; columnItr < topColumns_.size();
             ++columnItr) {
            const auto column = topColumns_[columnItr];
            Logger::logProgress(
                    INFO,
                    static_cast<double>(columnItr + 1) / topColumns_.size(),
                    "Calculating improvement by clustering tag %zu/%zu",
                    columnItr + 1,
                    topColumns_.size());
            auto now      = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                    now - start);
            if (maxTimeSecs_.has_value()
                && (size_t)duration.count() > maxTimeSecs_.value()) {
                Logger::log_c(
                        INFO,
                        "Stopping training early after %zu s. Exceeded max time of %zu s.",
                        duration.count(),
                        maxTimeSecs_.value());
                return bestConfig;
            }
            bool hasImprovement = false;
            // Solo splits
            std::vector<ClusteringConfigBuilder> candidates;
            auto soloSplitCandidate = bestConfig.buildConfigClusterSoloSplit(
                    metadata, cUtils, column);
            // Add candidate to list of the candidates of the iteration
            candidates.emplace_back(soloSplitCandidate);
            // Pair splits
            for (auto similarTag : similarColumns_[column]) {
                auto pairSplitCandidate =
                        bestConfig.buildConfigClusterPairSplit(
                                metadata, cUtils, column, similarTag);
                // Add candidate to list of the candidates of the iteration
                candidates.emplace_back(pairSplitCandidate);
            }
            // Merge input into a different pre-existing cluster
            for (size_t clusterIdx = 0;
                 clusterIdx < bestConfig.clusters().size();
                 clusterIdx++) {
                if (bestConfig.typeIsCompatibleWithClusterIdx(
                            column.type, column.width, clusterIdx)) {
                    candidates.emplace_back(
                            bestConfig.buildConfigAddInputToCluster(
                                    column.tag,
                                    column.type,
                                    column.width,
                                    clusterIdx));
                }
            }
            Logger::log_c(VERBOSE1, "Trying %zu candidates", candidates.size());
            std::vector<SizeTimePair> costs;
            std::vector<std::future<SizeTimePair>> futures;
            futures.reserve(candidates.size());
            for (size_t i = 0; i < candidates.size(); i++) {
                futures.emplace_back(cUtils.tryCompress(candidates[i].build()));
            }
            costs.reserve(futures.size());
            for (auto& future : futures) {
                costs.emplace_back(future.get());
            }

            size_t candidateIdx = 0;
            for (auto& candidate : candidates) {
                auto cost = costs[candidateIdx++];
                if (cost < bestCost) {
                    bestCost       = cost;
                    bestConfig     = std::move(candidate);
                    hasImprovement = true;
                }
            }
            // Can add info on what type of move is done if needed
            if (hasImprovement) {
                Logger::log_c(
                        VERBOSE1, "New cost: %zu", bestCost.compressedSize);
                foundImprovementInIteration = true;
            } else {
                Logger::log_c(
                        VERBOSE1,
                        "No improvement found using tag: %d",
                        column.tag);
            }
        }
        if (!foundImprovementInIteration) {
            break;
        }
    }
    Logger::finalizeProgress(INFO);

    Logger::log_c(
            VERBOSE1,
            "Final config found with cost: %d",
            bestCost.compressedSize);
    return bestConfig;
}

void GreedyTrainer::initTopInputs(
        const CompressionUtils& cUtils,
        ColumnMetadata& metadata)
{
    // doesn't necessarily have to be store config
    ClusteringConfig storeConfig =
            ClusteringConfigBuilder::buildStoreConfig().build();
    std::unordered_map<ColumnInfo, SizeTimePair, ColumnInfoHasher> columnToCost;
    std::vector<ColumnInfo> columns;
    std::vector<SizeTimePair> costs;
    std::vector<std::future<SizeTimePair>> futures;
    futures.reserve(metadata.size());
    columns.reserve(metadata.size());
    for (auto& column : metadata) {
        auto filter = [column](ColumnInfo val) { return val == column; };
        futures.emplace_back(cUtils.tryCompress(storeConfig, filter));
        columns.emplace_back(column);
    }
    costs.reserve(futures.size());
    for (auto& future : futures) {
        costs.emplace_back(future.get());
    }
    for (size_t i = 0; i < columns.size(); i++) {
        columnToCost[columns[i]] = costs[i];
    }
    std::stable_sort(columns.begin(), columns.end(), [&](auto& lhs, auto& rhs) {
        return columnToCost[rhs] < columnToCost[lhs];
    });
    size_t resultSize = std::min(maxInputs_, columns.size());
    topColumns_       = std::vector<ColumnInfo>(
            columns.begin(), columns.begin() + resultSize);
}

// Individual costs are not needed if we only use a single heuristic.
// Marginal cost is defined as the cost to compress with the input,
// subtracted by the cost without the input.
void GreedyTrainer::initInputMarginalCosts(
        const CompressionUtils& cUtils,
        const std::map<std::pair<ZL_Type, size_t>, size_t>&
                typeToDefaultSuccessorIdxMap,
        ColumnMetadata& metadata)
{
    ClusteringConfig startingConfig =
            ClusteringConfigBuilder::buildStartingConfig(
                    metadata,
                    cUtils,
                    typeToDefaultSuccessorIdxMap,
                    cUtils.getTypeToClusteringCodecIdxsMap())
                    .build();
    auto startingCsize =
            cUtils.tryCompress(startingConfig).get().compressedSize;
    std::vector<std::future<SizeTimePair>> futures;
    for (auto tag : topColumns_) {
        auto excludeTag = [tag](ColumnInfo val) {
            if (val == tag) {
                return false;
            }
            return true;
        };
        // TODO: double check this maths is correct
        futures.emplace_back(cUtils.tryCompress(startingConfig, excludeTag));
    }
    for (size_t i = 0; i < futures.size(); i++) {
        auto column            = topColumns_[i];
        auto csize             = futures[i].get().compressedSize;
        marginalCosts_[column] = std::max<int>(1, startingCsize - csize);
    }
}
void GreedyTrainer::initSimilarInputs(
        const CompressionUtils& cUtils,
        const std::map<std::pair<ZL_Type, size_t>, size_t>&
                typeToDefaultSuccessorIdxMap,
        ColumnMetadata& metadata)
{
    initInputMarginalCosts(cUtils, typeToDefaultSuccessorIdxMap, metadata);
    // For all pairs of inputs that have different tags and same type,
    // compute the split costs (cost of compressing a few specific tags)
    for (auto& column1 : topColumns_) {
        std::vector<std::pair<double, ColumnInfo>> similarityScoreColumns;
        std::vector<std::future<std::pair<double, ColumnInfo>>> futures;
        futures.reserve(topColumns_.size());
        for (size_t i = 0; i < topColumns_.size(); i++) {
            auto& column2 = topColumns_[i];
            if (column1 == column2 || column1.type != column2.type
                || column1.width != column2.width) {
                continue;
            }
            auto task = [this, &cUtils, &metadata, column1, column2]() {
                std::unordered_set<int> pairCluster = { column1.tag,
                                                        column2.tag };
                auto splitCost                      = cUtils.getBestClusterInfo(
                                               pairCluster,
                                               column1.type,
                                               column1.width,
                                               metadata)
                                         .cost.compressedSize;
                auto contextualSimilarity = (double)splitCost
                        / (marginalCosts_[column1] + marginalCosts_[column2]);
                return std::make_pair(contextualSimilarity, column2);
            };
            futures.emplace_back(threadPool_->run(task));
        }
        similarityScoreColumns.reserve(futures.size());
        for (auto& future : futures) {
            similarityScoreColumns.emplace_back(future.get());
        }
        std::stable_sort(
                similarityScoreColumns.begin(), similarityScoreColumns.end());
        for (size_t i = 0; i
             < std::min(maxPairSplitCandidates_, similarityScoreColumns.size());
             i++) {
            if (similarityScoreColumns[i].first < 1.0) {
                similarColumns_[column1].emplace_back(
                        similarityScoreColumns[i].second);
            }
        }
    }
}
} // namespace openzl::training
