// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/codecs/encoder_registry.h"
#include "openzl/codecs/zl_delta.h"
#include "openzl/codecs/zl_field_lz.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/codecs/zl_zstd.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/wire_format.h"
#include "openzl/compress/graph_registry.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_reflection.h"

using namespace ::testing;

class ReflectionTest : public ::testing::Test {
   public:
    void SetUp() override
    {
        cgraph_ = ZL_Compressor_create();
        rctx_   = ZL_ReflectionCtx_create();

        ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
                cgraph_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    }

    void TearDown() override
    {
        ZL_Compressor_free(cgraph_);
        cgraph_ = nullptr;
        ZL_ReflectionCtx_free(rctx_);
        rctx_ = nullptr;
    }

    void initializeReflectionCtx(ZL_GraphID graph, std::string_view data)
    {
        std::string compressed;
        compressed.resize(ZL_compressBound(data.size()));
        ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, graph));
        auto const report = ZL_compress_usingCompressor(
                compressed.data(),
                compressed.size(),
                data.data(),
                data.size(),
                cgraph_);
        ZL_REQUIRE_SUCCESS(report);
        ZL_REQUIRE_SUCCESS(ZL_ReflectionCtx_setCompressedFrame(
                rctx_, compressed.data(), ZL_validResult(report)));
    }

    std::string_view streamContent(const ZL_DataInfo* streamInfo) const
    {
        return { (const char*)ZL_DataInfo_getDataPtr(streamInfo),
                 ZL_DataInfo_getContentSize(streamInfo) };
    }

    ZL_Compressor* cgraph_  = nullptr;
    ZL_ReflectionCtx* rctx_ = nullptr;
};

TEST_F(ReflectionTest, Store)
{
    initializeReflectionCtx(ZL_GRAPH_STORE, "data");
    ASSERT_EQ(
            ZL_ReflectionCtx_getFrameFormatVersion(rctx_),
            (uint32_t)ZL_MAX_FORMAT_VERSION);
    ASSERT_NE(ZL_ReflectionCtx_getFrameHeaderSize(rctx_), 0u);
    ASSERT_NE(ZL_ReflectionCtx_getFrameFooterSize(rctx_), 0u);
    ASSERT_EQ(
            ZL_ReflectionCtx_getTotalTransformHeaderSize_lastChunk(rctx_), 0u);
    ASSERT_EQ(ZL_ReflectionCtx_getNumStreams_lastChunk(rctx_), 1u);
    ASSERT_EQ(ZL_ReflectionCtx_getNumInputs(rctx_), 1u);
    ASSERT_EQ(ZL_ReflectionCtx_getNumStoredOutputs_lastChunk(rctx_), 1u);
    auto streamInfo = ZL_ReflectionCtx_getStream_lastChunk(rctx_, 0u);
    ASSERT_EQ(streamInfo, ZL_ReflectionCtx_getInput(rctx_, 0u));
    ASSERT_EQ(
            streamInfo, ZL_ReflectionCtx_getStoredOutput_lastChunk(rctx_, 0u));
    ASSERT_EQ(ZL_ReflectionCtx_getNumCodecs_lastChunk(rctx_), 0u);

    ASSERT_EQ(ZL_DataInfo_getIndex(streamInfo), 0u);
    ASSERT_EQ(ZL_DataInfo_getType(streamInfo), ZL_Type_serial);
    ASSERT_EQ(streamContent(streamInfo), "data");

    ASSERT_EQ(ZL_DataInfo_getProducerCodec(streamInfo), nullptr);
    ASSERT_EQ(ZL_DataInfo_getConsumerCodec(streamInfo), nullptr);
}

