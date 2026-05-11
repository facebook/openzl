// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <limits.h> // INT_MAX
#include <stdio.h>  // printf

// OpenZL
#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_segmenters.h"
#include "openzl/compress/segmenters/segmenter_numeric.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DecompressIntrospectionHooks.hpp"
#include "openzl/zl_compress.h" // ZL_CCtx_compress, ZL_MIN_CHUNK_SIZE
#include "openzl/zl_compressor.h"
#include "openzl/zl_config.h" // ZL_ALLOW_INTROSPECTION
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_errors.h"     // ZL_TRY_LET_T
#include "openzl/zl_graph_api.h"  // ZL_FunctionGraphDesc
#include "openzl/zl_input.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_portability.h"
#include "openzl/zl_segmenter.h"
#include "openzl/zl_selector.h"
#include "openzl/zl_version.h" // ZL_MIN_FORMAT_VERSION

namespace {

static const int g_testVersion = ZL_MAX_FORMAT_VERSION;

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

/* ------   compress, using provided compressor generator   -------- */

static size_t compress(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_Type inputType,
        ZL_GraphFn graphf)
{
    size_t const nbItems    = srcSize / 4;
    ZL_TypedRef* input      = NULL;
    uint32_t* stringLengths = NULL;
    switch (inputType) {
        case ZL_Type_serial:
            input = ZL_TypedRef_createSerial(src, srcSize);
            break;
        case ZL_Type_struct:
            EXPECT_TRUE(srcSize % 4 == 0);
            input = ZL_TypedRef_createStruct(src, 4, nbItems);
            break;
        case ZL_Type_numeric:
            EXPECT_TRUE(srcSize % 4 == 0);
            input = ZL_TypedRef_createNumeric(src, 4, nbItems);
            break;
        case ZL_Type_string:
            stringLengths = (uint32_t*)malloc(nbItems * sizeof(uint32_t));
            assert(stringLengths);
            for (size_t n = 0; n < nbItems - 1; n++)
                stringLengths[n] = 4; // fixed size strings, for the test
            stringLengths[nbItems - 1] =
                    (uint32_t)(srcSize - (nbItems - 1) * 4);
            input = ZL_TypedRef_createString(
                    src, srcSize, stringLengths, nbItems);
            break;
        default:
            EXPECT_FALSE(1) << "unsupported type";
            exit(1);
    }

    ZL_CCtx* const cctx = ZL_CCtx_create();
    assert(cctx);
    ZL_Compressor* const compressor = ZL_Compressor_create();
    assert(compressor);
    ZL_Report const gssr = ZL_Compressor_initUsingGraphFn(compressor, graphf);
    EXPECT_FALSE(ZL_isError(gssr)) << "cgraph initialization failed\n";
    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, compressor);
    EXPECT_FALSE(ZL_isError(rcgr)) << "CGraph reference failed\n";
    ZL_Report const r = ZL_CCtx_compressTypedRef(cctx, dst, dstCapacity, input);
    EXPECT_FALSE(ZL_isError(r)) << "compression failed \n";

    ZL_Compressor_free(compressor);
    ZL_CCtx_free(cctx);
    free(stringLengths);
    ZL_TypedRef_free(input);
    return ZL_validResult(r);
}

/* ------   decompress   -------- */

static size_t
decompress(void* dst, size_t dstCapacity, const void* compressed, size_t cSize)
{
    // Check buffer size
    ZL_Report const dr = ZL_getDecompressedSize(compressed, cSize);
    assert(!ZL_isError(dr));
    size_t const dstSize = ZL_validResult(dr);
    assert(dstCapacity >= dstSize);

    // Create a typed buffer for decompression
    ZL_TypedBuffer* const tbuf = ZL_TypedBuffer_create();
    assert(tbuf);

    // Create a single decompression state, to store the custom decoder(s)
    // The decompression state will be re-employed
    static ZL_DCtx* const dctx = ZL_DCtx_create();
    assert(dctx);

    // Decompress
    ZL_Report const rtb =
            ZL_DCtx_decompressTBuffer(dctx, tbuf, compressed, cSize);
    EXPECT_EQ(ZL_isError(rtb), 0) << "decompression failed \n";
    EXPECT_EQ(dstSize, ZL_validResult(rtb));

    // Transfer decompressed data to output buffer
    memcpy(dst, ZL_TypedBuffer_rPtr(tbuf), dstSize);

    ZL_TypedBuffer_free(tbuf);
    return dstSize;
}

/* ------   round trip test   ------ */

static size_t roundTripTest(
        ZL_GraphFn graphf,
        const void* input,
        size_t inputSize,
        ZL_Type inputType,
        const char* name)
{
    printf("\n=========================== \n");
    printf(" %s \n", name);
    printf("--------------------------- \n");
    // For string inputs, total stored size includes string lengths array.
    // Estimate numStrings from inputSize assuming 4-byte elements (as used
    // by roundTripGen).
    size_t totalInputSize = inputSize;
    if (inputType == ZL_Type_string) {
        size_t const nbItems = inputSize / 4;
        totalInputSize += nbItems * sizeof(uint32_t);
    }
    size_t const compressedBound = ZL_compressBound(totalInputSize);
    void* const compressed       = malloc(compressedBound);
    assert(compressed);

    size_t const compressedSize = compress(
            compressed, compressedBound, input, inputSize, inputType, graphf);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           inputSize,
           compressedSize);

    void* const decompressed = malloc(inputSize);
    assert(decompressed);

    size_t const decompressedSize =
            decompress(decompressed, inputSize, compressed, compressedSize);
    printf("decompressed %zu input bytes into %zu original bytes \n",
           compressedSize,
           decompressedSize);

    // round-trip check
    EXPECT_EQ((int)decompressedSize, (int)inputSize)
            << "Error : decompressed size != original size \n";
    if (inputSize) {
        EXPECT_EQ(memcmp(input, decompressed, inputSize), 0)
                << "Error : decompressed content differs from original (corruption issue) !!!  \n";
    }

    printf("round-trip success \n");
    free(decompressed);
    free(compressed);
    return compressedSize;
}

static size_t
roundTripGen(ZL_Type inputType, ZL_GraphFn graphf, const char* name)
{
    // Generate test input
#define NB_INTS 344
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return roundTripTest(graphf, input, sizeof(input), inputType, name);
}

/* this test is expected to fail predictably */
static int cFailTest(ZL_GraphFn graphf, const char* testName)
{
    printf("\n=========================== \n");
    printf(" %s \n", testName);
    printf("--------------------------- \n");
    // Generate test input => too short, will fail
    char input[40];
    for (int i = 0; i < 40; i++)
        input[i] = (char)i;

#define COMPRESSED_BOUND ZL_COMPRESSBOUND(sizeof(input))
    char compressed[COMPRESSED_BOUND] = { 0 };

    ZL_Report const r = ZL_compress_usingGraphFn(
            compressed, COMPRESSED_BOUND, input, sizeof(input), graphf);
    EXPECT_EQ(ZL_isError(r), 1) << "compression should have failed \n";

    printf("Compression failure observed as expected : %s \n",
           ZL_ErrorCode_toString(r._code));
    return 0;
}

static ZL_GraphID permissiveGraph(
        ZL_Compressor* cgraph,
        ZL_GraphFn failingGraph)
{
    assert(cgraph != nullptr);
    ZL_Report const spp = ZL_Compressor_setParameter(
            cgraph, ZL_CParam_permissiveCompression, 1);
    EXPECT_FALSE(ZL_isError(spp));
    return failingGraph(cgraph);
}

static ZL_GraphFn g_failingGraph_forPermissive;
static ZL_GraphID permissiveGraph_asGraphF(ZL_Compressor* cgraph) noexcept
{
    return permissiveGraph(cgraph, g_failingGraph_forPermissive);
}

ZL_UNUSED_ATTR static size_t permissiveTest(
        ZL_GraphFn graphf,
        const char* testName)
{
    printf("\n=========================== \n");
    printf(" Testing Permissive Mode \n");
    g_failingGraph_forPermissive = graphf;
    return roundTripGen(ZL_Type_serial, permissiveGraph_asGraphF, testName);
}

// ****************************************
// Generic capabilities for Segmenter tests
// ****************************************

// This compressor function follows the ZL_CompressorFn definition
// It's in charge of registering a custom segmenter
// passed via unit-wide variable @g_segmenterDescPtr.
static const ZL_SegmenterDesc* g_segmenterDescPtr = nullptr;
static ZL_GraphID registerSegmenter(ZL_Compressor* compressor) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, g_testVersion);
    if (ZL_isError(setr))
        abort();

    return ZL_Compressor_registerSegmenter(compressor, g_segmenterDescPtr);
}

#if 0
// Additional capabilities, to skip Graph registration.
// This makes it possible to pass a Chunk to a "Successor Graph",
// defined as a private function, never registered at Compressor level.
// When defined this way, the private Graph has no name and a Descriptor identical to its parent Segmenter,
// notably same Inputs conditions.
typedef ZL_Report (*ZL_GraphPrivateFn)(ZL_Graph* graph, const void* customPayload);
ZL_Report ZL_Segmenter_processChunk_withFunction(ZL_Segmenter* segCtx, const size_t numElts[], ZL_GraphPrivateFn gpf, const void* payload);

