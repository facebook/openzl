// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <climits>
#include <cstdint> // uint_X,
#include <cstdio>  // printf
#include <cstring> // memcpy

// Zstrong
#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_*, ZL_Compressor_registerStaticGraph_fromPipelineNodes1o
#include "openzl/zl_ctransform.h" // ZL_TypedEncoderDesc
#include "openzl/zl_data.h"       // ZL_Data_*
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_dtransform.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_opaque_types.h"

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

/* ------   create custom transforms   -------- */

#define CT_SWAP_LASTFIRST_ID 2
#define CT_SWAP_LASTFIRST_V2_ID 3
#define CT_STRING_JUSTFAIL_ID 4

// requires nbElts > 1.
void swap_lastfirst_raw(
        uint32_t* dstStringLens,
        size_t nbStrings,
        void* dstContent,
        size_t contentSize,
        const uint32_t* srcStringLens,
        const void* srcContent)
{
    assert(nbStrings > 1);
    uint32_t const size1 = srcStringLens[0];
    uint32_t const size3 = srcStringLens[nbStrings - 1];
    size_t const pos3    = contentSize - size3;
    size_t const size2   = contentSize - size1 - size3;

    memcpy(dstContent, ((const char*)srcContent) + pos3, size3);
    memcpy(((char*)dstContent) + size3,
           ((const char*)srcContent) + size1,
           size2);
    memcpy(((char*)dstContent) + contentSize - size1, srcContent, size1);

    memcpy(dstStringLens, srcStringLens, nbStrings * sizeof(uint32_t));
    dstStringLens[0]             = size3;
    dstStringLens[nbStrings - 1] = size1;
}

// Trivial custom transform for String stream :
// swap last element with the first one.
static ZL_Report swap_lastfirst(ZL_Encoder* eictx, const ZL_Input* in) noexcept
{
    assert(in != nullptr);
    size_t const nbElts = ZL_Input_numElts(in);
    printf("swap_lastfirst transform (nb elts = %zu)\n", nbElts);
    assert(ZL_Input_type(in) == ZL_Type_string);
    assert(eictx != nullptr);
    size_t const sumStringLens = ZL_Input_contentSize(in);

    ZL_Output* const outStream =
            ZL_Encoder_createTypedStream(eictx, 0, sumStringLens, 1);
    ZL_RET_R_IF_NULL(allocation, outStream); // control allocation success

    uint32_t* const outStringLens =
            ZL_Output_reserveStringLens(outStream, nbElts);
    assert(outStringLens != nullptr);

    swap_lastfirst_raw(
            outStringLens,
            nbElts,
            ZL_Output_ptr(outStream),
            sumStringLens,
            ZL_Input_stringLens(in),
            ZL_Input_ptr(in));

    ZL_RET_R_IF_ERR(ZL_Output_commit(outStream, nbElts));

    return ZL_returnValue(1); // nb Out Streams
}

// Prefer a #define, to be used as initializer in static const declarations
#define SWAP_LASTFIRST_GDESC                                          \
    (ZL_TypedGraphDesc)                                               \
    {                                                                 \
        .CTid = CT_SWAP_LASTFIRST_ID, .inStreamType = ZL_Type_string, \
        .outStreamTypes = (const ZL_Type[]){ ZL_Type_string },        \
        .nbOutStreams   = 1                                           \
    }
static ZL_TypedEncoderDesc const swap_lastfirst_CDesc = {
    .gd          = SWAP_LASTFIRST_GDESC,
    .transform_f = swap_lastfirst,
};