TEST_F(ReflectionTest, Zstd)
{
    const std::string_view data =
            "hello hello hello hello hello hello hello hello";
    initializeReflectionCtx(ZL_GRAPH_ZSTD, data);

    ASSERT_EQ(ZL_ReflectionCtx_getNumStreams_lastChunk(rctx_), 2u);
    ASSERT_EQ(ZL_ReflectionCtx_getNumInputs(rctx_), 1u);
    ASSERT_EQ(ZL_ReflectionCtx_getNumStoredOutputs_lastChunk(rctx_), 1u);

    const auto inputStream = ZL_ReflectionCtx_getInput(rctx_, 0u);
    const auto storedStream =
            ZL_ReflectionCtx_getStoredOutput_lastChunk(rctx_, 0u);
    ASSERT_EQ(streamContent(inputStream), data);
    ASSERT_NE(streamContent(storedStream), data);

    ASSERT_EQ(ZL_ReflectionCtx_getNumCodecs_lastChunk(rctx_), 1u);
    const auto transform = ZL_ReflectionCtx_getCodec_lastChunk(rctx_, 0u);

    ASSERT_EQ(ZL_DataInfo_getProducerCodec(inputStream), nullptr);
    ASSERT_EQ(ZL_DataInfo_getProducerCodec(storedStream), transform);

    ASSERT_EQ(ZL_DataInfo_getConsumerCodec(inputStream), transform);
    ASSERT_EQ(ZL_DataInfo_getConsumerCodec(storedStream), nullptr);

    ASSERT_EQ(ZL_DataInfo_getIndex(inputStream), 1u);
    ASSERT_EQ(ZL_DataInfo_getIndex(storedStream), 0u);

    ASSERT_EQ(std::string(ZL_CodecInfo_getName(transform)), "zstd");
    ASSERT_EQ(ZL_CodecInfo_getCodecID(transform), ZL_StandardTransformID_zstd);
    ASSERT_TRUE(ZL_CodecInfo_isStandardCodec(transform));
    ASSERT_FALSE(ZL_CodecInfo_isCustomCodec(transform));
    ASSERT_EQ(ZL_CodecInfo_getIndex(transform), 0u);

    ASSERT_EQ(ZL_CodecInfo_getNumInputs(transform), 1u);
    ASSERT_EQ(ZL_CodecInfo_getInput(transform, 0u), inputStream);

    ASSERT_EQ(ZL_CodecInfo_getNumOutputs(transform), 1u);
    ASSERT_EQ(ZL_CodecInfo_getOutput(transform, 0u), storedStream);

    ASSERT_EQ(ZL_CodecInfo_getNumVariableOutputs(transform), 0u);
}

TEST_F(ReflectionTest, Conversion)
{
    const std::string_view data =
            "012345670123456701234567012345670123456701234567";
    const auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph_, ZL_NODE_INTERPRET_AS_LE64, ZL_GRAPH_CONSTANT);
    initializeReflectionCtx(graph, data);

    ASSERT_EQ(
            ZL_ReflectionCtx_getTotalTransformHeaderSize_lastChunk(rctx_), 2u);

    ASSERT_EQ(ZL_ReflectionCtx_getNumCodecs_lastChunk(rctx_), 4u);
    ASSERT_EQ(ZL_ReflectionCtx_getNumStoredOutputs_lastChunk(rctx_), 1u);
    const auto storedStream =
            ZL_ReflectionCtx_getStoredOutput_lastChunk(rctx_, 0u);
    ASSERT_EQ(streamContent(storedStream), "01234567");
}

TEST(VersionGetterTest, GraphVersionSucceedsForAllStandardGraphs)
{
    size_t nbGraphs = GR_getNbStandardGraphs();
    std::vector<ZL_GraphID> graphs(nbGraphs);
    GR_getAllStandardGraphIDs(graphs.data(), graphs.size());

    for (const auto& gid : graphs) {
        ZL_Report report =
                ZL_Compressor_Graph_getMinLibraryVersion(nullptr, gid);
        EXPECT_FALSE(ZL_isError(report))
                << "Expected success for standard graph ID " << gid.gid;
    }
}

TEST(VersionGetterTest,
     GraphVersionReturnsParameterInvalidForOutOfBoundsGraphID)
{
    ZL_GraphID invalid = { .gid = 99999 };
    ZL_Report report =
            ZL_Compressor_Graph_getMinLibraryVersion(nullptr, invalid);
    EXPECT_TRUE(ZL_isError(report));
    EXPECT_EQ(ZL_errorCode(report), ZL_ErrorCode_parameter_invalid);
}

TEST(VersionGetterTest, NodeVersionSucceedsForAllStandardNodes)
{
    size_t nbNodes = ER_getNbStandardNodes();
    std::vector<ZL_NodeID> nodes(nbNodes);
    ER_getAllStandardNodeIDs(nodes.data(), nodes.size());

    for (const auto& nid : nodes) {
        ZL_Report report =
                ZL_Compressor_Node_getMinLibraryVersion(nullptr, nid);
        EXPECT_FALSE(ZL_isError(report))
                << "Expected success for standard node ID " << nid.nid;
    }
}

TEST(VersionGetterTest, NodeVersionReturnsParameterInvalidForOutOfBoundsID)
{
    ZL_NodeID invalid = { .nid = 99999 };
    ZL_Report report =
            ZL_Compressor_Node_getMinLibraryVersion(nullptr, invalid);
    EXPECT_TRUE(ZL_isError(report));
    EXPECT_EQ(ZL_errorCode(report), ZL_ErrorCode_parameter_invalid);
}

TEST(VersionGetterTest, NodeVersionReturnsNodeInvalidForIllegalNode)
{
    ZL_NodeID illegal = ZL_NODE_ILLEGAL;
    ZL_Report report =
            ZL_Compressor_Node_getMinLibraryVersion(nullptr, illegal);
    EXPECT_TRUE(ZL_isError(report));
    EXPECT_EQ(ZL_errorCode(report), ZL_ErrorCode_node_invalid);
}
