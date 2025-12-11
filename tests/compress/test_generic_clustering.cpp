// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/compress/graphs/generic_clustering_graph.h"
#include "openzl/shared/string_view.h"
#include "openzl/zl_input.h"

#include "tests/utils.h"

static bool operator==(
        const ZL_ClusteringConfig_TypeSuccessor& lhs,
        const ZL_ClusteringConfig_TypeSuccessor& rhs)
{
    if (lhs.eltWidth != rhs.eltWidth) {
        return false;
    }
    if (lhs.type != rhs.type) {
        return false;
    }
    if (lhs.successorIdx != rhs.successorIdx) {
        return false;
    }
    if (lhs.clusteringCodecIdx != rhs.clusteringCodecIdx) {
        return false;
    }
    return true;
}

static bool operator!=(
        const ZL_ClusteringConfig_TypeSuccessor& lhs,
        const ZL_ClusteringConfig_TypeSuccessor& rhs)
{
    return !(lhs == rhs);
}

static bool operator==(
        const ZL_ClusteringConfig_Cluster& lhs,
        const ZL_ClusteringConfig_Cluster& rhs)
{
    if (lhs.typeSuccessor != rhs.typeSuccessor) {
        return false;
    }
    if (lhs.nbMemberTags != rhs.nbMemberTags) {
        return false;
    }
    for (size_t i = 0; i < lhs.nbMemberTags; i++) {
        if (lhs.memberTags[i] != rhs.memberTags[i]) {
            return false;
        }
    }
    return true;
}

static bool operator!=(
        const ZL_ClusteringConfig_Cluster& lhs,
        const ZL_ClusteringConfig_Cluster& rhs)
{
    return !(lhs == rhs);
}

static bool operator==(
        const ZL_ClusteringConfig& lhs,
        const ZL_ClusteringConfig& rhs)
{
    if (lhs.nbClusters != rhs.nbClusters) {
        return false;
    }
    for (size_t i = 0; i < lhs.nbClusters; i++) {
        if (lhs.clusters[i] != rhs.clusters[i]) {
            return false;
        }
    }
    if (lhs.nbTypeDefaults != rhs.nbTypeDefaults) {
        return false;
    }
    for (size_t i = 0; i < lhs.nbTypeDefaults; i++) {
        if (lhs.typeDefaults[i] != rhs.typeDefaults[i]) {
            return false;
        }
    }
    return true;
}

static std::ostream& operator<<(
        std::ostream& os,
        const ZL_ClusteringConfig& config)
{
    // TODO: implement serialization for debugging
    return os;
}

namespace openzl::tests {
namespace {

class GenericClusteringTest : public testing::Test {
   public:
    GenericClusteringTest(const GenericClusteringTest&)            = delete;
    GenericClusteringTest& operator=(const GenericClusteringTest&) = delete;
    GenericClusteringTest(GenericClusteringTest&&)                 = delete;
    GenericClusteringTest& operator=(GenericClusteringTest&&)      = delete;

    GenericClusteringTest()
    {
        cctx_   = ZL_CCtx_create();
        cgraph_ = ZL_Compressor_create();
        dctx_   = ZL_DCtx_create();
    }

    ~GenericClusteringTest()
    {
        ZL_CCtx_free(cctx_);
        ZL_Compressor_free(cgraph_);
        ZL_DCtx_free(dctx_);
        for (auto& input : inputs_) {
            ZL_TypedRef_free(input);
        }
        for (auto& output : outputs_) {
            ZL_TypedBuffer_free(output);
        }
    }

   protected:
    void testRoundTrip(
            ZL_ClusteringConfig* config,
            std::vector<ZL_GraphID>& successors)
    {
        // Register the graph
        ZL_GraphID graph = ZL_Clustering_registerGraph(
                cgraph_, config, successors.data(), successors.size());

        ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, graph));
        ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
                cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
        ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));

        size_t totalSize = 0;
        for (auto& input : inputs_) {
            totalSize += ZL_Input_contentSize(input);
        }
        std::string compressed(totalSize, '\0');

        std::vector<const ZL_TypedRef*> inputs;
        inputs.reserve(inputs_.size());
        for (auto& input : inputs_) {
            inputs.push_back(input);
        }

        ZL_Report const r = ZL_CCtx_compressMultiTypedRef(
                cctx_,
                compressed.data(),
                compressed.size(),
                inputs.data(),
                inputs.size());
        ZL_REQUIRE_SUCCESS(r);
        compressed.resize(ZL_validResult(r));

        // Decompress
        outputs_.resize(inputs_.size());
        for (size_t i = 0; i < inputs_.size(); ++i) {
            outputs_[i] = ZL_TypedBuffer_create();
        }
        ZL_REQUIRE_SUCCESS(ZL_DCtx_decompressMultiTBuffer(
                dctx_,
                outputs_.data(),
                outputs_.size(),
                compressed.data(),
                compressed.size()));

        for (size_t i = 0; i < inputs_.size(); ++i) {
            EXPECT_EQ(
                    ZL_Data_contentSize(ZL_codemodOutputAsData(outputs_[i])),
                    ZL_Input_contentSize(inputs_[i]));
            EXPECT_EQ(
                    memcmp(ZL_Data_rPtr(ZL_codemodOutputAsData(outputs_[i])),
                           ZL_Input_ptr(inputs_[i]),
                           ZL_Input_contentSize(inputs_[i])),
                    0);
        }
    }

    ZL_CCtx* cctx_;
    ZL_DCtx* dctx_;
    ZL_Compressor* cgraph_;
    std::vector<ZL_TypedRef*> inputs_;
    std::vector<ZL_TypedBuffer*> outputs_;
};