// For this to work, the Graph API must be augmented with the following methods:
size_t ZL_Graph_numInputs(const ZL_Graph* graph);
ZL_Edge* ZL_Graph_getEdge(const ZL_Graph* graph, size_t edgeID);

// Alternatively:
ZL_EdgeList ZL_Graph_getInputEdges(const ZL_Graph* graph);

// Another alternative, that would not need additional methods:
// ZL_Report (*ZL_GraphPrivateFn)(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs, const void* customPayload);

// Finally, we could keep the Graph Function prototype unchanged, aka:
// ZL_Report (*ZL_FunctionGraphFn)(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs);
// therefore without the `customPayload` argument,
// in which case, it's replaced by a method `const void* ZL_Graph_getCustomParameters()` for example,
// which itself could be a mere a wrapper on top of `ZL_Graph_getLocalRefParam(graph, ZL_STANDARD_PARAMID)`.
#endif

// ************************
// Simple Segmenter tests
// ************************

// Some dummy parser (just for tests)
using PARSER_State = struct PARSER_State_s;
PARSER_State* PARSER_create(void);
void PARSER_free(PARSER_State* ps);

using PARSER_Result = struct {
    size_t chunkSize;
    const void* parsingDetails; // details provided to following Graph stage
};
PARSER_Result PARSER_analyzeChunk(PARSER_State* ps, const ZL_Input* input);

// dummy parser implementation
static size_t g_chunkNb_current = 0;
struct PARSER_State_s {
    size_t chunkNb;
};
PARSER_State* PARSER_create(void)
{
    g_chunkNb_current = 0;
    return (PARSER_State*)calloc(1, sizeof(PARSER_State));
}
void PARSER_free(PARSER_State* ps)
{
    free(ps);
}

PARSER_Result PARSER_analyzeChunk(PARSER_State* ps, const ZL_Input* input)
{
    printf("PARSER_analyzeChunk (chunk nb %zu, input nbElts = %zu)\n",
           g_chunkNb_current,
           ZL_Input_numElts(input));
    assert(ps->chunkNb == g_chunkNb_current);
    ps->chunkNb++;
    g_chunkNb_current = ps->chunkNb;
    assert(input != NULL);
    size_t inSize = ZL_Input_contentSize(input);
#define CHUNKSIZE_DEFAULT 200
    // size_t chunkSize = inSize;
    size_t chunkSize =
            (inSize < CHUNKSIZE_DEFAULT) ? inSize : CHUNKSIZE_DEFAULT;
    PARSER_Result pr = {
        .chunkSize      = chunkSize,
        .parsingDetails = ps, // Only works in blocking mode (non-MT)
    };
    return pr;
}

/* Dummy Graph function, just for the exercise.
 * It's supposed to exploit the PARSER logic,
 * in this case it justs check that it received the expected value.
 * Input: Same as Segmenter ==> 1 Serial stream
 */
ZL_UNUSED_ATTR ZL_Report
test_PrivateGraphFn(ZL_Graph* graph, const void* payload)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    PARSER_State ps = *(const PARSER_State*)payload;
    EXPECT_EQ(ps.chunkNb, g_chunkNb_current);

    (void)graph;
    ZL_ERR_IF(1, GENERIC); // unfinished

    // assert(ZL_Graph_numInputs(graph) == 1);
    // ZL_Edge* input = ZL_Graph_getEdge(graph, 0);
    // return ZL_Edge_setDestination(input, ZL_GRAPH_COMPRESS_GENERIC);
}

/* Dummy Segmenter, just for the test
 * It's entirely driven by some external PARSER logic
 * Input: 1 stream of type
 */
ZL_Report trivialSegmenterFn_internal(
        ZL_Segmenter* sctx,
        ZL_Type type,
        size_t eltWidth,
        int incomplete)
{
    assert(ZL_Segmenter_numInputs(sctx) == 1);
    const ZL_Input* input = ZL_Segmenter_getInput(sctx, 0);
    assert(ZL_Input_type(input) == type);
    (void)type;

    PARSER_State* ps = PARSER_create();
    assert(ps != NULL);

    while (ZL_Input_numElts(input) > 0) {
        PARSER_Result parseR = PARSER_analyzeChunk(ps, input);
        EXPECT_TRUE(parseR.chunkSize > 0);
        EXPECT_TRUE(parseR.chunkSize % eltWidth == 0);
        size_t numElts = parseR.chunkSize / eltWidth;
        EXPECT_LE(numElts, ZL_Input_numElts(input));
        if (incomplete) {
            /* intentionally do not supply last chunk, thus resulting in
             * incomplete processing */
            if (numElts == ZL_Input_numElts(input))
                break;
        }
        ZL_Report processR = ZL_Segmenter_processChunk(
                sctx, &numElts, 1, ZL_GRAPH_COMPRESS_GENERIC, NULL);
        EXPECT_FALSE(ZL_isError(processR));
        if (ZL_isError(processR))
            return processR;
        // Update input: it now starts where previous chunk ended
        input = ZL_Segmenter_getInput(sctx, 0);
    }

    PARSER_free(ps);
    return ZL_returnSuccess();
}

ZL_Report trivialSegmenterFn(ZL_Segmenter* sctx, ZL_Type type, size_t eltWidth)
{
    return trivialSegmenterFn_internal(sctx, type, eltWidth, 0);
}

/* =======   Segmenter on serial input   ======== */

ZL_Report serialSegmenterFn(ZL_Segmenter* sctx)
{
    printf("serialSegmenterFn\n");
    return trivialSegmenterFn(sctx, ZL_Type_serial, 1);
}

static ZL_SegmenterDesc const serialSegmenter = {
    .name           = "!Simple Serial Segmenter",
    .segmenterFn    = serialSegmenterFn,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_serial },
    .numInputs      = 1,
};

TEST(Segmenter, serial)
{
    // Custom segmenter without customGraphs: no single-chunk fallback
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    g_segmenterDescPtr = &serialSegmenter;
    (void)roundTripGen(
            ZL_Type_serial, registerSegmenter, g_segmenterDescPtr->name);
}

/* =======   Segmenter on struct input   ======== */

ZL_Report structSegmenterFn(ZL_Segmenter* sctx)
{
    printf("structSegmenterFn\n");
    return trivialSegmenterFn(sctx, ZL_Type_struct, 4);
}

static ZL_SegmenterDesc const structSegmenter = {
    .name           = "Simple Struct Segmenter",
    .segmenterFn    = structSegmenterFn,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_struct },
    .numInputs      = 1,
};

TEST(Segmenter, struct)
{
    // Custom segmenter without customGraphs: no single-chunk fallback
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    g_segmenterDescPtr = &structSegmenter;
    (void)roundTripGen(
            ZL_Type_struct, registerSegmenter, g_segmenterDescPtr->name);
}

/* =======   Segmenter on numeric input   ======== */

ZL_Report numericSegmenterFn(ZL_Segmenter* sctx)
{
    printf("numericSegmenterFn\n");
    return trivialSegmenterFn(sctx, ZL_Type_numeric, 4);
}

static ZL_SegmenterDesc const numericSegmenter = {
    .name           = "Simple Numeric Segmenter",
    .segmenterFn    = numericSegmenterFn,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_numeric },
    .numInputs      = 1,
};

TEST(Segmenter, numeric)
{
    // Custom segmenter without customGraphs: no single-chunk fallback
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    g_segmenterDescPtr = &numericSegmenter;
    (void)roundTripGen(
            ZL_Type_numeric, registerSegmenter, g_segmenterDescPtr->name);
}

/* =======   Segmenter on string input   ======== */

ZL_Report stringSegmenterFn(ZL_Segmenter* sctx)
{
    printf("stringSegmenterFn\n");
    return trivialSegmenterFn(sctx, ZL_Type_string, 4);
}

static ZL_SegmenterDesc const stringSegmenter = {
    .name           = "Simple String Segmenter",
    .segmenterFn    = stringSegmenterFn,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_string },
    .numInputs      = 1,
};

TEST(Segmenter, string)
{
    // Custom segmenter without customGraphs: no single-chunk fallback
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    g_segmenterDescPtr = &stringSegmenter;
    (void)roundTripGen(
            ZL_Type_string, registerSegmenter, g_segmenterDescPtr->name);
}

/* =======   Segmenter after a Selector   ======== */

static ZL_GraphID justSelectFirst(
        const ZL_Selector* selectorAPI,
        const ZL_Input* input,
        const ZL_GraphID* gids,
        size_t nbGids) ZL_NOEXCEPT_FUNC_PTR
{
    printf("Selector 'justSelectFirst'\n");
    (void)selectorAPI;
    (void)input;
    assert(nbGids == 1);
    assert(gids != NULL);
    return gids[0];
}

