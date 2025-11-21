// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include <map>
#include "tools/training/clustering/clustering_config.h"
#include "tools/training/clustering/compression_utils.h"

namespace openzl::training {

// TODO (T231577342): Add documentation the builder methods
// TODO (T232129813): Let them config builder return an optional, and move the
// validity check of the move inside the building of the config
class ClusteringConfigBuilder {
   public:
    struct Cluster {
        ZL_ClusteringConfig_TypeSuccessor typeSuccessor;
        std::unordered_set<int> memberTags;
    };
    // Methods to build associated configs
    ClusteringConfigBuilder buildConfigClusterSoloSplit(
            const ColumnMetadata& metadata,
            const CompressionUtils& cUtils,
            ColumnInfo tag) const;
    ClusteringConfigBuilder buildConfigClusterPairSplit(
            const ColumnMetadata& metadata,
            const CompressionUtils& cUtils,
            ColumnInfo tag1,
            ColumnInfo tag2) const;
    /* Builds a config where the input with @p tag is moved out of its existing
     * cluster and added to the cluster with index @p clusterIdx */
    ClusteringConfigBuilder buildConfigAddInputToCluster(
            int tag,
            ZL_Type type,
            size_t eltWidth,
            int clusterIdx) const;
    // Returns false if incompatible type, or already in this cluster
    bool typeIsCompatibleWithClusterIdx(
            ZL_Type type,
            size_t eltWidth,
            int clusterIdx) const;
    // Methods to build a starting config
    static ClusteringConfigBuilder buildConfigSingleClusterWithSuccessor(
            const std::unordered_set<int>& tags,
            ZL_Type type,
            size_t eltWidth,
            size_t successorIdx,
            size_t clusteringCodecIdx);
    static ClusteringConfigBuilder buildFullSplitConfig(
            const ColumnMetadata& metadata,
            const std::map<std::pair<ZL_Type, size_t>, size_t>&
                    typeToDefaultSuccessorIdxMap,
            const std::map<ZL_Type, std::vector<size_t>>&
                    typeToClusteringCodecIdxsMap_);
    static ClusteringConfigBuilder buildStoreConfig();
    static ClusteringConfigBuilder buildStartingConfig(
            const ColumnMetadata& metadata,
            const CompressionUtils& cUtils,
            const std::map<std::pair<ZL_Type, size_t>, size_t>&
                    typeToDefaultSuccessorIdxMap,
            const std::map<ZL_Type, std::vector<size_t>>&
                    typeToClusteringCodecIdxsMap_);

    // Replaces the current config such that every cluster uses a unique
    // successor. If the successor is not unique, then a copy of the successor
    // is made and the successorIdx of the cluster will point to the copy.
    void makeSuccessorIndicesUnique(std::vector<ZL_GraphID>& successors);

    // Getters
    ClusteringConfig build() const;
    std::vector<Cluster> clusters() const
    {
        return clusters_;
    }
    // Mutators on ClusteringConfigBuilder
    void setClusterSuccessor(size_t clusterIdx, size_t successorIdx)
    {
        clusters_[clusterIdx].typeSuccessor.successorIdx = successorIdx;
    }

    void setClusteringCodec(size_t clusterIdx, size_t clusteringCodecIdx)
    {
        clusters_[clusterIdx].typeSuccessor.clusteringCodecIdx =
                clusteringCodecIdx;
    }

   private:
    std::vector<Cluster> clusters_;
    std::vector<ZL_ClusteringConfig_TypeSuccessor> typeDefaults_;
};

} // namespace openzl::training
