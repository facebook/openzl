// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <algorithm>
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/Input.hpp"
#include "tools/training/clustering/clustering_config.h"
#include "tools/training/clustering/clustering_config_builder.h"
#include "tools/training/utils/utils.h"

namespace openzl::tests {
namespace {

class TestClusteringConfigBuilder : public testing::Test {
   public:
    void SetUp() override
    {
        // Register the base clustering graph as the starting graph
        training::Utils::throwIfError(
                ZL_Compressor_selectStartingGraphID(
                        compressor_.get(), ZL_GRAPH_CLUSTERING),
                "Failed to select starting graph during compression");
        successors_.push_back(ZL_GRAPH_STORE);
        successors_.push_back(ZL_GRAPH_FIELD_LZ);
        successors_.push_back(ZL_GRAPH_ZSTD);
        successors_.push_back(ZL_GRAPH_COMPRESS_GENERIC);
        clusteringCodecs_.push_back(ZL_NODE_CONCAT_SERIAL);
        clusteringCodecs_.push_back(ZL_NODE_CONCAT_STRUCT);
        clusteringCodecs_.push_back(ZL_NODE_CONCAT_NUMERIC);
        clusteringCodecs_.push_back(ZL_NODE_CONCAT_STRING);
        clusteringCodecs_.push_back(ZL_NODE_INTERLEAVE_STRING);
        typeToClusteringCodecIdxsMap_[ZL_Type_serial]  = { 0 };
        typeToClusteringCodecIdxsMap_[ZL_Type_struct]  = { 1 };
        typeToClusteringCodecIdxsMap_[ZL_Type_numeric] = { 2 };
        typeToClusteringCodecIdxsMap_[ZL_Type_string]  = { 3, 4 };
        // Set up compressor utils
        auto samples    = setUpCompressSamples();
        auto threadPool = std::make_shared<training::ThreadPool>(1);
        cUtils_         = std::make_unique<training::CompressionUtils>(
                compressor_.get(),
                samples,
                successors_,
                clusteringCodecs_,
                threadPool);
        // Set up tags
        for (size_t i = 0; i < 20; ++i) {
            if (i < 5) {
                columnMetadata_.insert({ (int)i, ZL_Type_numeric, 1 });
            } else if (i < 10) {
                columnMetadata_.insert({ (int)i, ZL_Type_numeric, 8 });
            } else if (i < 15) {
                columnMetadata_.insert({ (int)i, ZL_Type_serial, 1 });
            } else {
                columnMetadata_.insert({ (int)i, ZL_Type_string, 0 });
            }
        }
    }

    /* Checks that the clustering config has a cluster with the given tag and
     * the typeSuccessor matches. Also checks that there should not be multiple
     * clusters with the same tag.
     *
     * Note: This function requires iterating through all clusters and all the
     * tags of each cluster.
     */
    void checkClusteringConfigTagHasTypeSuccessor(
            const ZL_ClusteringConfig* config,
            const ZL_ClusteringConfig_TypeSuccessor& typeSuccessor,
            int tag) const
    {
        bool hasTag = false;
        for (size_t i = 0; i < config->nbClusters; ++i) {
            for (size_t j = 0; j < config->clusters[i].nbMemberTags; ++j) {
                if (config->clusters[i].memberTags[j] == tag) {
                    EXPECT_FALSE(hasTag);
                    hasTag = true;
                    checkTypeSuccessorEquals(
                            config->clusters[i].typeSuccessor, typeSuccessor);
                }
            }
        }
        EXPECT_TRUE(hasTag);
    }

    void TearDown() override
    {
        successors_.clear();
        clusteringCodecs_.clear();
        columnMetadata_.clear();
        typeToDefaultSuccessorIdxMap_.clear();
        typeToClusteringCodecIdxsMap_.clear();
    }

   protected:
    std::vector<ZL_GraphID> successors_;
    std::vector<ZL_NodeID> clusteringCodecs_;
    training::ColumnMetadata columnMetadata_;
    std::map<std::pair<ZL_Type, size_t>, size_t> typeToDefaultSuccessorIdxMap_;
    std::map<ZL_Type, std::vector<size_t>> typeToClusteringCodecIdxsMap_;
    openzl::Compressor compressor_;
    std::unique_ptr<training::CompressionUtils> cUtils_;