static ZL_GraphID registerSelectorAndSegmenter(
        ZL_Compressor* compressor) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, g_testVersion);
    if (ZL_isError(setr))
        abort();

    ZL_GraphID const segid =
            ZL_Compressor_registerSegmenter(compressor, g_segmenterDescPtr);

    ZL_SelectorDesc const selectorDesc = {
        .selector_f     = justSelectFirst,
        .inStreamType   = ZL_Type_serial,
        .customGraphs   = &segid,
        .nbCustomGraphs = 1,
        .name           = "Selector justSelectFirst",
    };

    return ZL_Compressor_registerSelectorGraph(compressor, &selectorDesc);
}

TEST(Segmenter, selectorThenSegmenter)
{
    // Custom segmenter without customGraphs: no single-chunk fallback
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    g_segmenterDescPtr = &serialSegmenter;
    (void)roundTripGen(
            ZL_Type_serial,
            registerSelectorAndSegmenter,
            "selector then segmenter");
}

/* =======   Segmenter after a Function Graph that only selects   ======== */

static ZL_Report graphSelectFirst(
        ZL_Graph* graph,
        ZL_Edge* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    printf("Graph 'graphSelectFirst'\n");
    assert(nbInputs == 1);
    assert(inputs != NULL);
    const ZL_GraphIDList gids = ZL_Graph_getCustomGraphs(graph);
    assert(gids.nbGraphIDs >= 1);
    assert(gids.graphids != NULL);
    return ZL_Edge_setDestination(inputs[0], gids.graphids[0]);
}

static ZL_GraphID registerGraphAndSegmenter(ZL_Compressor* compressor) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, g_testVersion);
    if (ZL_isError(setr))
        abort();

    ZL_GraphID const segid =
            ZL_Compressor_registerSegmenter(compressor, g_segmenterDescPtr);

    const ZL_Type inType                 = ZL_Type_serial;
    ZL_FunctionGraphDesc const graphDesc = {
        .name           = "Graph justSelectFirst",
        .graph_f        = graphSelectFirst,
        .inputTypeMasks = &inType,
        .nbInputs       = 1,
        .customGraphs   = &segid,
        .nbCustomGraphs = 1,
    };

    return ZL_Compressor_registerFunctionGraph(compressor, &graphDesc);
}

TEST(Segmenter, graphThenSegmenter)
{
    // Custom segmenter without customGraphs: no single-chunk fallback
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    g_segmenterDescPtr = &serialSegmenter;
    (void)roundTripGen(
            ZL_Type_serial, registerGraphAndSegmenter, "graph then segmenter");
}

/* ======   Default segmenter   ======= */
ZL_GraphID default_numeric_segmenter(ZL_Compressor* compressor) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, g_testVersion);
    if (ZL_isError(setr))
        abort();

    return ZL_SEGMENT_NUMERIC;
}

TEST(Segmenter, default_numeric_segmenter)
{
    (void)roundTripGen(
            ZL_Type_numeric,
            default_numeric_segmenter,
            "use the default numeric segmenter");
}

/* =======   Single-chunk fallback for old format versions   ======= */

static ZL_GraphID numeric_segmenter_old_version(
        ZL_Compressor* compressor) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, ZL_CHUNK_VERSION_MIN - 1);
    if (ZL_isError(setr))
        abort();

    return ZL_SEGMENT_NUMERIC;
}

TEST(Segmenter, singleChunkFallback_numeric)
{
    (void)roundTripGen(
            ZL_Type_numeric,
            numeric_segmenter_old_version,
            "numeric segmenter with old format version (single-chunk fallback)");
}

/* =======   Runtime graph params survive old-format segmenters   ======= */

#define RUNTIME_PARAM_SEGMENTER_TEST_PID 912

static ZL_GraphID g_runtimeParamSuccessor =
        ZL_GRAPH_ILLEGAL; // NOLINT(facebook-avoid-non-const-global-variables)

static ZL_Report requireRuntimeParam(
        ZL_Graph* graph,
        ZL_Edge* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_ERR_IF_NE(nbInputs, 1, graph_invalidNumInputs);

    ZL_IntParam const param =
            ZL_Graph_getLocalIntParam(graph, RUNTIME_PARAM_SEGMENTER_TEST_PID);
    ZL_ERR_IF_NE(
            param.paramId,
            RUNTIME_PARAM_SEGMENTER_TEST_PID,
            graphParameter_invalid);
    ZL_ERR_IF_NE(param.paramValue, 7, graphParameter_invalid);

    return ZL_Edge_setDestination(inputs[0], ZL_GRAPH_STORE);
}

static ZL_Report runtimeParamSegmenterFn(ZL_Segmenter* sctx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    ZL_ERR_IF_NE(ZL_Segmenter_numInputs(sctx), 1, node_invalid_input);

    ZL_GraphIDList const customGraphs = ZL_Segmenter_getCustomGraphs(sctx);
    ZL_ERR_IF_NE(customGraphs.nbGraphIDs, 1, graphParameter_invalid);

    size_t const numElts    = ZL_Input_numElts(ZL_Segmenter_getInput(sctx, 0));
    ZL_IntParam const param = { RUNTIME_PARAM_SEGMENTER_TEST_PID, 7 };
    ZL_LocalParams const chunkParams    = { .intParams = { &param, 1 } };
    ZL_RuntimeGraphParameters const rgp = { .localParams = &chunkParams };

    return ZL_Segmenter_processChunk(
            sctx, &numElts, 1, customGraphs.graphids[0], &rgp);
}

static ZL_SegmenterDesc const runtimeParamSegmenter = {
    .name            = "Single Chunk Segmenter With Runtime Params",
    .segmenterFn     = runtimeParamSegmenterFn,
    .inputTypeMasks  = (const ZL_Type[]){ ZL_Type_serial },
    .numInputs       = 1,
    .customGraphs    = &g_runtimeParamSuccessor,
    .numCustomGraphs = 1,
};

static ZL_GraphID registerSegmenterWithRuntimeChunkParams(
        ZL_Compressor* compressor) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, ZL_CHUNK_VERSION_MIN - 1);
    if (ZL_isError(setr))
        abort();

    ZL_Type const inType                 = ZL_Type_serial;
    ZL_FunctionGraphDesc const graphDesc = {
        .name           = "Require runtime params",
        .graph_f        = requireRuntimeParam,
        .inputTypeMasks = &inType,
        .nbInputs       = 1,
    };

    g_runtimeParamSuccessor =
            ZL_Compressor_registerFunctionGraph(compressor, &graphDesc);
    return ZL_Compressor_registerSegmenter(compressor, &runtimeParamSegmenter);
}

TEST(Segmenter, singleChunkFallback_preservesRuntimeGraphParams)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerSegmenterWithRuntimeChunkParams,
            "single-chunk segmenter with runtime graph params on old format");
}

/* =======   Zero segments must fail (issue #253)   ======== */

ZL_Report zeroSegmentSegmenterFn(ZL_Segmenter* sctx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    const ZL_Input* const input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ERR_IF_NE(ZL_Input_numElts(input), 0, node_invalid_input);
    return ZL_returnSuccess();
}

static ZL_SegmenterDesc const zeroSegmentSegmenter = {
    .name           = "Zero Segment Segmenter",
    .segmenterFn    = zeroSegmentSegmenterFn,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_serial },
    .numInputs      = 1,
};

TEST(Segmenter, zeroSegments_mustFail)
{
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;

    size_t const compressedBound = ZL_compressBound(0);
    void* const compressed       = malloc(compressedBound);
    ASSERT_NE(compressed, nullptr);

    g_segmenterDescPtr = &zeroSegmentSegmenter;

    ZL_CCtx* const cctx = ZL_CCtx_create();
    ASSERT_NE(cctx, nullptr);
    ZL_Compressor* const compressor = ZL_Compressor_create();
    ASSERT_NE(compressor, nullptr);
    ZL_Report gssr =
            ZL_Compressor_initUsingGraphFn(compressor, registerSegmenter);
    ASSERT_FALSE(ZL_isError(gssr));
    ZL_Report rcgr = ZL_CCtx_refCompressor(cctx, compressor);
    ASSERT_FALSE(ZL_isError(rcgr));

    ZL_TypedRef* input = ZL_TypedRef_createSerial(NULL, 0);
    ASSERT_NE(input, nullptr);
    ZL_Report const r =
            ZL_CCtx_compressTypedRef(cctx, compressed, compressedBound, input);
    EXPECT_TRUE(ZL_isError(r))
            << "Compression must fail when segmenter produces zero segments";

    // The decompressor rejects frames with zero segments. We could change the
    // decompressor to accept them, in which case we'd need to validate that
    // round trips succeed here.

    ZL_TypedRef_free(input);
    ZL_Compressor_free(compressor);
    ZL_CCtx_free(cctx);
    free(compressed);
}

/* *********************************************** */
/* =======   Expected clean failure tests ======== */
/* *********************************************** */

/* =======   Did not consume all input   ======== */

ZL_Report failingIncompleteSerialSegmenterFn(ZL_Segmenter* sctx)
{
    printf("failingIncompleteSerialSegmenterFn\n");
    return trivialSegmenterFn_internal(sctx, ZL_Type_serial, 1, 1);
}

static ZL_SegmenterDesc const failingIncompleteSegmenter = {
    .name           = "Serial Segmenter that does not process all input",
    .segmenterFn    = failingIncompleteSerialSegmenterFn,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_serial },
    .numInputs      = 1,
};

