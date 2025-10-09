// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// Test sending and receiving transform's out-of-band parameters

// standard C
#include <stdio.h>  // printf
#include <stdlib.h> // exit, rand
#include <string.h> // memcpy

// Zstrong
#include "openzl/common/debug.h"           // ZL_REQUIRE
#include "openzl/compress/private_nodes.h" // ZL_GRAPH_SELECT_BYTE_ENTROPY
#include "openzl/zl_common_types.h" // ZL_TernaryParam_enable, ZL_TernaryParam_disable
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_registerStaticGraph_fromPipelineNodes1o
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_graph_api.h"
#include "openzl/zl_localParams.h"

namespace {

#if 0 // for debug only
void printHexa(const void* p, size_t size)
{
    const unsigned char* const b = (const unsigned char*)p;
    for (size_t n = 0; n < size; n++) {
        printf(" %02X ", b[n]);
    }
    printf("\n");
}
#endif

/* ------   create a custom nodes   -------- */

static size_t always_fail(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize) noexcept
{
    printf("running always_fail fake transform\n");
    (void)dst;
    (void)dstCapacity;
    (void)src;
    (void)srcSize;
    /* this fake custom node always fails.
     * the goal is to trigger backup compression mode.
     **/
    return (size_t)(-1);
}

static ZL_PipeEncoderDesc const fail_CDesc = {
    .CTid        = 1, /* no matter, this transform always fails anyway */
    .transform_f = always_fail,
};

/* ------   create custom graph   -------- */

static ZL_GraphID storeGraph(ZL_Compressor* compressor) noexcept
{
    printf("running storeGraph() \n");
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_GRAPH_STORE;
}

static ZL_GraphID entropySelectorGraph(ZL_Compressor* compressor) noexcept
{
    printf("running entropySelectorGraph() \n");
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_GRAPH_ENTROPY;
}

static ZL_GraphID genericLZGraph(ZL_Compressor* compressor) noexcept
{
    printf("running genericLZGraph() \n");
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_GRAPH_SELECT_GENERIC_LZ;
}

static ZL_GraphID triggerBackupGraph(ZL_Compressor* compressor) noexcept
{
    printf("running triggerBackupGraph() \n");
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            compressor,
            ZL_CParam_permissiveCompression,
            ZL_TernaryParam_enable));
    ZL_NodeID const node_fail =
            ZL_Compressor_registerPipeEncoder(compressor, &fail_CDesc);
    /* The following pipeline shall fail, since the first node will fail.
     * The only way processing doesn't just fail is
     * if the faulty node gets replaced on the fly by a backup generic graph.
     **/
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, node_fail, ZL_GRAPH_STORE);
}

ZL_GraphID graphs[3];
static ZL_GraphID delta8Graph(ZL_Compressor* compressor) noexcept
{
    printf("running delta8Graph() \n");
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    ZL_GraphID const returnToSerial =
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    compressor, ZL_NODE_CONVERT_NUM_TO_SERIAL, ZL_GRAPH_STORE);
    ZL_GraphID const delta8 = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, ZL_NODE_DELTA_INT, returnToSerial);
    ZL_GraphID const ontoInt = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, ZL_NODE_INTERPRET_AS_LE8, delta8);
    graphs[0] = returnToSerial;
    graphs[1] = delta8;
    graphs[2] = ontoInt;
    return ontoInt;
}

/* ------   compress, using provided graph function   -------- */

static size_t compress(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_GraphFn graphf)
{
    ZL_REQUIRE_GE(dstCapacity, ZL_compressBound(srcSize));

    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);
    ZL_Compressor* const compressor = ZL_Compressor_create();
    ZL_Report const initCompressor =
            ZL_Compressor_initUsingGraphFn(compressor, graphf);
    EXPECT_EQ(ZL_isError(initCompressor), 0)
            << "Compressor initialization failed\n";
    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, compressor);
    EXPECT_EQ(ZL_isError(rcgr), 0) << "compressor reference failed\n";
    ZL_Report const r = ZL_CCtx_compress(cctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";

    ZL_Compressor_free(compressor);
    ZL_CCtx_free(cctx);
    return ZL_validResult(r);
}