// Same custom transform, using the new StringType API
static ZL_Report swap_lastfirst_v2(
        ZL_Encoder* eictx,
        const ZL_Input* in) noexcept
{
    assert(in != nullptr);
    size_t const nbStrings = ZL_Input_numElts(in);
    printf("swap_lastfirst_v2 transform (nb strings = %zu)\n", nbStrings);
    assert(ZL_Input_type(in) == ZL_Type_string);
    assert(eictx != nullptr);
    size_t const sumStringLens = ZL_Input_contentSize(in);

    ZL_Output* const outStream =
            ZL_Encoder_createStringStream(eictx, 0, nbStrings, sumStringLens);
    ZL_RET_R_IF_NULL(allocation, outStream); // control allocation success

    swap_lastfirst_raw(
            ZL_Output_stringLens(outStream),
            nbStrings,
            ZL_Output_ptr(outStream),
            sumStringLens,
            ZL_Input_stringLens(in),
            ZL_Input_ptr(in));

    ZL_RET_R_IF_ERR(ZL_Output_commit(outStream, nbStrings));

    return ZL_returnValue(1); // nb Out Streams
}

// Prefer a #define, to be used as initializer in static const declarations
#define SWAP_LASTFIRST_V2_GDESC                                          \
    (ZL_TypedGraphDesc)                                                  \
    {                                                                    \
        .CTid = CT_SWAP_LASTFIRST_V2_ID, .inStreamType = ZL_Type_string, \
        .outStreamTypes = (const ZL_Type[]){ ZL_Type_string },           \
        .nbOutStreams   = 1                                              \
    }
static ZL_TypedEncoderDesc const swap_lastfirst_v2_CDesc = {
    .gd          = SWAP_LASTFIRST_V2_GDESC,
    .transform_f = swap_lastfirst_v2,
};

/* Trivial String->Serial Node that always fail */

static ZL_Report inString_justFail(
        ZL_Encoder* eictx,
        const ZL_Input* in) noexcept
{
    printf("Running inString_justFail custom transform \n");
    (void)eictx;
    assert(in != nullptr);
    assert(ZL_Input_type(in) == ZL_Type_string);
    (void)in;
    ZL_RET_R_ERR(GENERIC);
}

// Prefer a #define, to be used as initializer in static const declarations
#define STRING_JUSTFAIL_GDESC                                          \
    (ZL_TypedGraphDesc)                                                \
    {                                                                  \
        .CTid = CT_STRING_JUSTFAIL_ID, .inStreamType = ZL_Type_string, \
        .outStreamTypes = (const ZL_Type[]){ ZL_Type_serial },         \
        .nbOutStreams   = 1                                            \
    }
static ZL_TypedEncoderDesc const string_justFail_CDesc = {
    .gd          = STRING_JUSTFAIL_GDESC,
    .transform_f = inString_justFail,
    .name        = "just fail on String input"
};

/* ------   create custom parser for setStringLens   -------- */

// Requires inputSize > fixedPartsSize
ZL_SetStringLensInstructions parse_3parts_f(
        ZL_SetStringLensState* state,
        const ZL_Input* in)
{
    size_t const part1Size      = 5;
    size_t const part3Size      = 6;
    size_t const fixedPartsSize = part1Size + part3Size;
    assert(in != nullptr);
    size_t const inputSize = ZL_Input_contentSize(in);
    size_t const part2Size = inputSize - fixedPartsSize;
    printf("parse_3parts_f custom parser : "
           "splitting %zu input bytes into 3 parts: "
           "(%zu, %zu, %zu) \n",
           inputSize,
           part1Size,
           part2Size,
           part3Size);
    assert(inputSize > fixedPartsSize);
    uint32_t* const stringLens = (uint32_t*)ZL_SetStringLensState_malloc(
            state, 3 * sizeof(uint32_t));
    assert(stringLens != nullptr);
    stringLens[0]                         = (uint32_t)part1Size;
    stringLens[2]                         = (uint32_t)part3Size;
    stringLens[1]                         = (uint32_t)part2Size;
    ZL_SetStringLensInstructions const si = { stringLens, 3 };
    return si;
}

// this parser just fails, on purpose, for tests
static ZL_SetStringLensInstructions string_fail(
        ZL_SetStringLensState* ds,
        const ZL_Input* in)
{
    (void)in;
    (void)ds;
    return (ZL_SetStringLensInstructions){ nullptr, 0 };
}