TEST(Segmenter, input_incomplete)
{
    // Custom segmenter without customGraphs: no single-chunk fallback
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    g_segmenterDescPtr = &failingIncompleteSegmenter;
    (void)cFailTest(registerSegmenter, g_segmenterDescPtr->name);
}

/* =======   Codec precedes segmenter (must fail)  ======== */

ZL_GraphID registerInvalidGraph(ZL_Compressor* compressor) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, g_testVersion);
    if (ZL_isError(setr))
        abort();
    ZL_GraphID segid =
            ZL_Compressor_registerSegmenter(compressor, &serialSegmenter);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, ZL_NODE_CONVERT_SERIAL_TO_TOKEN4, segid);
}

TEST(Segmenter, codec_before_segmenter)
{
    // Uses serialSegmenter (no customGraphs): no single-chunk fallback
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    (void)cFailTest(
            registerInvalidGraph, "codec_before_segmenter (should fails)");
}

/* =======   Parameterized Segmenter with Local Params   ======== */

// Test that we can parameterize a segmenter with local parameters
// This is the fix for the bug where parameterizing a segmenter returns an
// illegal graph
ZL_GraphID registerParameterizedSegmenter(ZL_Compressor* compressor) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, g_testVersion);
    if (ZL_isError(setr))
        abort();

    // Register a base segmenter
    ZL_GraphID const baseSegid =
            ZL_Compressor_registerSegmenter(compressor, &serialSegmenter);

    // Verify the base segmenter is valid
    EXPECT_TRUE(ZL_GraphID_isValid(baseSegid))
            << "Base segmenter registration failed";

    // Parameterize it with local parameters
    ZL_IntParam intParams[1]         = { { .paramId = 1, .paramValue = 42 } };
    ZL_LocalIntParams localIntParams = {
        .intParams   = intParams,
        .nbIntParams = 1,
    };
    ZL_LocalParams localParams = {
        .intParams = localIntParams,
    };

    ZL_GraphParameters params = {
        .name        = "Parameterized Serial Segmenter",
        .localParams = &localParams,
    };

    ZL_RESULT_OF(ZL_GraphID)
    paramResult =
            ZL_Compressor_parameterizeGraph(compressor, baseSegid, &params);

    // The bug was that this would return an invalid graph (graph_invalid error)
    EXPECT_FALSE(ZL_RES_isError(paramResult))
            << "Parameterizing segmenter should succeed";
    if (ZL_RES_isError(paramResult)) {
        printf("Error code: %s\n", ZL_ErrorCode_toString(paramResult._code));
        abort();
    }

    ZL_GraphID paramSegid = ZL_RES_value(paramResult);
    EXPECT_TRUE(ZL_GraphID_isValid(paramSegid))
            << "Parameterized segmenter should be valid";

    return paramSegid;
}

TEST(Segmenter, parameterizedWithLocalParams)
{
    // Based on serialSegmenter (no customGraphs): no single-chunk fallback
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    (void)roundTripGen(
            ZL_Type_serial,
            registerParameterizedSegmenter,
            "parameterized segmenter with local params");
}

TEST(Segmenter, parameterizedSegmenterIsSerializable)
{
    openzl::Compressor compressor;
    registerParameterizedSegmenter(compressor.get());
    auto serialized = compressor.serialize();
    openzl::Compressor deserialized;
    // Register deps
    openzl::GraphParameters params = { .name = "test" };
    // Do registration in different order to ensure serialization works
    deserialized.parameterizeGraph(ZL_GRAPH_ZSTD, params);
    registerParameterizedSegmenter(deserialized.get());
    deserialized.deserialize(serialized);
}

/* =======   Segmenter on string input with variable-length chunks   ======== */

// Custom segmenter: splits input into two halves by element count
ZL_Report halfSplitStringSegmenterFn(ZL_Segmenter* sctx)
{
    assert(ZL_Segmenter_numInputs(sctx) == 1);
    const ZL_Input* input = ZL_Segmenter_getInput(sctx, 0);
    assert(ZL_Input_type(input) == ZL_Type_string);

    size_t const totalElts = ZL_Input_numElts(input);
    size_t firstHalf       = totalElts / 2;

    // Chunk 1: first half
    ZL_Report r = ZL_Segmenter_processChunk(
            sctx, &firstHalf, 1, ZL_GRAPH_COMPRESS_GENERIC, NULL);
    EXPECT_FALSE(ZL_isError(r));
    if (ZL_isError(r))
        return r;

    // Chunk 2: remaining
    input             = ZL_Segmenter_getInput(sctx, 0);
    size_t secondHalf = ZL_Input_numElts(input);
    r                 = ZL_Segmenter_processChunk(
            sctx, &secondHalf, 1, ZL_GRAPH_COMPRESS_GENERIC, NULL);
    EXPECT_FALSE(ZL_isError(r));
    if (ZL_isError(r))
        return r;

    return ZL_returnSuccess();
}

static ZL_SegmenterDesc const halfSplitStringSegmenter = {
    .name           = "Half-Split String Segmenter",
    .segmenterFn    = halfSplitStringSegmenterFn,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_string },
    .numInputs      = 1,
};

TEST(Segmenter, stringVariableLengthChunks)
{
    // Custom segmenter without customGraphs: no single-chunk fallback
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;

    // Create variable-length strings:
    // First 50 strings are 2 bytes each (100 bytes total)
    // Last 50 strings are 6 bytes each (300 bytes total)
    size_t const numStrings       = 100;
    size_t const firstHalf        = 50;
    size_t const shortLen         = 2;
    size_t const longLen          = 6;
    size_t const totalContentSize = firstHalf * shortLen + firstHalf * longLen;

    uint32_t* stringLengths = (uint32_t*)malloc(numStrings * sizeof(uint32_t));
    assert(stringLengths);
    for (size_t i = 0; i < firstHalf; i++)
        stringLengths[i] = shortLen;
    for (size_t i = firstHalf; i < numStrings; i++)
        stringLengths[i] = longLen;

    // Fill content with recognizable pattern
    unsigned char* content = (unsigned char*)malloc(totalContentSize);
    assert(content);
    for (size_t i = 0; i < totalContentSize; i++)
        content[i] = (unsigned char)(i & 0xFF);

    ZL_TypedRef* input = ZL_TypedRef_createString(
            content, totalContentSize, stringLengths, numStrings);
    assert(input);

    // Compress
    // For string inputs, total stored size includes string lengths array
    size_t const totalInputSize =
            totalContentSize + numStrings * sizeof(uint32_t);
    size_t const compressedBound = ZL_compressBound(totalInputSize);
    void* const compressed       = malloc(compressedBound);
    assert(compressed);

    g_segmenterDescPtr = &halfSplitStringSegmenter;

    ZL_CCtx* const cctx = ZL_CCtx_create();
    assert(cctx);
    ZL_Compressor* const compressor = ZL_Compressor_create();
    assert(compressor);
    ZL_Report gssr =
            ZL_Compressor_initUsingGraphFn(compressor, registerSegmenter);
    EXPECT_FALSE(ZL_isError(gssr)) << "cgraph initialization failed";
    ZL_Report rcgr = ZL_CCtx_refCompressor(cctx, compressor);
    EXPECT_FALSE(ZL_isError(rcgr)) << "CGraph reference failed";
    ZL_Report r =
            ZL_CCtx_compressTypedRef(cctx, compressed, compressedBound, input);
    EXPECT_FALSE(ZL_isError(r)) << "compression failed";
    size_t const compressedSize = ZL_validResult(r);

    ZL_Compressor_free(compressor);
    ZL_CCtx_free(cctx);

    // Decompress
    void* const decompressed = malloc(totalContentSize);
    assert(decompressed);
    size_t const decompressedSize = decompress(
            decompressed, totalContentSize, compressed, compressedSize);

    // Verify
    EXPECT_EQ(decompressedSize, totalContentSize);
    EXPECT_EQ(memcmp(content, decompressed, totalContentSize), 0)
            << "Decompressed content differs from original";

    free(decompressed);
    free(compressed);
    ZL_TypedRef_free(input);
    free(content);
    free(stringLengths);
}

/* =======   Single-chunk segmenter across all format versions   ======== */

ZL_Report singleChunkSegmenterFn(ZL_Segmenter* sctx)
{
    assert(ZL_Segmenter_numInputs(sctx) == 1);
    const ZL_Input* input = ZL_Segmenter_getInput(sctx, 0);
    assert(input != NULL);
    size_t numElts     = ZL_Input_numElts(input);
    ZL_Report processR = ZL_Segmenter_processChunk(
            sctx, &numElts, 1, ZL_GRAPH_COMPRESS_GENERIC, NULL);
    EXPECT_FALSE(ZL_isError(processR));
    return processR;
}

static ZL_SegmenterDesc const singleChunkSegmenter = {
    .name           = "Single Chunk Segmenter",
    .segmenterFn    = singleChunkSegmenterFn,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_serial },
    .numInputs      = 1,
};

static int g_singleChunkTestVersion = 0;
static ZL_GraphID registerSingleChunkSegmenter(
        ZL_Compressor* compressor) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, g_singleChunkTestVersion);
    if (ZL_isError(setr))
        abort();

    return ZL_Compressor_registerSegmenter(compressor, &singleChunkSegmenter);
}

