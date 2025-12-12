// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "custom_parsers/csv/csv_segmenter.h"
#include "custom_parsers/tests/DebugIntrospectionHooks.h"
#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/graphs/generic_clustering_graph.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"

#include "tests/utils.h"

namespace openzl::tests {
namespace {

class TestClusteringGraph : public testing::Test {
   public:
    void TearDown() override
    {
        ZL_CCtx_free(cctx_);
        ZL_DCtx_free(dctx_);
        ZL_Compressor_free(cgraph_);
    }

    void SetUp() override
    {
        successors_.push_back(ZL_GRAPH_COMPRESS_GENERIC);
        successors_.push_back(ZL_GRAPH_COMPRESS_GENERIC);
        successors_.push_back(ZL_GRAPH_COMPRESS_GENERIC);
        successors_.push_back(ZL_GRAPH_COMPRESS_GENERIC);

        cgraph_ = ZL_Compressor_create();
        dctx_   = ZL_DCtx_create();
        cctx_   = ZL_CCtx_create();
        ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
                cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
        hooks_ = std::make_unique<openzl::DebugIntrospectionHooks>();
        if (0) {
            ZL_REQUIRE_SUCCESS(ZL_CCtx_attachIntrospectionHooks(
                    cctx_, hooks_->getRawHooks()));
        }
    }

    ZL_GraphID getClusterAndCompressCsvGraph(ZL_Compressor* cgraph)
    {
        ZL_GraphID clusteringGraph = ZL_Clustering_registerGraph(
                cgraph,
                paramInfo_.clusteringConfig,
                successors_.data(),
                successors_.size());

        return ZL_RES_value(ZL_CsvSegmenter_registerSegmenterNoChunks(
                cgraph, true, ',', false, clusteringGraph));
    }

    size_t compress(
            void* dst,
            size_t dstCapacity,
            const void* src,
            size_t srcSize,
            ZL_GraphID sgid)
    {
        ZL_REQUIRE_GE(dstCapacity, ZL_compressBound(srcSize));
        ZL_REQUIRE_NN(cctx_);
        ZL_REQUIRE_NN(cgraph_);
        ZL_Report const gssr =
                ZL_Compressor_selectStartingGraphID(cgraph_, sgid);
        EXPECT_EQ(ZL_isError(gssr), 0)
                << "selection of starting graphid failed\n";
        ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx_, cgraph_);
        EXPECT_EQ(ZL_isError(rcgr), 0) << "CGraph reference failed\n";
        ZL_Report const r =
                ZL_CCtx_compress(cctx_, dst, dstCapacity, src, srcSize);
        EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";
        if (ZL_isError(r)) {
            std::cout << "compress Error: "
                      << ZL_CCtx_getErrorContextString(cctx_, r) << std::endl;
        }
        return ZL_validResult(r);
    }
    /* ------   decompress   -------- */

    size_t
    decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
    {
        // Check buffer size
        ZL_Report const dr = ZL_getDecompressedSize(src, srcSize);
        ZL_REQUIRE(!ZL_isError(dr));
        size_t const dstSize = ZL_validResult(dr);
        ZL_REQUIRE_GE(dstCapacity, dstSize);

        // Create a single decompression state, to store the custom decoder(s)
        // The decompression state will be re-employed
        ZL_REQUIRE_NN(dctx_);
        ZL_Report const r =
                ZL_DCtx_decompress(dctx_, dst, dstCapacity, src, srcSize);
        EXPECT_EQ(ZL_isError(r), 0) << "decompression failed \n";
        if (ZL_isError(r)) {
            std::cout << "decompress Error: "
                      << ZL_DCtx_getErrorContextString(dctx_, r) << std::endl;
        }
        return ZL_validResult(r);
    }

    void testRoundTrip(std::string input, ZL_GraphID sgid)
    {
        auto compressBound = ZL_compressBound(input.size());
        // Run dynamic graph
        std::vector<uint8_t> outBuff(compressBound);
        auto csize = compress(
                outBuff.data(),
                outBuff.size(),
                input.data(),
                input.size(),
                sgid);
        outBuff.resize(csize);
        std::string decompressed(input.size(), '\0');
        auto dsize = decompress(
                decompressed.data(),
                decompressed.size(),
                outBuff.data(),
                csize);
        EXPECT_EQ(dsize, input.size());
        EXPECT_EQ(decompressed, input);
    }

