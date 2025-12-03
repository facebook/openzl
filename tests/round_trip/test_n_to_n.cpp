// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <stdio.h>
#include <string.h>

#include "openzl/common/debug.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_graphs.h"

#include "tests/utils.h"

namespace {

// N-to-N routing helper: routes input[i] to customGraph[i]
// This replicates ZL_nToNFnGraph logic with noexcept for C++ testing
static ZL_Report testNToNRouting(
        ZL_Graph* graph,
        ZL_Edge* inputs[],
        size_t numInputs) noexcept
{
    ZL_GraphIDList const graphs = ZL_Graph_getCustomGraphs(graph);

    if (graphs.nbGraphIDs != numInputs) {
        return ZL_returnError(ZL_ErrorCode_parameter_invalid);
    }

    for (size_t i = 0; i < numInputs; ++i) {
        ZL_Report result = ZL_Edge_setDestination(inputs[i], graphs.graphids[i]);
        if (ZL_isError(result)) {
            return result;
        }
    }

    return ZL_returnSuccess();
}

static void fillTestData(int* data, size_t count, int startValue)
{
    for (size_t i = 0; i < count; i++) {
        data[i] = startValue + (int)i;
    }
}

TEST(NToNGraphTest, GraphIDRegistration)
{
    ZL_GraphID n_to_n_graph = ZL_GRAPH_N_TO_N;

    EXPECT_EQ(n_to_n_graph.gid, ZL_PrivateStandardGraphID_n_to_n);
    EXPECT_TRUE(ZL_GraphID_isValid(n_to_n_graph));
}

TEST(NToNGraphTest, FunctionalRoutingThreeStreams)
{
    ZL_Compressor* compressor = ZL_Compressor_create();
    ASSERT_NE(compressor, nullptr);

    static const ZL_GraphID successors[3] = {
        ZL_GRAPH_STORE,
        ZL_GRAPH_COMPRESS_GENERIC,
        ZL_GRAPH_COMPRESS_GENERIC
    };

    static ZL_Type inputType = ZL_Type_any;
    static ZL_FunctionGraphDesc const nToNDesc = {
        .name                = "test-n-to-n-functional",
        .graph_f             = testNToNRouting,
        .inputTypeMasks      = &inputType,
        .nbInputs            = 1,
        .lastInputIsVariable = true,
        .customGraphs        = successors,
        .nbCustomGraphs      = 3
    };

    ZL_GraphID n_to_n_graph =
            ZL_Compressor_registerFunctionGraph(compressor, &nToNDesc);
    ASSERT_TRUE(ZL_GraphID_isValid(n_to_n_graph));

    ZL_Report selectResult =
            ZL_Compressor_selectStartingGraphID(compressor, n_to_n_graph);
    ASSERT_FALSE(ZL_isError(selectResult));

    int data1[50], data2[50], data3[50];
    fillTestData(data1, 50, 0);
    fillTestData(data2, 50, 1000);
    fillTestData(data3, 50, 2000);

    ZL_TypedRef* input1 = ZL_TypedRef_createNumeric(data1, sizeof(int), 50);
    ZL_TypedRef* input2 = ZL_TypedRef_createNumeric(data2, sizeof(int), 50);
    ZL_TypedRef* input3 = ZL_TypedRef_createNumeric(data3, sizeof(int), 50);
    const ZL_TypedRef* inputs[3] = { input1, input2, input3 };

    size_t const totalBytes = 150 * sizeof(int);
    size_t const compressedBound = ZL_compressBound(totalBytes);
    std::vector<uint8_t> compressedData(compressedBound);

    ZL_CCtx* cctx = ZL_CCtx_create();
    ASSERT_NE(cctx, nullptr);

    ZL_Report versionResult =
            ZL_CCtx_setParameter(cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION);
    ASSERT_FALSE(ZL_isError(versionResult));

    ZL_Report refResult = ZL_CCtx_refCompressor(cctx, compressor);
    ASSERT_FALSE(ZL_isError(refResult));

    ZL_Report compressResult = ZL_CCtx_compressMultiTypedRef(
            cctx, compressedData.data(), compressedData.size(), inputs, 3);
    ASSERT_FALSE(ZL_isError(compressResult));

    size_t const compressedSize = ZL_validResult(compressResult);
    EXPECT_GT(compressedSize, 0);
    EXPECT_LE(compressedSize, compressedBound);

    ZL_TypedBuffer* outputs[3] = {
        ZL_TypedBuffer_create(),
        ZL_TypedBuffer_create(),
        ZL_TypedBuffer_create()
    };

    ZL_DCtx* dctx = ZL_DCtx_create();
    ASSERT_NE(dctx, nullptr);

    ZL_Report decompressResult = ZL_DCtx_decompressMultiTBuffer(
            dctx, outputs, 3, compressedData.data(), compressedSize);
    ASSERT_FALSE(ZL_isError(decompressResult));

    EXPECT_EQ(ZL_TypedBuffer_numElts(outputs[0]), 50);
    EXPECT_EQ(ZL_TypedBuffer_numElts(outputs[1]), 50);
    EXPECT_EQ(ZL_TypedBuffer_numElts(outputs[2]), 50);

    const int* dec1 = (const int*)ZL_TypedBuffer_rPtr(outputs[0]);
    const int* dec2 = (const int*)ZL_TypedBuffer_rPtr(outputs[1]);
    const int* dec3 = (const int*)ZL_TypedBuffer_rPtr(outputs[2]);

    EXPECT_EQ(memcmp(dec1, data1, 50 * sizeof(int)), 0);
    EXPECT_EQ(memcmp(dec2, data2, 50 * sizeof(int)), 0);
    EXPECT_EQ(memcmp(dec3, data3, 50 * sizeof(int)), 0);

    ZL_TypedRef_free(input1);
    ZL_TypedRef_free(input2);
    ZL_TypedRef_free(input3);
    for (int i = 0; i < 3; i++) {
        ZL_TypedBuffer_free(outputs[i]);
    }
    ZL_DCtx_free(dctx);
    ZL_CCtx_free(cctx);
    ZL_Compressor_free(compressor);
}

// Test error handling: mismatched input/graph counts should fail
TEST(NToNGraphTest, MismatchedCountsError)
{
    ZL_Compressor* compressor = ZL_Compressor_create();
    ASSERT_NE(compressor, nullptr);

    // Register with 2 successor graphs but will provide 3 inputs
    static const ZL_GraphID successors[2] = {
        ZL_GRAPH_STORE,
        ZL_GRAPH_COMPRESS_GENERIC
    };

    static ZL_Type inputType = ZL_Type_any;
    static ZL_FunctionGraphDesc const nToNDesc = {
        .name                = "test-n-to-n-mismatch",
        .graph_f             = testNToNRouting,
        .inputTypeMasks      = &inputType,
        .nbInputs            = 1,
        .lastInputIsVariable = true,
        .customGraphs        = successors,
        .nbCustomGraphs      = 2  // Only 2 graphs
    };

    ZL_GraphID n_to_n_graph =
            ZL_Compressor_registerFunctionGraph(compressor, &nToNDesc);
    ASSERT_TRUE(ZL_GraphID_isValid(n_to_n_graph));

    ZL_Report selectResult =
            ZL_Compressor_selectStartingGraphID(compressor, n_to_n_graph);
    ASSERT_FALSE(ZL_isError(selectResult));

    int data1[10], data2[10], data3[10];
    fillTestData(data1, 10, 0);
    fillTestData(data2, 10, 10);
    fillTestData(data3, 10, 20);

    ZL_TypedRef* input1 = ZL_TypedRef_createNumeric(data1, sizeof(int), 10);
    ZL_TypedRef* input2 = ZL_TypedRef_createNumeric(data2, sizeof(int), 10);
    ZL_TypedRef* input3 = ZL_TypedRef_createNumeric(data3, sizeof(int), 10);
    const ZL_TypedRef* inputs[3] = { input1, input2, input3 };  // 3 inputs!

    size_t const compressedBound = ZL_compressBound(30 * sizeof(int));
    std::vector<uint8_t> compressedData(compressedBound);

    ZL_CCtx* cctx = ZL_CCtx_create();
    ASSERT_NE(cctx, nullptr);

    ZL_Report versionResult =
            ZL_CCtx_setParameter(cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION);
    ASSERT_FALSE(ZL_isError(versionResult));

    ZL_Report refResult = ZL_CCtx_refCompressor(cctx, compressor);
    ASSERT_FALSE(ZL_isError(refResult));

    // Compression should fail: 3 inputs but only 2 graphs
    ZL_Report compressResult = ZL_CCtx_compressMultiTypedRef(
            cctx, compressedData.data(), compressedData.size(), inputs, 3);
    EXPECT_TRUE(ZL_isError(compressResult));
    EXPECT_EQ(ZL_errorCode(compressResult), ZL_ErrorCode_parameter_invalid);

    ZL_TypedRef_free(input1);
    ZL_TypedRef_free(input2);
    ZL_TypedRef_free(input3);
    ZL_CCtx_free(cctx);
    ZL_Compressor_free(compressor);
}

} // namespace