TEST(Segmenter, singleChunkAllVersions)
{
    for (int version = ZL_MIN_FORMAT_VERSION; version <= ZL_MAX_FORMAT_VERSION;
         ++version) {
        g_singleChunkTestVersion = version;
        char name[64];
        snprintf(name, sizeof(name), "single chunk segmenter v%d", version);
        (void)roundTripGen(ZL_Type_serial, registerSingleChunkSegmenter, name);
    }
}

/* ============================================================ */
/* =======   Serial-numeric segmenter (numFromSerial)   ======= */
/* ============================================================ */

/* Helper: create a graph that converts serial->numeric and compresses */
static ZL_GraphID makeNumericGraph(ZL_Compressor* compressor, size_t bitWidth)
{
    ZL_NodeID const interpretNode = ZL_Node_interpretAsLE(bitWidth);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, interpretNode, ZL_GRAPH_FIELD_LZ);
}

/* --- Round-trip: single chunk (input < chunk size) --- */

static ZL_GraphID registerNumFromSerial_u32_singleChunk(
        ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeNumericGraph(compressor, 32);
    /* 16 MiB chunk, input is much smaller */
    return ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 4, 16 << 20, graph);
}

TEST(Segmenter, numFromSerial_u32_singleChunk)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerNumFromSerial_u32_singleChunk,
            "numFromSerial u32, single chunk");
}

/* --- Old format: collapse to one chunk even if chunk size would split --- */

static ZL_GraphID registerNumFromSerial_oldFormat(
        ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, ZL_CHUNK_VERSION_MIN - 1)))
        abort();
    ZL_GraphID graph = makeNumericGraph(compressor, 32);
    return ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 4, ZL_MIN_CHUNK_SIZE, graph);
}

TEST(Segmenter, numFromSerial_oldFormat_singleChunkFallback)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerNumFromSerial_oldFormat,
            "numFromSerial u32, old format single-chunk fallback");
}

/* --- Round-trip: u64 (smoke test) ---
 * roundTripGen feeds 1376-byte input; with chunkByteSize=ZL_MIN_CHUNK_SIZE
 * this is a single-chunk smoke test. Multi-chunk coverage lives in
 * numFromSerial_largeRoundTrip / chunkCount_* tests below. */

static ZL_GraphID registerNumFromSerial_u64(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeNumericGraph(compressor, 64);
    return ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 8, ZL_MIN_CHUNK_SIZE, graph);
}

TEST(Segmenter, numFromSerial_u64)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerNumFromSerial_u64,
            "numFromSerial u64, ZL_MIN_CHUNK_SIZE chunks");
}

/* --- Round-trip: u8 (element width 1, no alignment concern) --- */

static ZL_GraphID registerNumFromSerial_u8(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeNumericGraph(compressor, 8);
    return ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 1, ZL_MIN_CHUNK_SIZE, graph);
}

TEST(Segmenter, numFromSerial_u8)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerNumFromSerial_u8,
            "numFromSerial u8, ZL_MIN_CHUNK_SIZE chunks");
}

/* --- Round-trip: u16 --- */

static ZL_GraphID registerNumFromSerial_u16(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeNumericGraph(compressor, 16);
    return ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 2, ZL_MIN_CHUNK_SIZE, graph);
}

TEST(Segmenter, numFromSerial_u16)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerNumFromSerial_u16,
            "numFromSerial u16, ZL_MIN_CHUNK_SIZE chunks");
}

/* --- Round-trip with the smallest accepted chunk size --- */

static ZL_GraphID registerNumFromSerial_minChunk(
        ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeNumericGraph(compressor, 32);
    return ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 4, ZL_MIN_CHUNK_SIZE, graph);
}

TEST(Segmenter, numFromSerial_minChunkSize)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerNumFromSerial_minChunk,
            "numFromSerial u32, chunk == ZL_MIN_CHUNK_SIZE (smallest accepted)");
}

/* --- Invalid element width returns ZL_GRAPH_ILLEGAL --- */

TEST(Segmenter, numFromSerial_invalidWidth)
{
    ZL_Compressor* compressor = ZL_Compressor_create();
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeNumericGraph(compressor, 32);

    ZL_GraphID result = ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 3, 1024, graph);
    EXPECT_FALSE(ZL_GraphID_isValid(result))
            << "Element width 3 should be rejected";

    result = ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 0, 1024, graph);
    EXPECT_FALSE(ZL_GraphID_isValid(result))
            << "Element width 0 should be rejected";

    ZL_Compressor_free(compressor);
}

static ZL_GraphID registerCorruptedNumFromSerialWidth(
        ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();

    ZL_GraphID const graph        = makeNumericGraph(compressor, 32);
    ZL_IntParam const intParams[] = {
        { .paramId = SEGM_NUM_FROM_SERIAL_ELT_WIDTH_ID, .paramValue = 3 },
        { .paramId    = SEGM_NUM_FROM_SERIAL_CHUNK_BYTE_SIZE_ID,
          .paramValue = 512 },
    };
    ZL_LocalParams const localParams = {
        .intParams = { .intParams = intParams, .nbIntParams = 2 },
    };
    ZL_ParameterizedGraphDesc const desc = {
        .graph          = ZL_SEGMENT_NUM8_FROM_SERIAL,
        .customGraphs   = &graph,
        .nbCustomGraphs = 1,
        .localParams    = &localParams,
    };
    return ZL_Compressor_registerParameterizedGraph(compressor, &desc);
}

TEST(Segmenter, numFromSerial_corruptedSerializedGraph_invalidWidth)
{
    char const input[32] = { 0 };
    char compressed[ZL_COMPRESSBOUND(sizeof(input))];

    ZL_Report const compressionReport = ZL_compress_usingGraphFn(
            compressed,
            sizeof(compressed),
            input,
            sizeof(input),
            registerCorruptedNumFromSerialWidth);
    EXPECT_TRUE(ZL_isError(compressionReport));
    EXPECT_EQ(ZL_errorCode(compressionReport), ZL_ErrorCode_parameter_invalid);
}

/* --- Invalid chunk size --- */

TEST(Segmenter, numFromSerial_invalidChunkSize)
{
    ZL_Compressor* compressor = ZL_Compressor_create();
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeNumericGraph(compressor, 32);

    /* Chunk size 0 means "use default" (uniform across the build*Segmenter
     * family). */
    ZL_GraphID result =
            ZL_Compressor_buildNumFromSerialSegmenter(compressor, 4, 0, graph);
    EXPECT_TRUE(ZL_GraphID_isValid(result))
            << "Chunk size 0 should be accepted as 'use default'";

    /* Sub-ZL_MIN_CHUNK_SIZE positive values are rejected (would invalidate
     * ZL_compressBound). The previous "< eltByteWidth" cases (3, 7) are
     * subsumed by this stricter floor. */
    result = ZL_Compressor_buildNumFromSerialSegmenter(compressor, 4, 3, graph);
    EXPECT_FALSE(ZL_GraphID_isValid(result))
            << "Chunk size below ZL_MIN_CHUNK_SIZE should be rejected";

    result = ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 4, ZL_MIN_CHUNK_SIZE - 1, graph);
    EXPECT_FALSE(ZL_GraphID_isValid(result))
            << "Chunk size ZL_MIN_CHUNK_SIZE - 1 should be rejected";

    /* Chunk size > INT_MAX should be rejected (would truncate in ZL_IntParam)
     */
    result = ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 4, (size_t)INT_MAX + 1, graph);
    EXPECT_FALSE(ZL_GraphID_isValid(result))
            << "Chunk size > INT_MAX should be rejected";

    /* Chunk size == ZL_MIN_CHUNK_SIZE is the smallest accepted positive
     * value */
    result = ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 4, ZL_MIN_CHUNK_SIZE, graph);
    EXPECT_TRUE(ZL_GraphID_isValid(result))
            << "Chunk size == ZL_MIN_CHUNK_SIZE should succeed";

    ZL_Compressor_free(compressor);
}

/* --- chunkByteSize == 0 round-trips through builder default path --- */

static ZL_GraphID registerNumFromSerial_zeroChunkSize(
        ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeNumericGraph(compressor, 32);
    return ZL_Compressor_buildNumFromSerialSegmenter(compressor, 4, 0, graph);
}

TEST(Segmenter, numFromSerial_zeroChunkSizeUsesDefault)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerNumFromSerial_zeroChunkSize,
            "numFromSerial u32, chunkByteSize=0 substitutes default");
}

/* --- buildNumFromSerialSegmenter2: error reporting variant --- */

TEST(Segmenter, numFromSerial2_invalidWidth)
{
    ZL_Compressor* compressor = ZL_Compressor_create();
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeNumericGraph(compressor, 32);

    ZL_RESULT_OF(ZL_GraphID)
    res = ZL_Compressor_buildNumFromSerialSegmenter2(
            compressor, 3, 1024, graph);
    EXPECT_TRUE(ZL_RES_isError(res)) << "Element width 3 should be rejected";

    res = ZL_Compressor_buildNumFromSerialSegmenter2(
            compressor, 0, 1024, graph);
    EXPECT_TRUE(ZL_RES_isError(res)) << "Element width 0 should be rejected";

    ZL_Compressor_free(compressor);
}

