// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <stdio.h> // printf

// OpenZL
#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/zl_compress.h" // ZL_CCtx_compress
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_errors.h"     // ZL_TRY_LET_T
#include "openzl/zl_graph_api.h"  // ZL_FunctionGraphDesc
#include "openzl/zl_input.h"
#include "openzl/zl_opaque_types.h"
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

/* ------   create the compressor   -------- */

// This graph function follows the ZL_GraphFn definition
// It's in charge of registering custom graphs and nodes
// and the one passed via unit-wide variable @g_dynGraph_dgdPtr.
static const ZL_FunctionGraphDesc* g_dynGraph_dgdPtr = nullptr;
static ZL_GraphID registerDynGraph(ZL_Compressor* cgraph) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, g_testVersion);
    if (ZL_isError(setr))
        abort();

    return ZL_Compressor_registerFunctionGraph(cgraph, g_dynGraph_dgdPtr);
}

/* ------   compress, using provided graph function   -------- */

static size_t compress(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_Type inputType,
        ZL_GraphFn graphf)
{
    assert(dstCapacity >= ZL_compressBound(srcSize));
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
    // Generate test input
    size_t const compressedBound = ZL_compressBound(inputSize);
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

static size_t permissiveTest(ZL_GraphFn graphf, const char* testName)
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
ZL_Report test_PrivateGraphFn(ZL_Graph* graph, const void* payload)
{
    PARSER_State ps = *(const PARSER_State*)payload;
    EXPECT_EQ(ps.chunkNb, g_chunkNb_current);

    (void)graph;
    ZL_RET_R_IF(GENERIC, 1); // unfinished

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
    .name           = "Simple Serial Segmenter",
    .segmenterFn    = serialSegmenterFn,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_serial },
    .numInputs      = 1,
};

TEST(Segmenter, serial)
{
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
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    g_segmenterDescPtr = &serialSegmenter;
    (void)roundTripGen(
            ZL_Type_serial, registerGraphAndSegmenter, "graph then segmenter");
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
    if (g_testVersion < ZL_CHUNK_VERSION_MIN)
        return;
    (void)cFailTest(
            registerInvalidGraph, "codec_before_segmenter (should fails)");
}

} // namespace