TEST_F(GenericClusteringTest, TestClusteringConfigSerialization)
{
    // Set up clustering config for movie data
    std::string movies = kMoviesCsvFormatInput;
    ZL_ClusteringConfig movieClusteringConfig;
    std::vector<ZL_ClusteringConfig_Cluster> clusters;
    std::vector<std::vector<int>> clusterMemberTags;
    for (size_t i = 0; i < 3; i++) {
        ZL_ClusteringConfig_Cluster cluster;
        cluster.nbMemberTags        = 1;
        std::vector<int> memberTags = { (int)i };
        clusterMemberTags.push_back(memberTags);
        cluster.memberTags = clusterMemberTags[i].data();
        ZL_ClusteringConfig_TypeSuccessor typeSuccessor;
        typeSuccessor.eltWidth           = 8;
        typeSuccessor.type               = ZL_Type_numeric;
        typeSuccessor.successorIdx       = 1;
        typeSuccessor.clusteringCodecIdx = 2;
        cluster.typeSuccessor            = typeSuccessor;
        clusters.push_back(cluster);
    }
    movieClusteringConfig.clusters       = clusters.data();
    movieClusteringConfig.nbClusters     = clusters.size();
    movieClusteringConfig.nbTypeDefaults = 4;
    std::vector<ZL_ClusteringConfig_TypeSuccessor> typeDefaults;

    ZL_ClusteringConfig_TypeSuccessor typeSuccessorSerial;
    typeSuccessorSerial.eltWidth           = 1;
    typeSuccessorSerial.type               = ZL_Type_serial;
    typeSuccessorSerial.successorIdx       = 2;
    typeSuccessorSerial.clusteringCodecIdx = 0;
    typeDefaults.push_back(typeSuccessorSerial);

    ZL_ClusteringConfig_TypeSuccessor typeSuccessorStruct;
    typeSuccessorStruct.eltWidth           = 1;
    typeSuccessorStruct.type               = ZL_Type_struct;
    typeSuccessorStruct.successorIdx       = 2;
    typeSuccessorStruct.clusteringCodecIdx = 1;
    typeDefaults.push_back(typeSuccessorStruct);

    ZL_ClusteringConfig_TypeSuccessor typeSuccessorNumeric;
    typeSuccessorNumeric.eltWidth           = 8;
    typeSuccessorNumeric.type               = ZL_Type_numeric;
    typeSuccessorNumeric.successorIdx       = 1;
    typeSuccessorNumeric.clusteringCodecIdx = 2;
    typeDefaults.push_back(typeSuccessorNumeric);

    ZL_ClusteringConfig_TypeSuccessor typeSuccessorString;
    typeSuccessorString.eltWidth           = 1;
    typeSuccessorString.type               = ZL_Type_string;
    typeSuccessorString.successorIdx       = 0;
    typeSuccessorString.clusteringCodecIdx = 3;
    typeDefaults.push_back(typeSuccessorString);

    movieClusteringConfig.typeDefaults = typeDefaults.data();

    uint8_t* serDst    = nullptr;
    size_t serDstSize  = 0;
    Arena* arena       = ALLOC_HeapArena_create();
    A1C_Arena a1cArena = A1C_Arena_wrap(arena);
    ZL_Report r        = ZL_Clustering_serializeClusteringConfig(
            nullptr, &serDst, &serDstSize, &movieClusteringConfig, &a1cArena);
    EXPECT_TRUE(!ZL_isError(r));
    const auto serializedInput =
            StringView_init(reinterpret_cast<const char*>(serDst), serDstSize);
    void* jsonDst      = nullptr;
    size_t jsonDstSize = 0;
    ASSERT_FALSE(ZL_RES_isError(A1C_convert_cbor_to_json(
            NULL, arena, &jsonDst, &jsonDstSize, serializedInput)));
    const auto serializedInputJson =
            StringView_init(static_cast<const char*>(jsonDst), jsonDstSize);
    std::string expectedJson{
        R"({
  "clusters": [
    {
      "typeSuccessor": {
        "type": 4,
        "eltWidth": 8,
        "successorIdx": 1,
        "clusteringCodecIdx": 2
      },
      "memberTags": [
        0
      ]
    },
    {
      "typeSuccessor": {
        "type": 4,
        "eltWidth": 8,
        "successorIdx": 1,
        "clusteringCodecIdx": 2
      },
      "memberTags": [
        1
      ]
    },
    {
      "typeSuccessor": {
        "type": 4,
        "eltWidth": 8,
        "successorIdx": 1,
        "clusteringCodecIdx": 2
      },
      "memberTags": [
        2
      ]
    }
  ],
  "typeDefaults": [
    {
      "type": 1,
      "eltWidth": 1,
      "successorIdx": 2,
      "clusteringCodecIdx": 0
    },
    {
      "type": 2,
      "eltWidth": 1,
      "successorIdx": 2,
      "clusteringCodecIdx": 1
    },
    {
      "type": 4,
      "eltWidth": 8,
      "successorIdx": 1,
      "clusteringCodecIdx": 2
    },
    {
      "type": 8,
      "eltWidth": 1,
      "successorIdx": 0,
      "clusteringCodecIdx": 3
    }
  ]
})"
    };
    auto expectedJsonView =
            StringView_init(expectedJson.data(), expectedJson.size());
    EXPECT_TRUE(StringView_eq(&serializedInputJson, &expectedJsonView));

    // Test round trip
    ZL_RESULT_OF(ZL_ClusteringConfig)
    regeneratedConfig = ZL_Clustering_deserializeClusteringConfig(
            nullptr, serDst, serDstSize, &a1cArena);
    ;
    EXPECT_TRUE(!ZL_RES_isError(regeneratedConfig));
    EXPECT_EQ(movieClusteringConfig, ZL_RES_value(regeneratedConfig));

    ALLOC_Arena_freeArena(arena);
}