static ZL_GraphID registerNumFromSerial2_u32(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeNumericGraph(compressor, 32);
    ZL_RESULT_OF(ZL_GraphID)
    res = ZL_Compressor_buildNumFromSerialSegmenter2(
            compressor, 4, 16 << 20, graph);
    if (ZL_RES_isError(res))
        abort();
    return ZL_RES_value(res);
}

TEST(Segmenter, numFromSerial2_u32_roundTrip)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerNumFromSerial2_u32,
            "numFromSerial2 u32, round trip");
}

/* --- Default successor (ZL_SEGMENTER_DEFAULT_SUCCESSOR) --- */

static ZL_GraphID registerNumFromSerial_defaultSuccessor(
        ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    return ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, 4, 16 << 20, ZL_SEGMENTER_DEFAULT_SUCCESSOR);
}

TEST(Segmenter, numFromSerial_defaultSuccessor)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerNumFromSerial_defaultSuccessor,
            "numFromSerial u32, default successor");
}

/* --- Large input with multiple widths --- */

static void numFromSerial_largeRoundTrip(size_t bitWidth)
{
    /* Input sized to span ~4 chunks at the minimum chunk size, so the
     * chunk loop iterates and emits multiple chunks across all widths. */
    size_t const eltByteWidth = bitWidth / 8;
    size_t const chunkSize    = ZL_MIN_CHUNK_SIZE;
    size_t const inputSize    = 4 * chunkSize;
    unsigned char* input      = (unsigned char*)malloc(inputSize);
    assert(input);
    for (size_t i = 0; i < inputSize; i++)
        input[i] = (unsigned char)(i & 0xFF);

    /* Set up compressor with serial-numeric segmenter */
    ZL_Compressor* compressor = ZL_Compressor_create();
    ZL_Report setr            = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, g_testVersion);
    ASSERT_FALSE(ZL_isError(setr));
    ZL_GraphID graph     = makeNumericGraph(compressor, bitWidth);
    ZL_GraphID segmenter = ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, eltByteWidth, chunkSize, graph);
    ASSERT_TRUE(ZL_GraphID_isValid(segmenter));
    ZL_Report selr = ZL_Compressor_selectStartingGraphID(compressor, segmenter);
    ASSERT_FALSE(ZL_isError(selr));

    /* Compress */
    ZL_CCtx* cctx  = ZL_CCtx_create();
    ZL_Report refr = ZL_CCtx_refCompressor(cctx, compressor);
    ASSERT_FALSE(ZL_isError(refr));
    size_t const compressedBound = ZL_compressBound(inputSize);
    void* compressed             = malloc(compressedBound);
    assert(compressed);
    ZL_TypedRef* typedInput = ZL_TypedRef_createSerial(input, inputSize);
    ZL_Report r             = ZL_CCtx_compressTypedRef(
            cctx, compressed, compressedBound, typedInput);
    ASSERT_FALSE(ZL_isError(r)) << "compression failed";
    size_t const compressedSize = ZL_validResult(r);

    /* Decompress */
    void* decompressed = malloc(inputSize);
    assert(decompressed);
    size_t const decompressedSize =
            decompress(decompressed, inputSize, compressed, compressedSize);

    /* Verify */
    EXPECT_EQ(decompressedSize, inputSize);
    EXPECT_EQ(memcmp(input, decompressed, inputSize), 0);

    free(decompressed);
    free(compressed);
    ZL_TypedRef_free(typedInput);
    ZL_CCtx_free(cctx);
    ZL_Compressor_free(compressor);
    free(input);
}

TEST(Segmenter, numFromSerial_large_u8)
{
    numFromSerial_largeRoundTrip(8);
}

TEST(Segmenter, numFromSerial_large_u16)
{
    numFromSerial_largeRoundTrip(16);
}

TEST(Segmenter, numFromSerial_large_u32)
{
    numFromSerial_largeRoundTrip(32);
}

TEST(Segmenter, numFromSerial_large_u64)
{
    numFromSerial_largeRoundTrip(64);
}

/* ============================================================ */
/* ===  Bare ZL_SEGMENT_NUM*_FROM_SERIAL macros (no param)  === */
/* ============================================================ */

/* These macros must work out of the box, without parameterization,
 * falling back to default chunk size and default successor graph. */

static ZL_GraphID bareNumFromSerial_u8(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    return ZL_SEGMENT_NUM8_FROM_SERIAL;
}

TEST(Segmenter, bareNumFromSerial_u8)
{
    (void)roundTripGen(
            ZL_Type_serial,
            bareNumFromSerial_u8,
            "bare ZL_SEGMENT_NUM8_FROM_SERIAL");
}

static ZL_GraphID bareNumFromSerial_u16(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    return ZL_SEGMENT_NUM16_FROM_SERIAL;
}

TEST(Segmenter, bareNumFromSerial_u16)
{
    (void)roundTripGen(
            ZL_Type_serial,
            bareNumFromSerial_u16,
            "bare ZL_SEGMENT_NUM16_FROM_SERIAL");
}

static ZL_GraphID bareNumFromSerial_u32(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    return ZL_SEGMENT_NUM32_FROM_SERIAL;
}

TEST(Segmenter, bareNumFromSerial_u32)
{
    (void)roundTripGen(
            ZL_Type_serial,
            bareNumFromSerial_u32,
            "bare ZL_SEGMENT_NUM32_FROM_SERIAL");
}

static ZL_GraphID bareNumFromSerial_u64(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    return ZL_SEGMENT_NUM64_FROM_SERIAL;
}

TEST(Segmenter, bareNumFromSerial_u64)
{
    (void)roundTripGen(
            ZL_Type_serial,
            bareNumFromSerial_u64,
            "bare ZL_SEGMENT_NUM64_FROM_SERIAL");
}

#if ZL_ALLOW_INTROSPECTION

/* --- Verify chunk count using introspection hooks --- */

class ChunkCounterHook : public openzl::CompressIntrospectionHooks {
   public:
    size_t chunkCount = 0;
    void on_ZL_Segmenter_processChunk_start(
            ZL_Segmenter*,
            const size_t[],
            size_t,
            ZL_GraphID,
            const ZL_RuntimeGraphParameters*) override
    {
        ++chunkCount;
    }
};

class DecompressChunkCounterHook : public openzl::DecompressIntrospectionHooks {
   public:
    size_t chunkCount = 0;
    void on_decompressChunk_start(ZL_DCtx*, size_t) override
    {
        ++chunkCount;
    }
};

static void verifyChunkCount(
        size_t bitWidth,
        size_t chunkByteSize,
        size_t inputSize,
        size_t expectedChunks)
{
    size_t const eltByteWidth = bitWidth / 8;

    /* Generate input data */
    unsigned char* input = (unsigned char*)malloc(inputSize);
    assert(input);
    for (size_t i = 0; i < inputSize; i++)
        input[i] = (unsigned char)(i & 0xFF);

    /* Build compressor */
    openzl::Compressor compressor;
    compressor.setParameter(openzl::CParam::FormatVersion, g_testVersion);
    ZL_GraphID graph     = makeNumericGraph(compressor.get(), bitWidth);
    ZL_GraphID segmenter = ZL_Compressor_buildNumFromSerialSegmenter(
            compressor.get(), eltByteWidth, chunkByteSize, graph);
    ASSERT_TRUE(ZL_GraphID_isValid(segmenter));
    compressor.selectStartingGraph(segmenter);

    /* Compress with chunk counter */
    ChunkCounterHook compressHook;
    openzl::CCtx cctx;
    cctx.refCompressor(compressor);
    ZL_Report attachr = ZL_CCtx_attachIntrospectionHooks(
            cctx.get(), compressHook.getRawHooks());
    ASSERT_FALSE(ZL_isError(attachr));
    auto compressed = cctx.compressSerial({ (const char*)input, inputSize });

    EXPECT_EQ(compressHook.chunkCount, expectedChunks)
            << "Compression: expected " << expectedChunks << " chunks for "
            << inputSize << " bytes with " << chunkByteSize << " byte chunks";

    /* Decompress with chunk counter */
    DecompressChunkCounterHook decompressHook;
    ZL_DCtx* rawDctx   = ZL_DCtx_create();
    ZL_Report dattachr = ZL_DCtx_attachDecompressIntrospectionHooks(
            rawDctx, decompressHook.getRawHooks());
    ASSERT_FALSE(ZL_isError(dattachr));
    ZL_TypedBuffer* tbuf = ZL_TypedBuffer_create();
    ZL_Report dr         = ZL_DCtx_decompressTBuffer(
            rawDctx, tbuf, compressed.data(), compressed.size());
    ASSERT_FALSE(ZL_isError(dr));

    EXPECT_EQ(decompressHook.chunkCount, expectedChunks)
            << "Decompression: expected " << expectedChunks << " chunks";

    /* Verify round-trip */
    size_t decompressedSize = ZL_validResult(dr);
    EXPECT_EQ(decompressedSize, inputSize);
    EXPECT_EQ(memcmp(input, ZL_TypedBuffer_rPtr(tbuf), inputSize), 0);

    ZL_TypedBuffer_free(tbuf);
    ZL_DCtx_free(rawDctx);
    free(input);
}