ZL_SetStringLensInstructions parse_tooLarge_f(
        ZL_SetStringLensState* state,
        const ZL_Input* in)
{
    assert(in != nullptr);
    size_t const totalSize = ZL_Input_contentSize(in);
    printf("parse_tooLarge_f custom parser \n");
    uint32_t* const stringLens = (uint32_t*)ZL_SetStringLensState_malloc(
            state, 2 * sizeof(uint32_t));
    assert(stringLens != nullptr);
    stringLens[0]                         = (uint32_t)totalSize;
    stringLens[1]                         = 1;
    ZL_SetStringLensInstructions const si = { stringLens, 2 };
    return si;
}

// Requires srcSize > 2
ZL_SetStringLensInstructions parse_tooSmall_f(
        ZL_SetStringLensState* state,
        const ZL_Input* in)
{
    assert(in != nullptr);
    size_t const totalSize = ZL_Input_contentSize(in);
    printf("parse_tooSmall_f custom parser \n");
    assert(totalSize > 2);
    uint32_t* const stringLens = (uint32_t*)ZL_SetStringLensState_malloc(
            state, 2 * sizeof(uint32_t));
    assert(stringLens != nullptr);
    stringLens[0]                         = (uint32_t)totalSize - 2;
    stringLens[1]                         = 1;
    ZL_SetStringLensInstructions const si = { stringLens, 2 };
    return si;
}

/* ------   create custom graph   -------- */

static ZL_Report DynGraph_serialTo3Strings(
        ZL_Graph* gctx,
        ZL_Edge* inputCtxs[],
        size_t nbIns) noexcept
{
    ZL_RET_R_IF(graph_invalidNumInputs, nbIns != 1);
    ZL_Edge* inputCtx     = inputCtxs[0];
    const ZL_Input* input = ZL_Edge_getData(inputCtx);
    assert(ZL_Input_type(input) == ZL_Type_serial);
    size_t byteSize = ZL_Input_contentSize(input);

    assert(byteSize >= 23);
    assert(byteSize < UINT_MAX);
    const uint32_t stringLens[] = { 11, (uint32_t)byteSize - 23, 12 };

    // Run newly created Node, collect outputs
    ZL_TRY_LET_T(
            ZL_EdgeList,
            so,
            ZL_Edge_runConvertSerialToStringNode(inputCtx, stringLens, 3));
    EXPECT_EQ((int)so.nbEdges, 1);

    // Assign dummy successor to output
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(so.edges[0], ZL_GRAPH_STORE));

    return ZL_returnSuccess();
}

static const ZL_Type serialTo3StringInputMask          = ZL_Type_serial;
static const ZL_FunctionGraphDesc serialTo3Strings_dgd = {
    .name    = "Dynamic Graph decides to split serial input into 3 strings",
    .graph_f = DynGraph_serialTo3Strings,
    .inputTypeMasks      = &serialTo3StringInputMask,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

static ZL_GraphID StringGraph_serialTo3Strings(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_Compressor_registerFunctionGraph(cgraph, &serialTo3Strings_dgd);
}

static ZL_GraphID StringGraph_withExtParser_internal(
        ZL_Compressor* cgraph,
        const ZL_TypedEncoderDesc* customStringTransform,
        ZL_SetStringLensParserFn parsef)
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    ZL_NodeID const node_swap_lastfirst =
            ZL_Compressor_registerTypedEncoder(cgraph, customStringTransform);

    // Graph : src => serial->String => swap_lastfirst => String_separate =>
    // => store (2x)

    const ZL_GraphID store2x[2] = { ZL_GRAPH_STORE, ZL_GRAPH_STORE };
    const ZL_GraphID separate_store =
            ZL_Compressor_registerStaticGraph_fromNode(
                    cgraph, ZL_NODE_SEPARATE_STRING_COMPONENTS, store2x, 2);
    const ZL_GraphID swap_store = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_swap_lastfirst, separate_store);

    const ZL_NodeID parse_into_String =
            ZL_Compressor_registerConvertSerialToStringNode(
                    cgraph, parsef, nullptr);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, parse_into_String, swap_store);
}

static ZL_GraphID StringGraph_withExtParser(
        ZL_Compressor* cgraph,
        ZL_SetStringLensParserFn parsef) noexcept
{
    return StringGraph_withExtParser_internal(
            cgraph, &swap_lastfirst_v2_CDesc, parsef);
}