   protected:
    struct csvParserLocalParamInfo {
        ZL_ClusteringConfig* clusteringConfig{};
        std::vector<ZL_Type> columnTypes;
    };

    ZL_Compressor* cgraph_{};
    ZL_DCtx* dctx_{};
    ZL_CCtx* cctx_{};
    std::unique_ptr<openzl::DebugIntrospectionHooks> hooks_;
    std::vector<ZL_GraphID> successors_; /* Assume successors are registered in
                                             the same cgraph */
    std::vector<ZL_Type> defaultSuccessorTypes_{ ZL_Type_serial,
                                                 ZL_Type_struct,
                                                 ZL_Type_numeric,
                                                 ZL_Type_string };
    csvParserLocalParamInfo paramInfo_;
};

/* ------   compress, using provided graph function   -------- */

TEST_F(TestClusteringGraph, TestClusteringGraphRoundTrip)
{
    std::string movies = kMoviesCsvFormatInput;
    // Set up clustering config for movie data
    ZL_ClusteringConfig movieClusteringConfig;
    std::vector<ZL_ClusteringConfig_Cluster> clusters;
    std::vector<std::vector<int>> clusterMemberTags;
    for (size_t i = 0; i < 3; i++) {
        ZL_ClusteringConfig_Cluster cluster;
        cluster.nbMemberTags        = 1;
        std::vector<int> memberTags = { (int)i };
        clusterMemberTags.push_back(memberTags);
        cluster.memberTags = clusterMemberTags[i].data();
        if (i == 0 || i == 1) {
            ZL_ClusteringConfig_TypeSuccessor typeSuccessor;
            typeSuccessor.eltWidth           = 8;
            typeSuccessor.type               = ZL_Type_numeric;
            typeSuccessor.successorIdx       = 1;
            typeSuccessor.clusteringCodecIdx = 2;
            cluster.typeSuccessor            = typeSuccessor;
        } else {
            ZL_ClusteringConfig_TypeSuccessor typeSuccessor;
            typeSuccessor.eltWidth           = 0;
            typeSuccessor.type               = ZL_Type_string;
            typeSuccessor.successorIdx       = 3;
            typeSuccessor.clusteringCodecIdx = 3;
            cluster.typeSuccessor            = typeSuccessor;
        }
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
    typeSuccessorString.eltWidth           = 0;
    typeSuccessorString.type               = ZL_Type_string;
    typeSuccessorString.successorIdx       = 3;
    typeSuccessorString.clusteringCodecIdx = 3;
    typeDefaults.push_back(typeSuccessorString);

    movieClusteringConfig.typeDefaults = typeDefaults.data();

    paramInfo_.clusteringConfig = &movieClusteringConfig;
    paramInfo_.columnTypes      = { ZL_Type_numeric,
                                    ZL_Type_numeric,
                                    ZL_Type_string };
    // Register cluster and compress graph
    auto clusterAndCompressCsvGraph = getClusterAndCompressCsvGraph(cgraph_);
    testRoundTrip(movies, clusterAndCompressCsvGraph);
}

TEST_F(TestClusteringGraph, TestClusteringGraphRoundTripConfigMissingTags)
{
    std::string movies = kMoviesCsvFormatInput;
    // Set up clustering config for movie data
    ZL_ClusteringConfig movieClusteringConfig;
    std::vector<ZL_ClusteringConfig_Cluster> clusters;
    std::vector<std::vector<int>> clusterMemberTags;
    for (size_t i = 0; i < 2; i++) {
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
    typeSuccessorString.eltWidth           = 0;
    typeSuccessorString.type               = ZL_Type_string;
    typeSuccessorString.successorIdx       = 3;
    typeSuccessorString.clusteringCodecIdx = 3;
    typeDefaults.push_back(typeSuccessorString);

    movieClusteringConfig.typeDefaults = typeDefaults.data();

    paramInfo_.clusteringConfig = &movieClusteringConfig;
    paramInfo_.columnTypes      = { ZL_Type_numeric,
                                    ZL_Type_numeric,
                                    ZL_Type_string };
    // Register cluster and compress graph
    auto clusterAndCompressCsvGraph = getClusterAndCompressCsvGraph(cgraph_);
    testRoundTrip(movies, clusterAndCompressCsvGraph);
}

TEST_F(TestClusteringGraph, TestClusteringGraphRoundTripInputMissingTags)
{
    std::string movies = kMoviesCsvFormatInput;
    // Set up clustering config for movie data
    ZL_ClusteringConfig movieClusteringConfig;
    std::vector<ZL_ClusteringConfig_Cluster> clusters;
    std::vector<std::vector<int>> clusterMemberTags;
    for (size_t i = 0; i < 3; i++) {
        ZL_ClusteringConfig_Cluster cluster;
        cluster.nbMemberTags        = 1;
        std::vector<int> memberTags = { (int)i };
        if (i == 2) {
            memberTags[0] = 3;
        }
        clusterMemberTags.push_back(memberTags);
        cluster.memberTags = clusterMemberTags[i].data();
        if (i == 0 || i == 1) {
            ZL_ClusteringConfig_TypeSuccessor typeSuccessor;
            typeSuccessor.eltWidth           = 8;
            typeSuccessor.type               = ZL_Type_numeric;
            typeSuccessor.successorIdx       = 1;
            typeSuccessor.clusteringCodecIdx = 2;
            cluster.typeSuccessor            = typeSuccessor;
        } else {
            ZL_ClusteringConfig_TypeSuccessor typeSuccessor;
            typeSuccessor.eltWidth           = 0;
            typeSuccessor.type               = ZL_Type_string;
            typeSuccessor.successorIdx       = 3;
            typeSuccessor.clusteringCodecIdx = 3;
            cluster.typeSuccessor            = typeSuccessor;
        }
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
    typeSuccessorString.eltWidth           = 0;
    typeSuccessorString.type               = ZL_Type_string;
    typeSuccessorString.successorIdx       = 3;
    typeSuccessorString.clusteringCodecIdx = 3;
    typeDefaults.push_back(typeSuccessorString);

    movieClusteringConfig.typeDefaults = typeDefaults.data();
    paramInfo_.clusteringConfig        = &movieClusteringConfig;
    paramInfo_.columnTypes             = { ZL_Type_numeric,
                                           ZL_Type_numeric,
                                           ZL_Type_string };
    // Register cluster and compress graph
    auto clusterAndCompressCsvGraph = getClusterAndCompressCsvGraph(cgraph_);
    testRoundTrip(movies, clusterAndCompressCsvGraph);
}

TEST_F(TestClusteringGraph, TestClusteringGraphRoundTripClusterColumns)
{
    std::string grades = kStudentGradesCsvFormatInput;
    ZL_ClusteringConfig gradesClusteringConfig;
    std::vector<ZL_ClusteringConfig_Cluster> clusters;

    gradesClusteringConfig.clusters       = clusters.data();
    gradesClusteringConfig.nbClusters     = clusters.size();
    gradesClusteringConfig.nbTypeDefaults = 4;
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
    typeSuccessorString.eltWidth           = 0;
    typeSuccessorString.type               = ZL_Type_string;
    typeSuccessorString.successorIdx       = 3;
    typeSuccessorString.clusteringCodecIdx = 3;
    typeDefaults.push_back(typeSuccessorString);

    ZL_ClusteringConfig_Cluster cluster1;
    std::vector<int> cluster1MemberTags = { 0, 1 };
    cluster1.nbMemberTags               = 2;
    cluster1.memberTags                 = cluster1MemberTags.data();
    cluster1.typeSuccessor              = typeSuccessorString;
    clusters.push_back(cluster1);

    ZL_ClusteringConfig_Cluster cluster2;
    std::vector<int> cluster2MemberTags = { 2 };
    cluster2.nbMemberTags               = 1;
    cluster2.memberTags                 = cluster2MemberTags.data();
    cluster2.typeSuccessor              = typeSuccessorString;
    clusters.push_back(cluster2);

    ZL_ClusteringConfig_Cluster cluster3;
    std::vector<int> cluster3MemberTags = { 3, 4, 5, 6, 7 };
    cluster3.nbMemberTags               = 5;
    cluster3.memberTags                 = cluster3MemberTags.data();
    cluster3.typeSuccessor              = typeSuccessorNumeric;
    clusters.push_back(cluster3);

    ZL_ClusteringConfig_Cluster cluster4;
    std::vector<int> cluster4MemberTags = { 8 };
    cluster4.nbMemberTags               = 1;
    cluster4.memberTags                 = cluster4MemberTags.data();
    cluster4.typeSuccessor              = typeSuccessorString;
    clusters.push_back(cluster4);

    gradesClusteringConfig.typeDefaults   = typeDefaults.data();
    gradesClusteringConfig.clusters       = clusters.data();
    gradesClusteringConfig.nbClusters     = clusters.size();
    gradesClusteringConfig.nbTypeDefaults = 4;

    paramInfo_.clusteringConfig = &gradesClusteringConfig;
    paramInfo_.columnTypes      = { ZL_Type_string,  ZL_Type_string,
                                    ZL_Type_string,  ZL_Type_numeric,
                                    ZL_Type_numeric, ZL_Type_numeric,
                                    ZL_Type_numeric, ZL_Type_numeric,
                                    ZL_Type_string };
    // Register cluster and compress graph
    auto clusterAndCompressCsvGraph = getClusterAndCompressCsvGraph(cgraph_);
    testRoundTrip(grades, clusterAndCompressCsvGraph);
}

TEST_F(TestClusteringGraph, TestClusteringClusterCodecIndexBoundsCheck)
{
    std::vector<ZL_GraphID> testSuccessors = { ZL_GRAPH_STORE };
    const ZL_NodeID codecs[2]              = { ZL_NODE_CONCAT_STRING,
                                               ZL_NODE_CONCAT_NUMERIC };

    // Test case 1: Index > nbClusteringCodecs (should fail)
    {
        std::vector<ZL_ClusteringConfig_Cluster> clusters;
        std::vector<int> memberTags = { 999 };
        ZL_ClusteringConfig_Cluster cluster;
        cluster.nbMemberTags = 1;
        cluster.memberTags   = memberTags.data();
        ZL_ClusteringConfig_TypeSuccessor clusterTs;
        clusterTs.type               = ZL_Type_string;
        clusterTs.eltWidth           = 0;
        clusterTs.successorIdx       = 0;
        clusterTs.clusteringCodecIdx = 1; // Out of bounds (should be < 1)
        cluster.typeSuccessor        = clusterTs;
        clusters.push_back(cluster);

        ZL_ClusteringConfig config = { .clusters       = clusters.data(),
                                       .nbClusters     = 1,
                                       .typeDefaults   = nullptr,
                                       .nbTypeDefaults = 0 };

        ZL_GraphID graph =
                ZL_Clustering_registerGraphWithCustomClusteringCodecs(
                        cgraph_, &config, testSuccessors.data(), 1, codecs, 1);

        EXPECT_EQ(graph.gid, ZL_GRAPH_ILLEGAL.gid)
                << "Expected failure with out-of-bounds cluster codec index";
    }

    // Test case 2: Index == nbClusteringCodecs (should fail)
    {
        std::vector<ZL_ClusteringConfig_Cluster> clusters;
        std::vector<int> memberTags = { 999 };
        ZL_ClusteringConfig_Cluster cluster;
        cluster.nbMemberTags = 1;
        cluster.memberTags   = memberTags.data();
        ZL_ClusteringConfig_TypeSuccessor clusterTs;
        clusterTs.type         = ZL_Type_string;
        clusterTs.eltWidth     = 0;
        clusterTs.successorIdx = 0;
        clusterTs.clusteringCodecIdx =
                2; // Equal to nbClusteringCodecs (should fail)
        cluster.typeSuccessor = clusterTs;
        clusters.push_back(cluster);

        ZL_ClusteringConfig config = { .clusters       = clusters.data(),
                                       .nbClusters     = 1,
                                       .typeDefaults   = nullptr,
                                       .nbTypeDefaults = 0 };

        ZL_GraphID graph =
                ZL_Clustering_registerGraphWithCustomClusteringCodecs(
                        cgraph_, &config, testSuccessors.data(), 1, codecs, 2);

        EXPECT_EQ(graph.gid, ZL_GRAPH_ILLEGAL.gid)
                << "Expected failure when index equals nbClusteringCodecs";
    }
}

TEST_F(TestClusteringGraph, TestClusteringTypeDefaultCodecIndexOutOfBounds)
{
    std::vector<ZL_GraphID> testSuccessors = { ZL_GRAPH_STORE };
    const ZL_NodeID codecs[1]              = { ZL_NODE_CONCAT_STRING };

    std::vector<ZL_ClusteringConfig_TypeSuccessor> typeDefaults;
    ZL_ClusteringConfig_TypeSuccessor typeDefault;
    typeDefault.type               = ZL_Type_string;
    typeDefault.eltWidth           = 0;
    typeDefault.successorIdx       = 0;
    typeDefault.clusteringCodecIdx = 1; // Out of bounds (should be < 1)
    typeDefaults.push_back(typeDefault);

    ZL_ClusteringConfig config = { .clusters       = nullptr,
                                   .nbClusters     = 0,
                                   .typeDefaults   = typeDefaults.data(),
                                   .nbTypeDefaults = 1 };

    ZL_GraphID graph = ZL_Clustering_registerGraphWithCustomClusteringCodecs(
            cgraph_, &config, testSuccessors.data(), 1, codecs, 1);

    EXPECT_EQ(graph.gid, ZL_GRAPH_ILLEGAL.gid)
            << "Expected failure with out-of-bounds typeDefault codec index";
}

TEST_F(TestClusteringGraph, TestClusteringValidCodecIndices)
{
    std::vector<ZL_GraphID> testSuccessors = { ZL_GRAPH_STORE };
    const ZL_NodeID codecs[2]              = { ZL_NODE_CONCAT_STRING,
                                               ZL_NODE_CONCAT_NUMERIC };

    std::vector<ZL_ClusteringConfig_Cluster> clusters;
    std::vector<int> memberTags = { 999 };
    ZL_ClusteringConfig_Cluster cluster;
    cluster.nbMemberTags = 1;
    cluster.memberTags   = memberTags.data();
    ZL_ClusteringConfig_TypeSuccessor clusterTs;
    clusterTs.type               = ZL_Type_string;
    clusterTs.eltWidth           = 0;
    clusterTs.successorIdx       = 0;
    clusterTs.clusteringCodecIdx = 1; // Valid index (< 2)
    cluster.typeSuccessor        = clusterTs;
    clusters.push_back(cluster);

    std::vector<ZL_ClusteringConfig_TypeSuccessor> typeDefaults;
    ZL_ClusteringConfig_TypeSuccessor typeDefault;
    typeDefault.type               = ZL_Type_numeric;
    typeDefault.eltWidth           = 8;
    typeDefault.successorIdx       = 0;
    typeDefault.clusteringCodecIdx = 0; // Valid index
    typeDefaults.push_back(typeDefault);

    ZL_ClusteringConfig config = { .clusters       = clusters.data(),
                                   .nbClusters     = 1,
                                   .typeDefaults   = typeDefaults.data(),
                                   .nbTypeDefaults = 1 };

    ZL_GraphID graph = ZL_Clustering_registerGraphWithCustomClusteringCodecs(
            cgraph_, &config, testSuccessors.data(), 1, codecs, 2);

    EXPECT_NE(graph.gid, ZL_GRAPH_ILLEGAL.gid)
            << "Expected success with valid indices";
}

} // namespace
} // namespace openzl::tests