TEST(Segmenter, numFromSerial_chunkCount_singleChunk)
{
    // Chunk count verification requires actual chunking support
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    /* Half-chunk-size of u32 data → 1 chunk */
    verifyChunkCount(32, ZL_MIN_CHUNK_SIZE, ZL_MIN_CHUNK_SIZE / 2, 1);
}

TEST(Segmenter, numFromSerial_chunkCount_exactSplit)
{
    // Chunk count verification requires actual chunking support
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    /* 2x ZL_MIN_CHUNK_SIZE bytes of u32 data → exactly 2 chunks */
    verifyChunkCount(32, ZL_MIN_CHUNK_SIZE, 2 * ZL_MIN_CHUNK_SIZE, 2);
}

TEST(Segmenter, numFromSerial_chunkCount_unevenSplit)
{
    // Chunk count verification requires actual chunking support
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    /* 2x chunk + 100 bytes of u8 data → 3 chunks
     * (chunk + chunk + 100-byte remainder) */
    verifyChunkCount(8, ZL_MIN_CHUNK_SIZE, 2 * ZL_MIN_CHUNK_SIZE + 100, 3);
}

TEST(Segmenter, numFromSerial_chunkCount_alignedDown)
{
    // Chunk count verification requires actual chunking support
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    /* u64 width = 8. Pick a chunk one byte above ZL_MIN_CHUNK_SIZE so it
     * is NOT 8-aligned: chunk=ZL_MIN_CHUNK_SIZE+1=32769, aligned down to
     * (32769 - (32769 % 8)) = 32768. Input = 4*32768 + 128 = 131200
     * → ceil(131200 / 32768) = 5 chunks (32768*4 + 128). */
    verifyChunkCount(64, ZL_MIN_CHUNK_SIZE + 1, 4 * ZL_MIN_CHUNK_SIZE + 128, 5);
}

TEST(Segmenter, numFromSerial_chunkCount_u32_multiChunk)
{
    // Chunk count verification requires actual chunking support
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    /* chunk=ZL_MIN_CHUNK_SIZE (already 4-aligned). Input = 4*chunk + 96.
     * → 4 full + 96 remainder = 5 chunks. */
    verifyChunkCount(32, ZL_MIN_CHUNK_SIZE, 4 * ZL_MIN_CHUNK_SIZE + 96, 5);
}

TEST(Segmenter, numFromSerial_chunkCount_u32_unalignedChunk)
{
    // Chunk count verification requires actual chunking support
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    /* u32 width = 4. chunk=ZL_MIN_CHUNK_SIZE+1=32769 → aligned down to
     * 32768 (4-aligned). Input = 4*32768 + 96 = 131168 → 5 chunks. */
    verifyChunkCount(32, ZL_MIN_CHUNK_SIZE + 1, 4 * ZL_MIN_CHUNK_SIZE + 96, 5);
}

TEST(Segmenter, numFromSerial_chunkCount_manyChunks)
{
    // Chunk count verification requires actual chunking support
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    /* 16x ZL_MIN_CHUNK_SIZE bytes of u16 data → 16 chunks */
    verifyChunkCount(16, ZL_MIN_CHUNK_SIZE, 16 * ZL_MIN_CHUNK_SIZE, 16);
}

#endif // ZL_ALLOW_INTROSPECTION

/* ============================================================ */
/* =======   Pure serial segmenter (ZL_SEGMENT_SERIAL)   ====== */
/* ============================================================ */

/* Helper: a simple serial-input compression graph */
static ZL_GraphID makeSerialGraph(ZL_Compressor*)
{
    return ZL_GRAPH_COMPRESS_GENERIC;
}

/* --- Round-trip: single chunk (input < chunk size) --- */

static ZL_GraphID registerSerial_singleChunk(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    /* 16 MiB chunk, input is much smaller */
    return ZL_Compressor_buildSerialSegmenter(
            compressor, 16 << 20, makeSerialGraph(compressor));
}

TEST(Segmenter, serial_singleChunk)
{
    (void)roundTripGen(
            ZL_Type_serial, registerSerial_singleChunk, "serial, single chunk");
}

/* --- Round-trip with the smallest accepted chunk size ---
 * roundTripGen feeds 1376-byte input; with chunkByteSize=ZL_MIN_CHUNK_SIZE
 * this is a single-chunk smoke test. Multi-chunk coverage lives in the
 * chunkCount_* introspection tests below. */

static ZL_GraphID registerSerial_minChunk(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    return ZL_Compressor_buildSerialSegmenter(
            compressor, ZL_MIN_CHUNK_SIZE, makeSerialGraph(compressor));
}

TEST(Segmenter, serial_minChunkSize)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerSerial_minChunk,
            "serial, chunk == ZL_MIN_CHUNK_SIZE (smallest accepted)");
}

/* --- Old format: collapse to one chunk regardless of chunk size --- */

static ZL_GraphID registerSerial_oldFormat(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, ZL_CHUNK_VERSION_MIN - 1)))
        abort();
    return ZL_Compressor_buildSerialSegmenter(
            compressor, ZL_MIN_CHUNK_SIZE, makeSerialGraph(compressor));
}

TEST(Segmenter, serial_oldFormat_singleChunkFallback)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerSerial_oldFormat,
            "serial, old format single-chunk fallback");
}

/* --- Invalid chunk size --- */

TEST(Segmenter, serial_invalidChunkSize)
{
    ZL_Compressor* compressor = ZL_Compressor_create();
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeSerialGraph(compressor);

    /* Chunk size 0 means "use default" (matches SDDL2 + C++ wrapper) */
    ZL_GraphID result =
            ZL_Compressor_buildSerialSegmenter(compressor, 0, graph);
    EXPECT_TRUE(ZL_GraphID_isValid(result))
            << "Chunk size 0 should be accepted as 'use default'";

    /* Sub-ZL_MIN_CHUNK_SIZE positive values are rejected (would invalidate
     * ZL_compressBound). */
    result = ZL_Compressor_buildSerialSegmenter(compressor, 1, graph);
    EXPECT_FALSE(ZL_GraphID_isValid(result))
            << "Chunk size below ZL_MIN_CHUNK_SIZE should be rejected";

    result = ZL_Compressor_buildSerialSegmenter(
            compressor, ZL_MIN_CHUNK_SIZE - 1, graph);
    EXPECT_FALSE(ZL_GraphID_isValid(result))
            << "Chunk size ZL_MIN_CHUNK_SIZE - 1 should be rejected";

    /* Chunk size > INT_MAX should be rejected (would truncate in ZL_IntParam)
     */
    result = ZL_Compressor_buildSerialSegmenter(
            compressor, (size_t)INT_MAX + 1, graph);
    EXPECT_FALSE(ZL_GraphID_isValid(result))
            << "Chunk size > INT_MAX should be rejected";

    /* Chunk size == ZL_MIN_CHUNK_SIZE is the smallest accepted positive
     * value */
    result = ZL_Compressor_buildSerialSegmenter(
            compressor, ZL_MIN_CHUNK_SIZE, graph);
    EXPECT_TRUE(ZL_GraphID_isValid(result))
            << "Chunk size == ZL_MIN_CHUNK_SIZE should succeed";

    ZL_Compressor_free(compressor);
}

/* --- buildSerialSegmenter2: error reporting variant --- */

TEST(Segmenter, serial2_invalidChunkSize)
{
    ZL_Compressor* compressor = ZL_Compressor_create();
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_GraphID graph = makeSerialGraph(compressor);

    /* Chunk size 0 means "use default", not an error. */
    ZL_RESULT_OF(ZL_GraphID)
    res = ZL_Compressor_buildSerialSegmenter2(compressor, 0, graph);
    EXPECT_FALSE(ZL_RES_isError(res))
            << "Chunk size 0 should be accepted as 'use default'";

    res = ZL_Compressor_buildSerialSegmenter2(
            compressor, ZL_MIN_CHUNK_SIZE - 1, graph);
    EXPECT_TRUE(ZL_RES_isError(res))
            << "Chunk size below ZL_MIN_CHUNK_SIZE should be rejected";
    EXPECT_EQ(ZL_RES_code(res), ZL_ErrorCode_parameter_invalid);

    res = ZL_Compressor_buildSerialSegmenter2(
            compressor, (size_t)INT_MAX + 1, graph);
    EXPECT_TRUE(ZL_RES_isError(res))
            << "Chunk size > INT_MAX should be rejected";
    EXPECT_EQ(ZL_RES_code(res), ZL_ErrorCode_parameter_invalid);

    ZL_Compressor_free(compressor);
}

/* --- chunkByteSize == 0 round-trips through builder default path --- */

static ZL_GraphID registerSerial_zeroChunkSize(
        ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    return ZL_Compressor_buildSerialSegmenter(
            compressor, 0, makeSerialGraph(compressor));
}

TEST(Segmenter, serial_zeroChunkSizeUsesDefault)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerSerial_zeroChunkSize,
            "serial, chunkByteSize=0 substitutes default");
}

