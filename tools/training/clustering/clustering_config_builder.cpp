// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/clustering/clustering_config_builder.h"

namespace openzl::training {

ClusteringConfigBuilder ClusteringConfigBuilder::buildConfigClusterSoloSplit(
        const ColumnMetadata& metadata,
        const CompressionUtils& cUtils,
        ColumnInfo tag) const
{
    ClusteringConfigBuilder soloSplitCandidate(*this);
    for (auto& cluster : soloSplitCandidate.clusters_) {
        if (cluster.typeSuccessor.type != tag.type
            || cluster.typeSuccessor.eltWidth != tag.width) {
            continue;
        }
        if (cluster.memberTags.find(tag.tag) != cluster.memberTags.end()) {
            cluster.memberTags.erase(tag.tag);
            break;
        }
    }
    Cluster newCluster;
    newCluster.typeSuccessor.type     = tag.type;
    newCluster.typeSuccessor.eltWidth = tag.width;
    newCluster.memberTags.insert(tag.tag);
    // Pick a successor for the cluster
    auto clusterInfo = cUtils.getBestClusterInfo(
            { tag.tag }, tag.type, tag.width, metadata);
    newCluster.typeSuccessor.successorIdx = clusterInfo.successorIdx;
    newCluster.typeSuccessor.clusteringCodecIdx =
            clusterInfo.clusteringCodecIdx;
    soloSplitCandidate.clusters_.emplace_back(newCluster);
    return ClusteringConfigBuilder(soloSplitCandidate);
}

ClusteringConfigBuilder ClusteringConfigBuilder::buildConfigClusterPairSplit(
        const ColumnMetadata& metadata,
        const CompressionUtils& cUtils,
        ColumnInfo tag1,
        ColumnInfo tag2) const
{
    ZL_Type type1    = tag1.type;
    size_t eltWidth1 = tag1.width;
    ZL_Type type2    = tag2.type;
    size_t eltWidth2 = tag2.width;
    ClusteringConfigBuilder pairSplitCandidate(*this);
    for (auto& cluster : pairSplitCandidate.clusters_) {
        if (cluster.typeSuccessor.type == tag1.type
            && cluster.typeSuccessor.eltWidth == tag1.width
            && cluster.memberTags.find(tag1.tag) != cluster.memberTags.end()) {
            cluster.memberTags.erase(tag1.tag);
        }
        if (cluster.typeSuccessor.type == tag2.type
            && cluster.typeSuccessor.eltWidth == tag2.width
            && cluster.memberTags.find(tag2.tag) != cluster.memberTags.end()) {
            cluster.memberTags.erase(tag2.tag);
        }
    }
    if (type1 != type2 || eltWidth1 != eltWidth2) {
        throw Exception("Incompatible types");
    }
    // Note: successor selection must be done subsequently
    Cluster newCluster;
    newCluster.typeSuccessor.type     = type1;
    newCluster.typeSuccessor.eltWidth = eltWidth1;
    newCluster.memberTags.insert(tag1.tag);
    newCluster.memberTags.insert(tag2.tag);
    auto clusterInfo = cUtils.getBestClusterInfo(
            { tag1.tag, tag2.tag }, type1, eltWidth1, metadata);
    newCluster.typeSuccessor.successorIdx = clusterInfo.successorIdx;
    newCluster.typeSuccessor.clusteringCodecIdx =
            clusterInfo.clusteringCodecIdx;
    pairSplitCandidate.clusters_.emplace_back(newCluster);
    return pairSplitCandidate;
}

ClusteringConfigBuilder ClusteringConfigBuilder::buildConfigAddInputToCluster(
        int tag,
        ZL_Type type,
        size_t eltWidth,
        int clusterIdx) const
{
    ClusteringConfigBuilder config(*this);
    for (auto& cluster : config.clusters_) {
        if (cluster.memberTags.find(tag) != cluster.memberTags.end()) {
            if (type != cluster.typeSuccessor.type
                || eltWidth != cluster.typeSuccessor.eltWidth) {
                continue;
            }
            cluster.memberTags.erase(tag);
            break;
        }
    }
    auto& cluster = config.clusters_[clusterIdx];
    if (type != cluster.typeSuccessor.type
        || eltWidth != cluster.typeSuccessor.eltWidth) {
        throw Exception("Incompatible types");
    }
    cluster.memberTags.insert(tag);
    return config;
}

// Returns false if incompatible type, or already in this cluster
bool ClusteringConfigBuilder::typeIsCompatibleWithClusterIdx(
        ZL_Type type,
        size_t eltWidth,
        int clusterIdx) const
{
    auto& cluster = clusters_[clusterIdx];
    if (type != cluster.typeSuccessor.type
        || eltWidth != cluster.typeSuccessor.eltWidth) {
        return false;
    }
    return true;
}

ClusteringConfigBuilder ClusteringConfigBuilder::buildFullSplitConfig(
        const ColumnMetadata& metadata,
        const std::map<std::pair<ZL_Type, size_t>, size_t>&
                typeToDefaultSuccessorIdxMap,
        const std::map<ZL_Type, std::vector<size_t>>&
                typeToClusteringCodecIdxsMap_)
{
    ClusteringConfigBuilder config;
    for (auto& [typeWidth, defaultSuccessorIdx] :
         typeToDefaultSuccessorIdxMap) {
        ZL_ClusteringConfig_TypeSuccessor typeSuccessor;
        typeSuccessor.type         = typeWidth.first;
        typeSuccessor.eltWidth     = typeWidth.second;
        typeSuccessor.successorIdx = defaultSuccessorIdx;
        typeSuccessor.clusteringCodecIdx =
                typeToClusteringCodecIdxsMap_.at(typeWidth.first)
                        .front(); // Just use the first type of clustering by
                                  // default
        config.typeDefaults_.emplace_back(typeSuccessor);
    }
    for (auto& info : metadata) {
        auto tag      = info.tag;
        auto typeInfo = std::make_pair(info.type, info.width);
        Cluster cluster;
        cluster.typeSuccessor.type     = typeInfo.first;
        cluster.typeSuccessor.eltWidth = typeInfo.second;

        auto it = typeToDefaultSuccessorIdxMap.find(typeInfo);
        cluster.typeSuccessor.successorIdx =
                (it != typeToDefaultSuccessorIdxMap.end()) ? it->second : 0;
        cluster.typeSuccessor.clusteringCodecIdx =
                typeToClusteringCodecIdxsMap_.at(cluster.typeSuccessor.type)
                        .front();
        cluster.memberTags.insert(tag);
        config.clusters_.emplace_back(cluster);
    }
    return config;
}

ClusteringConfigBuilder ClusteringConfigBuilder::buildStoreConfig()
{
    ClusteringConfigBuilder config;
    ZL_ClusteringConfig_TypeSuccessor serialDefault = {
        .type               = ZL_Type_serial,
        .eltWidth           = 1,
        .successorIdx       = 0,
        .clusteringCodecIdx = 0,
    };
    ZL_ClusteringConfig_TypeSuccessor numericDefault = {
        .type               = ZL_Type_numeric,
        .eltWidth           = 8,
        .successorIdx       = 0,
        .clusteringCodecIdx = 2,
    };
    ZL_ClusteringConfig_TypeSuccessor stringDefault = {
        .type               = ZL_Type_string,
        .eltWidth           = 0,
        .successorIdx       = 0,
        .clusteringCodecIdx = 3,
    };
    config.typeDefaults_ = { serialDefault, numericDefault, stringDefault };
    return config;
}

ClusteringConfigBuilder
ClusteringConfigBuilder::buildConfigSingleClusterWithSuccessor(
        const std::unordered_set<int>& tags,
        ZL_Type type,
        size_t eltWidth,
        size_t successorIdx,
        size_t clusteringCodecIdx)
{
    ClusteringConfigBuilder config;
    ZL_ClusteringConfig_TypeSuccessor typeSuccessor;
    typeSuccessor.type               = type;
    typeSuccessor.eltWidth           = eltWidth;
    typeSuccessor.successorIdx       = successorIdx;
    typeSuccessor.clusteringCodecIdx = clusteringCodecIdx;
    Cluster cluster;
    cluster = { .typeSuccessor = typeSuccessor, .memberTags = tags };
    config.clusters_.emplace_back(cluster);
    return config;
}

ClusteringConfigBuilder ClusteringConfigBuilder::buildStartingConfig(
        const ColumnMetadata& metadata,
        const CompressionUtils& cUtils,
        const std::map<std::pair<ZL_Type, size_t>, size_t>&
                typeToDefaultSuccessorIdxMap,
        const std::map<ZL_Type, std::vector<size_t>>&
                typeToClusteringCodecIdxsMap_)
{
    ClusteringConfigBuilder config;
    for (auto& [typeWidth, defaultSuccessorIdx] :
         typeToDefaultSuccessorIdxMap) {
        ZL_ClusteringConfig_TypeSuccessor typeSuccessor;
        typeSuccessor.type         = typeWidth.first;
        typeSuccessor.eltWidth     = typeWidth.second;
        typeSuccessor.successorIdx = defaultSuccessorIdx;
        typeSuccessor.clusteringCodecIdx =
                typeToClusteringCodecIdxsMap_.at(typeWidth.first)
                        .front(); // Just use the first type of clustering by
                                  // default;
        config.typeDefaults_.emplace_back(typeSuccessor);
    }
    // Set up a type split configuration
    std::map<std::pair<ZL_Type, size_t>, std::unordered_set<int>>
            typeToInputsMap;
    for (auto& info : metadata) {
        auto tag       = info.tag;
        auto typeWidth = std::make_pair(info.type, info.width);
        typeToInputsMap[typeWidth].insert(tag);
    }
    for (auto& [typeWidth, tags] : typeToInputsMap) {
        Cluster cluster;
        cluster.typeSuccessor.type     = typeWidth.first;
        cluster.typeSuccessor.eltWidth = typeWidth.second;
        auto clusterInfo               = cUtils.getBestClusterInfo(
                { tags }, typeWidth.first, typeWidth.second, metadata);
        cluster.typeSuccessor.successorIdx = clusterInfo.successorIdx;
        cluster.typeSuccessor.clusteringCodecIdx =
                clusterInfo.clusteringCodecIdx;
        cluster.memberTags = std::move(tags);
        config.clusters_.emplace_back(cluster);
    }
    return config;
}

void ClusteringConfigBuilder::makeSuccessorIndicesUnique(
        std::vector<ZL_GraphID>& successors)
{
    std::vector<ZL_GraphID> newSuccessors;
    size_t currSuccessorIdx = 0;
    // Rewrite the successor indices of the cluster to be one successor per
    // cluster
    newSuccessors.reserve(clusters_.size());
    for (auto& cluster : clusters_) {
        if (cluster.memberTags.size() == 0) {
            continue;
        }
        auto successorIdx                  = cluster.typeSuccessor.successorIdx;
        cluster.typeSuccessor.successorIdx = currSuccessorIdx++;
        newSuccessors.push_back(successors[successorIdx]);
    }
    // Overwrite the current set of successors with ones with what the new
    // indices map to
    std::swap(successors, newSuccessors);
}

ClusteringConfig ClusteringConfigBuilder::build() const
{
    ClusteringConfig config;
    for (auto& cluster : clusters_) {
        ZL_ClusteringConfig_Cluster cCluster;
        cCluster.typeSuccessor = cluster.typeSuccessor;
        std::vector<int> memberTags(
                cluster.memberTags.begin(), cluster.memberTags.end());
        cCluster.memberTags   = memberTags.data();
        cCluster.nbMemberTags = memberTags.size();
        if (cluster.memberTags.size() != 0) {
            config.push_back(cCluster);
        }
    }
    for (auto& typeSuccessor : typeDefaults_) {
        config.push_back(typeSuccessor);
    }
    return config;
}

} // namespace openzl::training