static ZL_GraphID StringGraph_oldVSFAPI(ZL_Compressor* cgraph) noexcept
{
    return StringGraph_withExtParser_internal(
            cgraph, &swap_lastfirst_CDesc, parse_3parts_f);
}

static ZL_GraphID StringGraph_3parts(ZL_Compressor* cgraph) noexcept
{
    return StringGraph_withExtParser(cgraph, parse_3parts_f);
}

static ZL_GraphID StringGraph_fail(ZL_Compressor* cgraph) noexcept
{
    return StringGraph_withExtParser(cgraph, string_fail);
}

static ZL_GraphID StringGraph_tooLarge(ZL_Compressor* cgraph) noexcept
{
    return StringGraph_withExtParser(cgraph, parse_tooLarge_f);
}

static ZL_GraphID StringGraph_tooSmall(ZL_Compressor* cgraph) noexcept
{
    return StringGraph_withExtParser(cgraph, parse_tooSmall_f);
}

static ZL_GraphID StringGraph_justFail(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    // Graph : src => serial->String => (fail) String->Serial => Store

    const ZL_NodeID parse_into_String =
            ZL_Compressor_registerConvertSerialToStringNode(
                    cgraph, parse_3parts_f, nullptr);

    ZL_NodeID const string_justFail =
            ZL_Compressor_registerTypedEncoder(cgraph, &string_justFail_CDesc);

    const ZL_NodeID pipeline[2] = { parse_into_String, string_justFail };
    return ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            cgraph, pipeline, 2, ZL_GRAPH_STORE);
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
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    ZL_GraphID const sgid       = graphf(cgraph);
    ZL_Report const gssr = ZL_Compressor_selectStartingGraphID(cgraph, sgid);
    EXPECT_EQ(ZL_isError(gssr), 0) << "selection of starting graphid failed\n";
    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, cgraph);
    EXPECT_EQ(ZL_isError(rcgr), 0) << "CGraph reference failed\n";
    ZL_Report const r = ZL_CCtx_compress(cctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";

    ZL_Compressor_free(cgraph);
    ZL_CCtx_free(cctx);
    return ZL_validResult(r);
}

/* ------ define custom decoder transforms ------- */

// custom decoder transform description
static ZL_Report swap_lastfirst_decode_oldAPI(
        ZL_Decoder* dictx,
        const ZL_Input* ins[]) noexcept
{
    printf("swap_lastfirst decoder \n");
    assert(ins != nullptr);
    const ZL_Input* const in = ins[0];
    assert(in != nullptr);
    assert(ZL_Input_type(in) == ZL_Type_string);
    size_t const nbStrings = ZL_Input_numElts(in);

    assert(dictx != nullptr);
    size_t const sumStringLens = ZL_Input_contentSize(in);

    ZL_Output* const outStream =
            ZL_Decoder_create1OutStream(dictx, sumStringLens, 1);
    ZL_RET_R_IF_NULL(allocation, outStream); // control allocation success

    uint32_t* const outStringLens =
            ZL_Output_reserveStringLens(outStream, nbStrings);
    ZL_RET_R_IF_NULL(allocation, outStringLens); // control allocation success

    swap_lastfirst_raw(
            outStringLens,
            nbStrings,
            ZL_Output_ptr(outStream),
            sumStringLens,
            ZL_Input_stringLens(in),
            ZL_Input_ptr(in));

    ZL_RET_R_IF_ERR(ZL_Output_commit(outStream, nbStrings));
    return ZL_returnValue(1); // nb Out Streams
}

static ZL_TypedDecoderDesc const swap_lastfirst_DDesc = {
    .gd          = SWAP_LASTFIRST_GDESC,
    .transform_f = swap_lastfirst_decode_oldAPI,
    .name        = "swap_lastfirst decoder, using old String API"
};