/* --- Corrupted serialized graph: negative chunk size param value --- */

static ZL_GraphID registerCorruptedSerial_negativeChunkSize(
        ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();

    ZL_GraphID const graph        = makeSerialGraph(compressor);
    ZL_IntParam const intParams[] = {
        { .paramId    = ZL_SEGMENT_SERIAL_CHUNK_BYTE_SIZE_PARAM,
          .paramValue = -1 },
    };
    ZL_LocalParams const localParams = {
        .intParams = { .intParams = intParams, .nbIntParams = 1 },
    };
    ZL_ParameterizedGraphDesc const desc = {
        .graph          = ZL_SEGMENT_SERIAL,
        .customGraphs   = &graph,
        .nbCustomGraphs = 1,
        .localParams    = &localParams,
    };
    return ZL_Compressor_registerParameterizedGraph(compressor, &desc);
}

TEST(Segmenter, serial_corruptedSerializedGraph_negativeChunkSize)
{
    char const input[32] = { 0 };
    char compressed[ZL_COMPRESSBOUND(sizeof(input))];

    ZL_Report const compressionReport = ZL_compress_usingGraphFn(
            compressed,
            sizeof(compressed),
            input,
            sizeof(input),
            registerCorruptedSerial_negativeChunkSize);
    EXPECT_TRUE(ZL_isError(compressionReport));
    EXPECT_EQ(ZL_errorCode(compressionReport), ZL_ErrorCode_parameter_invalid);
}

static ZL_GraphID registerSerial2(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    ZL_RESULT_OF(ZL_GraphID)
    res = ZL_Compressor_buildSerialSegmenter2(
            compressor, 16 << 20, makeSerialGraph(compressor));
    if (ZL_RES_isError(res))
        abort();
    return ZL_RES_value(res);
}

TEST(Segmenter, serial2_roundTrip)
{
    (void)roundTripGen(
            ZL_Type_serial, registerSerial2, "serial2, error-result variant");
}

/* --- Default successor (ZL_SEGMENTER_DEFAULT_SUCCESSOR) --- */

static ZL_GraphID registerSerial_defaultSuccessor(
        ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    return ZL_Compressor_buildSerialSegmenter(
            compressor, 16 << 20, ZL_SEGMENTER_DEFAULT_SUCCESSOR);
}

TEST(Segmenter, serial_defaultSuccessor)
{
    (void)roundTripGen(
            ZL_Type_serial,
            registerSerial_defaultSuccessor,
            "serial, default successor (ZL_GRAPH_COMPRESS_GENERIC)");
}

/* --- Bare ZL_SEGMENT_SERIAL: no parameterization, default chunk + successor */

static ZL_GraphID registerSerial_bare(ZL_Compressor* compressor) noexcept
{
    if (ZL_isError(ZL_Compressor_setParameter(
                compressor, ZL_CParam_formatVersion, g_testVersion)))
        abort();
    return ZL_SEGMENT_SERIAL;
}

TEST(Segmenter, serial_bare)
{
    (void)roundTripGen(
            ZL_Type_serial, registerSerial_bare, "bare ZL_SEGMENT_SERIAL");
}

/* --- Empty serial input round-trip ---
 * Empty input is fed through processChunk(size=0) on both format-version
 * paths in SEGM_serial. Both must produce valid compressed output that
 * decompresses to 0 bytes. Pinning down both paths so a future change to
 * either branch can't silently break empty-input handling. */

static void verifySerial_emptyInputRoundTrip(int formatVersion)
{
    openzl::Compressor compressor;
    compressor.setParameter(openzl::CParam::FormatVersion, formatVersion);
    ZL_GraphID const graph     = makeSerialGraph(compressor.get());
    ZL_GraphID const segmenter = ZL_Compressor_buildSerialSegmenter(
            compressor.get(), ZL_MIN_CHUNK_SIZE, graph);
    ASSERT_TRUE(ZL_GraphID_isValid(segmenter));
    compressor.selectStartingGraph(segmenter);

    openzl::CCtx cctx;
    cctx.refCompressor(compressor);
    auto compressed = cctx.compressSerial({});

    ZL_DCtx* const rawDctx     = ZL_DCtx_create();
    ZL_TypedBuffer* const tbuf = ZL_TypedBuffer_create();
    ZL_Report const dr         = ZL_DCtx_decompressTBuffer(
            rawDctx, tbuf, compressed.data(), compressed.size());
    EXPECT_FALSE(ZL_isError(dr));
    EXPECT_EQ(ZL_validResult(dr), 0u);

    ZL_TypedBuffer_free(tbuf);
    ZL_DCtx_free(rawDctx);
}

TEST(Segmenter, serial_emptyInput_newFormat)
{
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    verifySerial_emptyInputRoundTrip(g_testVersion);
}

TEST(Segmenter, serial_emptyInput_oldFormat)
{
    /* Exercises the formatVersion < ZL_CHUNK_VERSION_MIN branch in
     * SEGM_serial, which calls processChunk(size=0) directly without
     * entering the chunk loop. */
    verifySerial_emptyInputRoundTrip(ZL_CHUNK_VERSION_MIN - 1);
}

#if ZL_ALLOW_INTROSPECTION

/* --- Verify chunk count using introspection hooks --- */

static void verifySerialChunkCount(
        size_t chunkByteSize,
        size_t inputSize,
        size_t expectedChunks)
{
    /* Generate input data */
    unsigned char* input = (unsigned char*)malloc(inputSize);
    assert(input);
    for (size_t i = 0; i < inputSize; i++)
        input[i] = (unsigned char)(i & 0xFF);

    /* Build compressor */
    openzl::Compressor compressor;
    compressor.setParameter(openzl::CParam::FormatVersion, g_testVersion);
    ZL_GraphID graph     = makeSerialGraph(compressor.get());
    ZL_GraphID segmenter = ZL_Compressor_buildSerialSegmenter(
            compressor.get(), chunkByteSize, graph);
    ASSERT_TRUE(ZL_GraphID_isValid(segmenter));
    compressor.selectStartingGraph(segmenter);

    /* Compress with chunk counter */
    ChunkCounterHook compressHook;
    openzl::CCtx cctx;
    cctx.refCompressor(compressor);
    ZL_Report attachr = ZL_CCtx_attachIntrospectionHooks(
            cctx.get(), compressHook.getRawHooks());
    ASSERT_FALSE(ZL_isError(attachr));
    auto compressed = cctx.compressSerial({ (const char*)input, inputSize });

    EXPECT_EQ(compressHook.chunkCount, expectedChunks)
            << "Compression: expected " << expectedChunks << " chunks for "
            << inputSize << " bytes with " << chunkByteSize << " byte chunks";

    /* Decompress with chunk counter */
    DecompressChunkCounterHook decompressHook;
    ZL_DCtx* rawDctx   = ZL_DCtx_create();
    ZL_Report dattachr = ZL_DCtx_attachDecompressIntrospectionHooks(
            rawDctx, decompressHook.getRawHooks());
    ASSERT_FALSE(ZL_isError(dattachr));
    ZL_TypedBuffer* tbuf = ZL_TypedBuffer_create();
    ZL_Report dr         = ZL_DCtx_decompressTBuffer(
            rawDctx, tbuf, compressed.data(), compressed.size());
    ASSERT_FALSE(ZL_isError(dr));

    EXPECT_EQ(decompressHook.chunkCount, expectedChunks)
            << "Decompression: expected " << expectedChunks << " chunks";

    /* Verify round-trip */
    size_t decompressedSize = ZL_validResult(dr);
    EXPECT_EQ(decompressedSize, inputSize);
    EXPECT_EQ(memcmp(input, ZL_TypedBuffer_rPtr(tbuf), inputSize), 0);

    ZL_TypedBuffer_free(tbuf);
    ZL_DCtx_free(rawDctx);
    free(input);
}

TEST(Segmenter, serial_chunkCount_singleChunk)
{
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    /* Half-chunk-size of input → 1 chunk */
    verifySerialChunkCount(ZL_MIN_CHUNK_SIZE, ZL_MIN_CHUNK_SIZE / 2, 1);
}

TEST(Segmenter, serial_chunkCount_exactSplit)
{
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    /* 2x ZL_MIN_CHUNK_SIZE bytes → exactly 2 chunks */
    verifySerialChunkCount(ZL_MIN_CHUNK_SIZE, 2 * ZL_MIN_CHUNK_SIZE, 2);
}

TEST(Segmenter, serial_chunkCount_unevenSplit)
{
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    /* 2x chunk + 100 bytes → 3 chunks (chunk + chunk + 100-byte remainder) */
    verifySerialChunkCount(ZL_MIN_CHUNK_SIZE, 2 * ZL_MIN_CHUNK_SIZE + 100, 3);
}

TEST(Segmenter, serial_chunkCount_manyChunks)
{
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    /* 16x ZL_MIN_CHUNK_SIZE bytes → 16 chunks */
    verifySerialChunkCount(ZL_MIN_CHUNK_SIZE, 16 * ZL_MIN_CHUNK_SIZE, 16);
}

#endif // ZL_ALLOW_INTROSPECTION

} // namespace