static size_t compress_explicitStart(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_GraphFn graphf,
        const ZL_GraphID* providedStartGraphid,
        const ZL_RuntimeGraphParameters* rgp)
{
    printf("running compress_explicitStart \n");
    ZL_REQUIRE_GE(dstCapacity, ZL_compressBound(srcSize));
    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);

    ZL_Compressor* const compressor = ZL_Compressor_create();
    ZL_REQUIRE_NN(compressor);

    ZL_GraphID defaultStart = graphf(compressor);
    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_selectStartingGraphID(compressor, defaultStart));
    ZL_GraphID startgid =
            providedStartGraphid ? *providedStartGraphid : defaultStart;
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(
            cctx, compressor)); // note: also erases previous advanced starting
                                // parameters
    // testing: compressor == NULL
    ZL_REQUIRE_SUCCESS(
            ZL_CCtx_selectStartingGraphID(cctx, NULL, startgid, rgp));

    ZL_Report const r = ZL_CCtx_compress(cctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";

    ZL_Compressor_free(compressor);
    ZL_CCtx_free(cctx);

    return ZL_validResult(r);
}

/* ------ define custom decoder transforms ------- */

/* none needed */

/* ------   decompress   -------- */

static size_t
decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    // Check buffer size
    ZL_Report const dr = ZL_getDecompressedSize(src, srcSize);
    ZL_REQUIRE(!ZL_isError(dr));
    size_t const dstSize = ZL_validResult(dr);
    ZL_REQUIRE_GE(dstCapacity, dstSize);

    // Create a single decompression state, to store the custom decoder(s)
    // The decompression state will be re-employed
    static ZL_DCtx* const dctx = ZL_DCtx_create();
    ZL_REQUIRE_NN(dctx);

    // Decompress
    ZL_Report const r =
            ZL_DCtx_decompress(dctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "decompression failed \n";

    return ZL_validResult(r);
}

/* ------   round trip test   ------ */

static int roundTripTest(
        ZL_GraphFn graphf,
        const void* input,
        size_t inputSize,
        const char* name)
{
    printf("\n=========================== \n");
    printf(" %s \n", name);
    printf("--------------------------- \n");
    // Generate test input
    size_t const compressedBound = ZL_compressBound(inputSize);
    void* const compressed       = malloc(compressedBound);
    ZL_REQUIRE_NN(compressed);

    size_t const compressedSize =
            compress(compressed, compressedBound, input, inputSize, graphf);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           inputSize,
           compressedSize);

    void* const decompressed = malloc(inputSize);
    ZL_REQUIRE_NN(decompressed);

    size_t const decompressedSize =
            decompress(decompressed, inputSize, compressed, compressedSize);
    printf("decompressed %zu input bytes into %zu original bytes \n",
           compressedSize,
           decompressedSize);

    // round-trip check
    EXPECT_EQ(decompressedSize, inputSize)
            << "Error : decompressed size != original size \n";
    if (inputSize) {
        EXPECT_EQ(memcmp(input, decompressed, inputSize), 0)
                << "Error : decompressed content differs from original (corruption issue) !!!  \n";
    }

    printf("round-trip success \n");
    free(decompressed);
    free(compressed);
    return 0;
}

static int roundTripTest_explicitStart(
        ZL_GraphFn graphf,
        const ZL_GraphID* startgid,
        const ZL_RuntimeGraphParameters* rgp,
        const void* input,
        size_t inputSize,
        const char* name)
{
    printf("\n=========================== \n");
    printf(" %s \n", name);
    printf("--------------------------- \n");
    // Generate test input
    size_t const compressedBound = ZL_compressBound(inputSize);
    void* const compressed       = malloc(compressedBound);
    ZL_REQUIRE_NN(compressed);

    size_t const compressedSize = compress_explicitStart(
            compressed,
            compressedBound,
            input,
            inputSize,
            graphf,
            startgid,
            rgp);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           inputSize,
           compressedSize);

    void* const decompressed = malloc(inputSize);
    ZL_REQUIRE_NN(decompressed);

    size_t const decompressedSize =
            decompress(decompressed, inputSize, compressed, compressedSize);
    printf("decompressed %zu input bytes into %zu original bytes \n",
           compressedSize,
           decompressedSize);

    // round-trip check
    EXPECT_EQ(decompressedSize, inputSize)
            << "Error : decompressed size != original size \n";
    if (inputSize) {
        EXPECT_EQ(memcmp(input, decompressed, inputSize), 0)
                << "Error : decompressed content differs from original (corruption issue) !!!  \n";
    }

    printf("round-trip success \n");
    free(decompressed);
    free(compressed);
    return 0;
}

static int roundTripIntegers(ZL_GraphFn graphf, const char* name)
{
    // Generate test input
#define NB_INTS 78
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return roundTripTest(graphf, input, sizeof(input), name);
}

static int roundTripIntegers_explicitStart(
        ZL_GraphFn graphf,
        const ZL_GraphID* startgid,
        const ZL_RuntimeGraphParameters* rgp,
        const char* name)
{
    // Generate test input
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return roundTripTest_explicitStart(
            graphf, startgid, rgp, input, sizeof(input), name);
}