   private:
    std::vector<training::MultiInput> setUpCompressSamples()
    {
        auto input = Input::refNumeric<uint64_t>(numVec_);
        input.setIntMetadata(0, 0);
        auto sample = training::MultiInput();
        sample.add(std::move(input));
        return std::vector<training::MultiInput>{ sample };
    }

    void checkTypeSuccessorEquals(
            const ZL_ClusteringConfig_TypeSuccessor& lhs,
            const ZL_ClusteringConfig_TypeSuccessor& rhs) const
    {
        EXPECT_EQ(lhs.clusteringCodecIdx, rhs.clusteringCodecIdx);
        EXPECT_EQ(lhs.eltWidth, rhs.eltWidth);
        EXPECT_EQ(lhs.successorIdx, rhs.successorIdx);
        EXPECT_EQ(lhs.type, rhs.type);
    }
    // A small sample to compress
    const std::vector<uint64_t> numVec_ = { 0, 1, 1, 0, 2 };
};

TEST_F(TestClusteringConfigBuilder, TestBuildFullSplitConfig)
{
    typeToDefaultSuccessorIdxMap_[{ ZL_Type_numeric, 1 }] = 1;
    typeToDefaultSuccessorIdxMap_[{ ZL_Type_string, 0 }]  = 3;
    auto configBuilder =
            training::ClusteringConfigBuilder::buildFullSplitConfig(
                    columnMetadata_,
                    typeToDefaultSuccessorIdxMap_,
                    typeToClusteringCodecIdxsMap_);
    auto config = configBuilder.build();
    EXPECT_EQ(config->nbTypeDefaults, 2);
    EXPECT_EQ(config->nbClusters, 20);
    for (size_t i = 0; i < 20; i++) {
        if (i < 5) {
            ZL_ClusteringConfig_TypeSuccessor typeSuccessor = {
                .type               = ZL_Type_numeric,
                .eltWidth           = 1,
                .successorIdx       = 1,
                .clusteringCodecIdx = 2
            };
            checkClusteringConfigTagHasTypeSuccessor(
                    config.get(), typeSuccessor, i);
        } else if (i < 10) {
            // Successor is 0 because no default makes 0 the choice of successor
            ZL_ClusteringConfig_TypeSuccessor typeSuccessor = {
                .type               = ZL_Type_numeric,
                .eltWidth           = 8,
                .successorIdx       = 0,
                .clusteringCodecIdx = 2
            };
            checkClusteringConfigTagHasTypeSuccessor(
                    config.get(), typeSuccessor, i);
        } else if (i < 15) {
            ZL_ClusteringConfig_TypeSuccessor typeSuccessor = {
                .type               = ZL_Type_serial,
                .eltWidth           = 1,
                .successorIdx       = 0,
                .clusteringCodecIdx = 0
            };
            checkClusteringConfigTagHasTypeSuccessor(
                    config.get(), typeSuccessor, i);
        } else {
            // Chooses the first valid string clustering codec
            ZL_ClusteringConfig_TypeSuccessor typeSuccessor = {
                .type               = ZL_Type_string,
                .eltWidth           = 0,
                .successorIdx       = 3,
                .clusteringCodecIdx = 3
            };
            checkClusteringConfigTagHasTypeSuccessor(
                    config.get(), typeSuccessor, i);
        }
    }
}

TEST_F(TestClusteringConfigBuilder, TestBuildStartingConfig)
{
    auto configBuilder = training::ClusteringConfigBuilder::buildStartingConfig(
            columnMetadata_,
            *cUtils_,
            typeToDefaultSuccessorIdxMap_,
            typeToClusteringCodecIdxsMap_);
    auto config = configBuilder.build();
    EXPECT_EQ(config->nbTypeDefaults, 0);
    EXPECT_EQ(config->nbClusters, 4);
    auto clusters = configBuilder.clusters();
    ZL_Type type;
    size_t width;
    for (size_t i = 0; i < 20; i++) {
        if (i < 5) {
            type  = ZL_Type_numeric;
            width = 1;
            auto typeWidthMatch =
                    [type,
                     width](const training::ClusteringConfigBuilder::Cluster&
                                    cluster) {
                        return cluster.typeSuccessor.type == type
                                && cluster.typeSuccessor.eltWidth == width;
                    };
            auto clusterIt = std::find_if(
                    clusters.begin(), clusters.end(), typeWidthMatch);
            EXPECT_TRUE(clusterIt != clusters.end());
            EXPECT_TRUE(clusterIt->memberTags.count(i) == 1);
        } else if (i < 10) {
            type  = ZL_Type_numeric;
            width = 8;
            auto typeWidthMatch =
                    [type,
                     width](const training::ClusteringConfigBuilder::Cluster&
                                    cluster) {
                        return cluster.typeSuccessor.type == type
                                && cluster.typeSuccessor.eltWidth == width;
                    };
            auto clusterIt = std::find_if(
                    clusters.begin(), clusters.end(), typeWidthMatch);
            EXPECT_TRUE(clusterIt != clusters.end());
            EXPECT_TRUE(clusterIt->memberTags.count(i) == 1);
        } else if (i < 15) {
            type  = ZL_Type_serial;
            width = 1;
            auto typeWidthMatch =
                    [type,
                     width](const training::ClusteringConfigBuilder::Cluster&
                                    cluster) {
                        return cluster.typeSuccessor.type == type
                                && cluster.typeSuccessor.eltWidth == width;
                    };
            auto clusterIt = std::find_if(
                    clusters.begin(), clusters.end(), typeWidthMatch);
            EXPECT_TRUE(clusterIt != clusters.end());
            EXPECT_TRUE(clusterIt->memberTags.count(i) == 1);
        } else {
            type  = ZL_Type_string;
            width = 0;
            auto typeWidthMatch =
                    [type,
                     width](const training::ClusteringConfigBuilder::Cluster&
                                    cluster) {
                        return cluster.typeSuccessor.type == type
                                && cluster.typeSuccessor.eltWidth == width;
                    };
            auto clusterIt = std::find_if(
                    clusters.begin(), clusters.end(), typeWidthMatch);
            EXPECT_TRUE(clusterIt != clusters.end());
            EXPECT_TRUE(clusterIt->memberTags.count(i) == 1);
        }
    }
}

TEST_F(TestClusteringConfigBuilder, TestBuildConfigAddInputToCluster)
{
    // This test will not include cases where the tag cannot be added, as the
    // behavior will be changed in the future.
    auto configBuilder =
            training::ClusteringConfigBuilder::buildFullSplitConfig(
                    columnMetadata_,
                    typeToDefaultSuccessorIdxMap_,
                    typeToClusteringCodecIdxsMap_);
    auto config = configBuilder.build();
    EXPECT_EQ(config->nbTypeDefaults, 0);
    EXPECT_EQ(config->nbClusters, 20);

    // Find cluster index with the tag 0
    auto clusters = configBuilder.clusters();
    int tag       = 0;
    auto tagMatch =
            [tag](const training::ClusteringConfigBuilder::Cluster& cluster) {
                return cluster.memberTags.count(tag) == 1;
            };
    auto it = std::find_if(clusters.begin(), clusters.end(), tagMatch);
    EXPECT_TRUE(it != clusters.end());
    int clusterIdx = it - clusters.begin();

    configBuilder = configBuilder.buildConfigAddInputToCluster(
            1, ZL_Type_numeric, 1, clusterIdx);
    config = configBuilder.build();
    EXPECT_EQ(config->nbClusters, 19);
    configBuilder = configBuilder.buildConfigAddInputToCluster(
            2, ZL_Type_numeric, 1, clusterIdx);
    config = configBuilder.build();
    EXPECT_EQ(config->nbClusters, 18);
    configBuilder = configBuilder.buildConfigAddInputToCluster(
            3, ZL_Type_numeric, 1, clusterIdx);
    config = configBuilder.build();
    EXPECT_EQ(config->nbClusters, 17);
    configBuilder = configBuilder.buildConfigAddInputToCluster(
            4, ZL_Type_numeric, 1, clusterIdx);
    config = configBuilder.build();
    EXPECT_EQ(config->nbClusters, 16);
    clusters = configBuilder.clusters();
    it       = std::find_if(clusters.begin(), clusters.end(), tagMatch);
    EXPECT_TRUE(it != clusters.end());
    EXPECT_TRUE(it->memberTags.size() == 5);
    for (size_t i = 0; i < 5; i++) {
        EXPECT_TRUE(it->memberTags.count(i) == 1);
    }
}

TEST_F(TestClusteringConfigBuilder, TestBuildSoloSplit)
{
    auto configBuilder = training::ClusteringConfigBuilder::buildStartingConfig(
            columnMetadata_,
            *cUtils_,
            typeToDefaultSuccessorIdxMap_,
            typeToClusteringCodecIdxsMap_);
    auto config = configBuilder.build();
    EXPECT_EQ(config->nbTypeDefaults, 0);
    EXPECT_EQ(config->nbClusters, 4);
    configBuilder = configBuilder.buildConfigClusterSoloSplit(
            columnMetadata_,
            *cUtils_,
            (training::ColumnInfo){
                    .tag = 0, .type = ZL_Type_numeric, .width = 1 });
    config = configBuilder.build();
    EXPECT_EQ(config->nbClusters, 5);
    configBuilder = configBuilder.buildConfigClusterSoloSplit(
            columnMetadata_,
            *cUtils_,
            (training::ColumnInfo){
                    .tag = 1, .type = ZL_Type_numeric, .width = 1 });
    config = configBuilder.build();
    EXPECT_EQ(config->nbClusters, 6);
    configBuilder = configBuilder.buildConfigClusterSoloSplit(
            columnMetadata_,
            *cUtils_,
            (training::ColumnInfo){
                    .tag = 2, .type = ZL_Type_numeric, .width = 1 });
    config = configBuilder.build();
    EXPECT_EQ(config->nbClusters, 7);
    // Expect tags 0-2 are separated into their own clusters
    auto clusters = configBuilder.clusters();
    for (size_t i = 0; i < 3; i++) {
        int tag       = (int)i;
        auto tagMatch = [tag](const training::ClusteringConfigBuilder::Cluster&
                                      cluster) {
            return cluster.memberTags.count(tag) == 1;
        };
        auto it = std::find_if(clusters.begin(), clusters.end(), tagMatch);
        EXPECT_TRUE(it != clusters.end());
        EXPECT_EQ(it->memberTags.size(), 1);
    }
}

TEST_F(TestClusteringConfigBuilder, TestBuildPairSplit)
{
    // This test will not include cases where pair split fails, as the
    // behavior will be changed in the future.
    auto configBuilder = training::ClusteringConfigBuilder::buildStartingConfig(
            columnMetadata_,
            *cUtils_,
            typeToDefaultSuccessorIdxMap_,
            typeToClusteringCodecIdxsMap_);
    auto config = configBuilder.build();
    EXPECT_EQ(config->nbTypeDefaults, 0);
    EXPECT_EQ(config->nbClusters, 4);
    configBuilder = configBuilder.buildConfigClusterPairSplit(
            columnMetadata_,
            *cUtils_,
            (training::ColumnInfo){
                    .tag = 0, .type = ZL_Type_numeric, .width = 1 },
            (training::ColumnInfo){
                    .tag = 1, .type = ZL_Type_numeric, .width = 1 });
    config = configBuilder.build();
    EXPECT_EQ(config->nbClusters, 5);
    configBuilder = configBuilder.buildConfigClusterPairSplit(
            columnMetadata_,
            *cUtils_,
            (training::ColumnInfo){
                    .tag = 2, .type = ZL_Type_numeric, .width = 1 },
            (training::ColumnInfo){
                    .tag = 3, .type = ZL_Type_numeric, .width = 1 });
    config = configBuilder.build();
    EXPECT_EQ(config->nbClusters, 6);
    // Expect tags {0,1} and {2,3} are separate tag sets in clusters
    auto clusters = configBuilder.clusters();
    for (size_t i = 0; i < 2; i++) {
        int tag       = (int)2 * i;
        auto tagMatch = [tag](const training::ClusteringConfigBuilder::Cluster&
                                      cluster) {
            return cluster.memberTags.count(tag) == 1;
        };
        auto it = std::find_if(clusters.begin(), clusters.end(), tagMatch);
        EXPECT_TRUE(it != clusters.end());
        EXPECT_EQ(it->memberTags.size(), 2);
        EXPECT_TRUE(it->memberTags.count(tag + 1) == 1);
    }
}

TEST_F(TestClusteringConfigBuilder, TestBuildConfigSingleClusterWithSuccessor)
{
    std::unordered_set<int> tags = { 0 };
    auto configBuilder           = training::ClusteringConfigBuilder::
            buildConfigSingleClusterWithSuccessor(
                    { 0 }, ZL_Type_numeric, 1, 0, 0);
    auto config = configBuilder.build();
    EXPECT_EQ(config->nbTypeDefaults, 0);
    EXPECT_EQ(config->nbClusters, 1);
    EXPECT_EQ(config->clusters[0].nbMemberTags, 1);
    EXPECT_EQ(config->clusters[0].memberTags[0], 0);
    EXPECT_EQ(config->clusters[0].typeSuccessor.type, ZL_Type_numeric);
    EXPECT_EQ(config->clusters[0].typeSuccessor.eltWidth, 1);
    EXPECT_EQ(config->clusters[0].typeSuccessor.successorIdx, 0);
    EXPECT_EQ(config->clusters[0].typeSuccessor.clusteringCodecIdx, 0);
}

TEST_F(TestClusteringConfigBuilder, TestConvertToConfigWithUniqueSuccessors)
{
    typeToDefaultSuccessorIdxMap_[{ ZL_Type_numeric, 1 }] = 1;
    typeToDefaultSuccessorIdxMap_[{ ZL_Type_string, 0 }]  = 3;
    auto configBuilder =
            training::ClusteringConfigBuilder::buildFullSplitConfig(
                    columnMetadata_,
                    typeToDefaultSuccessorIdxMap_,
                    typeToClusteringCodecIdxsMap_);
    // Find cluster index with the tag 0. This makes a config with 19 clusters
    // and 1 empty cluster
    auto clusters = configBuilder.clusters();
    int tag       = 0;
    auto tagMatch =
            [tag](const training::ClusteringConfigBuilder::Cluster& cluster) {
                return cluster.memberTags.count(tag) == 1;
            };
    auto it = std::find_if(clusters.begin(), clusters.end(), tagMatch);
    EXPECT_TRUE(it != clusters.end());
    int clusterIdx = it - clusters.begin();

    configBuilder = configBuilder.buildConfigAddInputToCluster(
            1, ZL_Type_numeric, 1, clusterIdx);

    auto oldConfig                         = configBuilder.build();
    std::vector<ZL_GraphID> successorsCopy = successors_;
    configBuilder.makeSuccessorIndicesUnique(successorsCopy);
    auto newConfig = configBuilder.build();
    EXPECT_EQ(newConfig->nbTypeDefaults, 2);
    EXPECT_EQ(newConfig->nbClusters, 19);
    EXPECT_EQ(successorsCopy.size(), 19);
    std::unordered_set<size_t> successorSet;
    // Check the graphIds are identical for new and old configs
    for (size_t i = 0; i < newConfig->nbClusters; i++) {
        auto oldGraph = successors_
                [oldConfig.get()->clusters[i].typeSuccessor.successorIdx];
        auto newSuccIdx =
                newConfig.get()->clusters[i].typeSuccessor.successorIdx;
        auto newGraph = successorsCopy[newSuccIdx];
        // Check that the graph picked has not changed
        EXPECT_EQ(oldGraph.gid, newGraph.gid);
        // Check that the new successors are unique
        EXPECT_EQ(successorSet.count(newSuccIdx), 0);
        successorSet.insert(newSuccIdx);
    }
}

} // namespace
} // namespace openzl::tests