static ZL_Report swap_lastfirst_decode_newStringAPI(
        ZL_Decoder* dictx,
        const ZL_Input* ins[]) noexcept
{
    printf("swap_lastfirst decoder \n");
    assert(ins != nullptr);
    const ZL_Input* const in = ins[0];
    assert(in != nullptr);
    assert(ZL_Input_type(in) == ZL_Type_string);
    size_t const nbStrings = ZL_Input_numElts(in);

    assert(dictx != nullptr);
    size_t const sumStringLens = ZL_Input_contentSize(in);

    ZL_Output* const outStream =
            ZL_Decoder_create1StringStream(dictx, nbStrings, sumStringLens);
    ZL_RET_R_IF_NULL(allocation, outStream); // control allocation success

    swap_lastfirst_raw(
            ZL_Output_stringLens(outStream),
            nbStrings,
            ZL_Output_ptr(outStream),
            sumStringLens,
            ZL_Input_stringLens(in),
            ZL_Input_ptr(in));

    ZL_RET_R_IF_ERR(ZL_Output_commit(outStream, nbStrings));
    return ZL_returnValue(1); // nb Out Streams
}

static ZL_TypedDecoderDesc const swap_lastfirst_v2_DDesc = {
    .gd          = SWAP_LASTFIRST_V2_GDESC,
    .transform_f = swap_lastfirst_decode_newStringAPI,
    .name        = "swap_lastfirst decoder, using new String API"
};

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

    // register custom decoders
    ZL_REQUIRE_SUCCESS(
            ZL_DCtx_registerTypedDecoder(dctx, &swap_lastfirst_DDesc));
    ZL_REQUIRE_SUCCESS(
            ZL_DCtx_registerTypedDecoder(dctx, &swap_lastfirst_v2_DDesc));

    // Decompress, using custom decoder(s)
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
    EXPECT_EQ((int)decompressedSize, (int)inputSize)
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
#define NB_INTS 84
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return roundTripTest(graphf, input, sizeof(input), name);
}

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

static int permissiveTest(ZL_GraphFn graphf, const char* testName)
{
    printf("\n=========================== \n");
    printf(" Testing Permissive Mode \n");
    g_failingGraph_forPermissive = graphf;
    return roundTripIntegers(permissiveGraph_asGraphF, testName);
}

/* ------   published tests   ------ */

TEST(StringGraph, basic_swapLastFirst)
{
    (void)roundTripIntegers(
            StringGraph_3parts,
            "Basic graph employing String Stream, single custom transform lastFirst");
}

TEST(StringGraph, decide_stringLens_from_dynGraph)
{
    (void)roundTripIntegers(
            StringGraph_serialTo3Strings, serialTo3Strings_dgd.name);
}

TEST(StringGraph, old_VSF_API)
{
    (void)roundTripIntegers(
            StringGraph_oldVSFAPI, "Transform uses old VSF API");
}

TEST(StringGraph, parserFailure)
{
    cFailTest(
            StringGraph_fail,
            "conversion to String : parser fails => failure expected");
}

TEST(StringGraph, parse_tooLarge)
{
    cFailTest(
            StringGraph_tooLarge,
            "conversion to String : parsed lengths larger than src => failure expected");
}

TEST(StringGraph, parse_tooSmall)
{
    cFailTest(
            StringGraph_tooSmall,
            "conversion to String : parsed lengths smaller than src => failure expected");
}

TEST(StringGraph, parserFailure_permissive)
{
    permissiveTest(
            StringGraph_fail,
            "String conversion parser failure => catch and fix by Permissive mode");
}

TEST(StringGraph, parse_tooLarge_permissive)
{
    permissiveTest(
            StringGraph_tooLarge,
            "String conversion parser invalid (too large) => catch and fix by Permissive mode");
}

TEST(StringGraph, parse_tooSmall_permissive)
{
    cFailTest(
            StringGraph_tooSmall,
            "String conversion parser invalid (too small) => catch and fix by Permissive mode");
}

TEST(StringGraph, fail_processing_string_stream_permissive)
{
    (void)permissiveTest(
            StringGraph_justFail,
            "Fail processing a String Stream => catch and fix by Permissive mode");
}

} // namespace