TEST_F(GenericClusteringTest, TestNoClusters)
{
    // Create the clustering config
    std::vector<ZL_GraphID> successors = { ZL_GRAPH_ZSTD, ZL_GRAPH_FIELD_LZ };
    ZL_ClusteringConfig config         = {};

    std::vector<ZL_ClusteringConfig_Cluster> clusters;

    config.clusters   = clusters.data();
    config.nbClusters = clusters.size();

    std::vector<ZL_ClusteringConfig_TypeSuccessor> typeDefaults = {
        { .type               = ZL_Type_serial,
          .eltWidth           = 1,
          .successorIdx       = 0,
          .clusteringCodecIdx = 0 },
        { .type               = ZL_Type_numeric,
          .eltWidth           = 4,
          .successorIdx       = 1,
          .clusteringCodecIdx = 2 },
    };

    config.nbTypeDefaults = typeDefaults.size();
    config.typeDefaults   = typeDefaults.data();

    // Create the inputs
    size_t nbInputs = 10;
    std::vector<std::string> data(nbInputs);
    inputs_.resize(nbInputs);

    for (size_t i = 0; i < 10; i++) {
        data[i]    = std::string(100, i);
        inputs_[i] = ZL_TypedRef_createSerial(data[i].data(), data[i].size());
        ZL_REQUIRE_SUCCESS(ZL_Input_setIntMetadata(
                inputs_[i], ZL_CLUSTERING_TAG_METADATA_ID, i % 4));
    }

    testRoundTrip(&config, successors);
}

TEST_F(GenericClusteringTest, TestEmptyConfig)
{
    // Create the clustering config
    std::vector<ZL_GraphID> successors = { ZL_GRAPH_ZSTD, ZL_GRAPH_FIELD_LZ };
    ZL_ClusteringConfig config         = {};

    // Create the inputs
    size_t nbInputs = 4;
    inputs_.resize(nbInputs);

    std::string data(100, 'a');
    std::vector<uint32_t> lens = { 20, 20, 20, 20, 20 };

    inputs_[0] = ZL_TypedRef_createSerial(data.data(), data.size());
    ZL_REQUIRE_SUCCESS(ZL_Input_setIntMetadata(
            inputs_[0], ZL_CLUSTERING_TAG_METADATA_ID, 0));
    inputs_[1] = ZL_TypedRef_createNumeric(data.data(), 4, data.size() / 4);
    ZL_REQUIRE_SUCCESS(ZL_Input_setIntMetadata(
            inputs_[1], ZL_CLUSTERING_TAG_METADATA_ID, 1));
    inputs_[2] = ZL_TypedRef_createStruct(data.data(), 10, data.size() / 10);
    ZL_REQUIRE_SUCCESS(ZL_Input_setIntMetadata(
            inputs_[2], ZL_CLUSTERING_TAG_METADATA_ID, 2));
    inputs_[3] = ZL_TypedRef_createString(
            data.data(), data.size(), lens.data(), lens.size());
    ZL_REQUIRE_SUCCESS(ZL_Input_setIntMetadata(
            inputs_[3], ZL_CLUSTERING_TAG_METADATA_ID, 3));

    testRoundTrip(&config, successors);
}

} // namespace
} // namespace openzl::tests