int nullInputTest(void)
{
    return roundTripTest(storeGraph, nullptr, 0, "Null input scenario");
}

int storeGraphTest(void)
{
    return roundTripIntegers(storeGraph, "Trivial no-op store graph");
}

int entropySelectorGraphTest(void)
{
    return roundTripIntegers(
            entropySelectorGraph,
            "Call entropy graph, a standard graph starting with a selector");
}

int genericLZGraphTest(void)
{
    return roundTripIntegers(
            genericLZGraph,
            "Call generic LZ graph, a standard graph starting with a selector");
}

int triggerBackupGraphTest(void)
{
    return roundTripIntegers(
            triggerBackupGraph,
            "Call faulty graph, triggering backup compression");
}

int smallPipelineTest(void)
{
    return roundTripIntegers(delta8Graph, "Trivial pipeline graph");
}

int explicitStandardStartTest(void)
{
    ZL_GraphID const standardStart = ZL_GRAPH_ZSTD;
    return roundTripIntegers_explicitStart(
            delta8Graph, &standardStart, NULL, "Explicit standard Graph start");
}

int explicitCustomStartTest(void)
{
    return roundTripIntegers_explicitStart(
            delta8Graph, &graphs[2], NULL, "Explicit custom Graph start");
}

TEST(SimpleGraphs, nullInput)
{
    nullInputTest();
}
TEST(SimpleGraphs, storeGraph)
{
    storeGraphTest();
}
TEST(SimpleGraphs, entropySelector_as_standardGraph)
{
    entropySelectorGraphTest();
}
TEST(SimpleGraphs, genericLZbackend)
{
    genericLZGraphTest();
}
TEST(SimpleGraphs, triggerBackup)
{
    triggerBackupGraphTest();
}
TEST(SimpleGraphs, smallPipeline)
{
    smallPipelineTest();
}
TEST(SimpleGraphs, explicitStandardGraph)
{
    explicitStandardStartTest();
}
TEST(SimpleGraphs, explicitCustomGraph)
{
    explicitCustomStartTest();
}

// =================================================
// Test Parameterized Starting Graph

static int g_intParamTest = 0;
#define TEST_INT_PARAM_ID 766
static ZL_Report
printParamGraph(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs) noexcept
{
    assert(nbInputs == 1);
    const ZL_IntParam ip = ZL_Graph_getLocalIntParam(graph, TEST_INT_PARAM_ID);
    if (ip.paramId == TEST_INT_PARAM_ID) {
        printf("one parameter provided, of value %i \n", ip.paramValue);
        g_intParamTest = ip.paramValue;
    }
    // send input to successor (which must be a Graph)
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(inputs[0], ZL_GRAPH_ZSTD));
    return ZL_returnSuccess();
}

static ZL_Type serialInputType                   = ZL_Type_serial;
static ZL_FunctionGraphDesc const printParam_dgd = {
    .name                = "display int param if present",
    .graph_f             = printParamGraph,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

static ZL_GraphID recordFunctionGraph(ZL_Compressor* compressor) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_Compressor_registerFunctionGraph(compressor, &printParam_dgd);
}

int parameterizedStartingGraphTest(int value)
{
    ZL_IntParam const ip        = { .paramId    = TEST_INT_PARAM_ID,
                                    .paramValue = value };
    ZL_LocalIntParams const ips = { .intParams = &ip, .nbIntParams = 1 };
    ZL_LocalParams const lp     = { .intParams = ips };
    ZL_RuntimeGraphParameters const rgp     = { .localParams = &lp };
    const ZL_RuntimeGraphParameters* rgpptr = value ? &rgp : NULL;
    if (rgpptr) {
        printf("intParam1: value = %i \n",
               rgpptr->localParams->intParams.intParams->paramValue);
    }
    return roundTripIntegers_explicitStart(
            recordFunctionGraph, NULL, rgpptr, "display int param if present");
}

TEST(SimpleGraphs, parameterizedStartingGraph_none)
{
    g_intParamTest = -2;
    parameterizedStartingGraphTest(0); // actually means "no param"
    EXPECT_EQ(g_intParamTest, -2);
}

TEST(SimpleGraphs, parameterizedStartingGraph_39)
{
    g_intParamTest = -1;
    parameterizedStartingGraphTest(39);
    EXPECT_EQ(g_intParamTest, 39);
}

TEST(SimpleGraphs, parameterizedStartingGraph_73)
{
    g_intParamTest = -1;
    parameterizedStartingGraphTest(73);
    EXPECT_EQ(g_intParamTest, 73);
}

} // namespace
