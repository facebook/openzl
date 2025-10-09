// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <cstdio>  // printf
#include <cstring> // memcpy

// Zstrong
#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_*, ZL_Compressor_registerStaticGraph_fromNode1o
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_errors.h"
#include "openzl/zl_errors_types.h"

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

/* ------   custom transforms   -------- */

#define CT_DECFAIL_ID 1
#define CT_COMPRESSFAIL_ID 2
#define CT_JUSTCOPY_ID 3

// This transform just copy the input to the output.
// On the decoder side, it will fail after creating the Stream,
// in order to check error management and buffer lifetime
static ZL_Report decFail_encoder(
        ZL_Encoder* eictx, // To create output stream
        const ZL_Input* in) noexcept
{
    assert(ZL_Input_type(in) == ZL_Type_serial);
    size_t const size    = ZL_Input_contentSize(in);
    ZL_Output* const out = ZL_Encoder_createTypedStream(eictx, 0, size, 1);
    ZL_RET_R_IF_NULL(allocation, out); // control allocation success

    memcpy(ZL_Output_ptr(out), ZL_Input_ptr(in), size);
    ZL_RET_R_IF_ERR(ZL_Output_commit(out, size));

    return ZL_returnValue(1); // nb Out Streams
}

// We use a #define, to be employed as initializer in static const declarations
// below.
#define DECFAIL_GDESC                                          \
    (ZL_TypedGraphDesc)                                        \
    {                                                          \
        .CTid = CT_DECFAIL_ID, .inStreamType = ZL_Type_serial, \
        .outStreamTypes = (const ZL_Type[]){ ZL_Type_serial }, \
        .nbOutStreams   = 1                                    \
    }

// Encoder declaration
static ZL_TypedEncoderDesc const decFail_CDesc = {
    .gd          = DECFAIL_GDESC,
    .transform_f = decFail_encoder,
};

// This transform always just fails
static ZL_Report compressFail_encoder(
        ZL_Encoder* eictx, // To create output stream
        const ZL_Input* in) noexcept
{
    (void)eictx;
    (void)in;
    ZL_RET_R_ERR(GENERIC);
}

// We use a #define, to be employed as initializer in static const declarations
// below.
#define COMPRESSFAIL_GDESC                                          \
    (ZL_TypedGraphDesc)                                             \
    {                                                               \
        .CTid = CT_COMPRESSFAIL_ID, .inStreamType = ZL_Type_serial, \
        .outStreamTypes = (const ZL_Type[]){ ZL_Type_serial },      \
        .nbOutStreams   = 1                                         \
    }

// Encoder declaration
static ZL_TypedEncoderDesc const compressFail_CDesc = {
    .gd          = COMPRESSFAIL_GDESC,
    .transform_f = compressFail_encoder,
    .name        = "compressFail",
};

// just duplicate input, to separate cleanly from source
static ZL_Report justCopy_encoder(
        ZL_Encoder* eictx, // To create output stream
        const ZL_Input* in) noexcept
{
    (void)eictx;
    assert(in != NULL);
    assert(ZL_Input_type(in) == ZL_Type_serial);
    assert(ZL_Input_eltWidth(in) == 1);
    size_t const size = ZL_Input_numElts(in);
    ZL_Output* out    = ZL_Encoder_createTypedStream(eictx, 0, size, 1);
    assert(out != NULL);
    memcpy(ZL_Output_ptr(out), ZL_Input_ptr(in), size);
    ZL_RET_R_IF_ERR(ZL_Output_commit(out, size));
    return ZL_returnSuccess();
}

// We use a #define, to be employed as initializer in static const declarations
// below.
#define JUSTCOPY_GDESC                                          \
    (ZL_TypedGraphDesc)                                         \
    {                                                           \
        .CTid = CT_JUSTCOPY_ID, .inStreamType = ZL_Type_serial, \
        .outStreamTypes = (const ZL_Type[]){ ZL_Type_serial },  \
        .nbOutStreams   = 1                                     \
    }

// Encoder declaration
static ZL_TypedEncoderDesc const justCopy_CDesc = {
    .gd          = JUSTCOPY_GDESC,
    .transform_f = justCopy_encoder,
    .name        = "justCopy",
};

/* ------   custom graphs   -------- */

// Currently, zstrong requires setting up a CGraph to start compression.
// The below (simple) graphs is a work around this limitation.
// They may be removed in the future, once default graphs are a thing.

static ZL_GraphID serialGraph(ZL_Compressor* cgraph) noexcept
{
    (void)cgraph;
    return ZL_GRAPH_ZSTD;
}

static ZL_GraphID graph_decFail(ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID decFail =
            ZL_Compressor_registerTypedEncoder(cgraph, &decFail_CDesc);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, decFail, ZL_GRAPH_ZSTD);
}

static ZL_GraphID graph_compressFail(ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID forward =
            ZL_Compressor_registerTypedEncoder(cgraph, &justCopy_CDesc);
    ZL_NodeID compressFail =
            ZL_Compressor_registerTypedEncoder(cgraph, &compressFail_CDesc);
    const ZL_NodeID nodes[3] = { forward, forward, compressFail };
    return ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            cgraph, nodes, 3, ZL_GRAPH_STORE);
}

static ZL_GraphID graph_constant(ZL_Compressor* cgraph) noexcept
{
    (void)cgraph;
    return ZL_GRAPH_CONSTANT;
}

static ZL_GraphID graph_store(ZL_Compressor* cgraph) noexcept
{
    (void)cgraph;
    return ZL_GRAPH_STORE;
}

/* ------   compress, specify Type & CGraph   -------- */

static ZL_TypedRef* initInput(const void* src, size_t srcSize, ZL_Type type)
{
    switch (type) {
        case ZL_Type_serial:
            return ZL_TypedRef_createSerial(src, srcSize);
        case ZL_Type_struct:
            assert(srcSize % 4 == 0);
            return ZL_TypedRef_createStruct(src, 4, srcSize / 4);
        case ZL_Type_numeric:
            assert(srcSize % 4 == 0);
            return ZL_TypedRef_createNumeric(src, 4, srcSize / 4);
        case ZL_Type_string:
        default:
            return NULL;
    }
}

static ZL_Report compress(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_Type type,
        ZL_GraphFn graphf)
{
    ZL_REQUIRE_GE(dstCapacity, ZL_compressBound(srcSize));

    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);

    ZL_TypedRef* const tref = initInput(src, srcSize, type);
    ZL_REQUIRE_NN(tref);

    // CGraph setup
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    ZL_Report const gssr = ZL_Compressor_initUsingGraphFn(cgraph, graphf);
    EXPECT_EQ(ZL_isError(gssr), 0) << "selection of starting graphid failed\n";
    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, cgraph);
    EXPECT_EQ(ZL_isError(rcgr), 0) << "CGraph reference failed\n";
    // Parameter setup
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    ZL_Report const r = ZL_CCtx_compressTypedRef(cctx, dst, dstCapacity, tref);

    ZL_Compressor_free(cgraph);
    ZL_TypedRef_free(tref);
    ZL_CCtx_free(cctx);
    return r;
}

/* ------ define custom decoder transforms ------- */

// custom decoder transform description
static ZL_Report decFail_decoder(
        ZL_Decoder* eictx,
        const ZL_Input* ins[]) noexcept
{
    assert(ins != nullptr);
    const ZL_Input* const in = ins[0];
    assert(in != nullptr);
    assert(ZL_Input_type(in) == ZL_Type_serial);

    size_t const size    = ZL_Input_contentSize(in);
    ZL_Output* const out = ZL_Decoder_create1OutStream(eictx, size, 1);
    ZL_RET_R_IF_NULL(allocation, out); // control allocation success

    memcpy(ZL_Output_ptr(out), ZL_Input_ptr(in), size);
    ZL_RET_R_IF_ERR(ZL_Output_commit(out, size));

    // now, let's fail on purpose
    ZL_RET_R_ERR(GENERIC);
}
static ZL_TypedDecoderDesc const decFail_DDesc = {
    .gd          = DECFAIL_GDESC,
    .transform_f = decFail_decoder,
};

// just duplicate input, to separate cleanly from source
static ZL_Report justCopy_decoder(
        ZL_Decoder* eictx, // To create output stream
        const ZL_Input* ins[]) noexcept
{
    (void)eictx;
    assert(ins != NULL);
    const ZL_Input* const in = ins[0];
    assert(in != NULL);
    assert(ZL_Input_type(in) == ZL_Type_serial);
    assert(ZL_Input_eltWidth(in) == 1);
    size_t const size = ZL_Input_numElts(in);
    ZL_Output* out    = ZL_Decoder_create1OutStream(eictx, size, 1);
    assert(out != NULL);
    memcpy(ZL_Output_ptr(out), ZL_Input_ptr(in), size);
    ZL_RET_R_IF_ERR(ZL_Output_commit(out, size));
    return ZL_returnSuccess();
}

// Encoder declaration
static ZL_TypedDecoderDesc const justCopy_DDesc = {
    .gd          = JUSTCOPY_GDESC,
    .transform_f = justCopy_decoder,
    .name        = "justCopy_decoder",
};

/* ------   decompress   -------- */

static size_t decompress(
        void* dst,
        size_t dstCapacity,
        ZL_Type type,
        const void* src,
        size_t srcSize)
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
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx, &justCopy_DDesc));

    // Decompress, using custom decoder(s)
    ZL_OutputInfo outInfo = {};
    ZL_Report const r     = ZL_DCtx_decompressTyped(
            dctx, &outInfo, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "decompression failed \n";
    EXPECT_EQ(outInfo.type, type);

    return ZL_validResult(r);
}

static size_t
decompress_fail(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
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
            ZL_DCtx_registerTypedDecoder(dctx, &decFail_DDesc)); // will fail

    // Decompress, using custom decoder(s)
    ZL_TypedBuffer* tbuf = ZL_TypedBuffer_create();
    ZL_Report const r    = ZL_DCtx_decompressTBuffer(dctx, tbuf, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 1) << "decompression should have failed \n";
    ZL_TypedBuffer_free(tbuf);
    (void)dst;

    return (size_t)(-1);
}

/* decompress (serial) into a too small buffer => error */
using decFail_scenario_e = enum { dst0, dstm1 };
static void decompress_tooSmall(
        decFail_scenario_e scenario,
        void* dst,
        size_t dstCapacity,
        ZL_Type type,
        const void* src,
        size_t srcSize)
{
    // Check buffer size
    ZL_Report const dr = ZL_getDecompressedSize(src, srcSize);
    ZL_REQUIRE(!ZL_isError(dr));
    size_t const dstSize = ZL_validResult(dr);
    ZL_REQUIRE_GE(dstCapacity, dstSize);

    // too small dstCapacity
    if (scenario == dstm1) {
        dstCapacity = dstSize / 2;
    }
    if (scenario == dst0) {
        dstCapacity = 0;
    }

    // Create a single decompression state, to store the custom decoder(s)
    // The decompression state will be re-employed
    static ZL_DCtx* const dctx = ZL_DCtx_create();
    ZL_REQUIRE_NN(dctx);

    // Decompress, using custom decoder(s)
    ZL_OutputInfo outInfo;
    ZL_Report const r = ZL_DCtx_decompressTyped(
            dctx, &outInfo, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 1) << "decompression should have failed \n";
    (void)type; // Note: when decompression fails, @outInfo is not expected to
                // be filled correctly
}

/* ------   round trip test   ------ */

static int roundTripTest(
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
    ZL_REQUIRE_NN(compressed);

    ZL_Report const compressionReport = compress(
            compressed, compressedBound, input, inputSize, inputType, graphf);
    EXPECT_EQ(ZL_isError(compressionReport), 0) << "compression failed \n";
    size_t const compressedSize = ZL_validResult(compressionReport);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           inputSize,
           compressedSize);

    void* const decompressed = malloc(inputSize);
    ZL_REQUIRE_NN(decompressed);

    size_t const decompressedSize = decompress(
            decompressed, inputSize, inputType, compressed, compressedSize);
    printf("decompressed %zu input bytes into %zu original bytes \n",
           compressedSize,
           decompressedSize);

    // round-trip check
    EXPECT_EQ((int)decompressedSize, (int)inputSize)
            << "Error : decompressed size != original size \n";
    if (inputSize) {
        printf("checking that round-trip regenerates the same content \n");
        EXPECT_EQ(memcmp(input, decompressed, inputSize), 0)
                << "Error : decompressed content differs from original (corruption issue) !!!  \n";
    }

    printf("round-trip success \n");
    free(decompressed);
    free(compressed);
    return 0;
}

static int roundTripIntegers(ZL_GraphFn graphf, ZL_Type type, const char* name)
{
    // Generate test input
#define NB_INTS 84
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return roundTripTest(graphf, input, sizeof(input), type, name);
}

static int roundTrip_fail(
        ZL_GraphFn graphf,
        const char* testTitle,
        const void* input,
        size_t inputSize,
        ZL_Type inputType)
{
    printf("\n=========================== \n");
    printf(" %s \n", testTitle);
    printf("--------------------------- \n");
    // Generate test input
    size_t const compressedBound = ZL_compressBound(inputSize);
    void* const compressed       = malloc(compressedBound);
    ZL_REQUIRE_NN(compressed);

    ZL_Report const compressionReport = compress(
            compressed, compressedBound, input, inputSize, inputType, graphf);
    if (!ZL_isError(compressionReport)) {
        size_t const compressedSize = ZL_validResult(compressionReport);
        printf("compressed %zu input bytes into %zu compressed bytes \n",
               inputSize,
               compressedSize);

        void* const decompressed = malloc(inputSize);
        ZL_REQUIRE_NN(decompressed);

        decompress_fail(decompressed, inputSize, compressed, compressedSize);
        free(decompressed);
    }

    printf("round-trip failed as expected \n");
    free(compressed);
    return 0;
}

static int RTFail(ZL_GraphFn graphf, const char* testTitle, ZL_Type inputType)
{
    // Generate test input
#define NB_INTS 84
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return roundTrip_fail(graphf, testTitle, input, sizeof(input), inputType);
}

static int roundTripFail_destTooSmall(
        ZL_GraphFn graphf,
        const char* testTitle,
        decFail_scenario_e scenario,
        ZL_Type inputType,
        const void* input,
        size_t inputSize)
{
    printf("\n=========================== \n");
    printf(" %s \n", testTitle);
    printf("--------------------------- \n");
    // Generate test input
    size_t const compressedBound = ZL_compressBound(inputSize);
    void* const compressed       = malloc(compressedBound);
    ZL_REQUIRE_NN(compressed);

    ZL_Report const compressionReport = compress(
            compressed, compressedBound, input, inputSize, inputType, graphf);
    EXPECT_EQ(ZL_isError(compressionReport), 0) << "compression failed \n";
    size_t const compressedSize = ZL_validResult(compressionReport);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           inputSize,
           compressedSize);

    void* const decompressed = malloc(inputSize);
    ZL_REQUIRE_NN(decompressed);

    decompress_tooSmall(
            scenario,
            decompressed,
            inputSize,
            inputType,
            compressed,
            compressedSize);

    free(decompressed);
    free(compressed);
    return 0;
}

static int RTFail_gen(
        ZL_GraphFn graphf,
        const char* testTitle,
        decFail_scenario_e scenario,
        ZL_Type inputType)
{
    // Generate test input
#define NB_INTS 84
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return roundTripFail_destTooSmall(
            graphf, testTitle, scenario, inputType, input, sizeof(input));
}

/* ------   permissive tests   ------ */

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
    return roundTripIntegers(
            permissiveGraph_asGraphF, ZL_Type_serial, testName);
}

/* ------   exposed tests   ------ */

TEST(RTbasic, serial)
{
    (void)roundTripIntegers(
            serialGraph,
            ZL_Type_serial,
            "Typed Compression, using Serial TypedRef");
}

TEST(RTbasic, struct)
{
    (void)roundTripIntegers(
            serialGraph,
            ZL_Type_struct,
            "Typed Compression, using Struct TypedRef");
}

TEST(RTbasic, compression_fails)
{
    (void)RTFail(graph_compressFail, "Failing Transform", ZL_Type_serial);
}

TEST(RTbasic, decoder_fails)
{
    (void)RTFail(graph_decFail, "Destination buffer: size 0", ZL_Type_serial);
    // doing twice, to re-use the state
    (void)RTFail(graph_decFail, "Destination buffer: size 0", ZL_Type_serial);
}

TEST(RTbasic, dstTooSmall)
{
    (void)RTFail_gen(
            serialGraph,
            "Destination buffer too small: decompression fails properly",
            dstm1,
            ZL_Type_serial);
}

TEST(RTbasic, struct_dstTooSmall)
{
    (void)RTFail_gen(
            serialGraph,
            "Destination struct buffer too small",
            dstm1,
            ZL_Type_struct);
}

TEST(RTbasic, dst0)
{
    (void)RTFail_gen(
            serialGraph, "Destination buffer: size 0", dst0, ZL_Type_serial);
}

TEST(RTbasic, permissive)
{
    (void)permissiveTest(
            graph_compressFail, "Catch up and fix failing Transform");
}

/* ------   compression tests   -------- */

TEST(RTbasic, compressConstant0)
{
    printf("compressing constant `0` \n");
#define MAX_SRC_SIZE 21
    char src[MAX_SRC_SIZE] = { 0 };
#define DST_CAPACITY ZL_COMPRESSBOUND(MAX_SRC_SIZE)
    char dst[DST_CAPACITY] = { 0 };
    for (size_t n = 1; n < MAX_SRC_SIZE; n++) {
        ZL_Report r = compress(
                dst, DST_CAPACITY, src, n, ZL_Type_serial, graph_constant);
        EXPECT_FALSE(ZL_isError(r));
        size_t s = ZL_validResult(r);
        printf("Compressing %zu `0` => %zu bytes \n", n, s);
    }
}

TEST(RTbasic, compressInt0)
{
    printf("compressing ints `0` \n");
    char src[MAX_SRC_SIZE] = { 0 };
    char dst[DST_CAPACITY] = { 0 };
    for (size_t n = 0; n < MAX_SRC_SIZE; n += 4) {
        ZL_Report r = compress(
                dst, DST_CAPACITY, src, n, ZL_Type_serial, serialGraph);
        EXPECT_FALSE(ZL_isError(r));
        size_t s = ZL_validResult(r);
        printf("Compressing %zu 32-bit `0` => %zu bytes \n", n / 4, s);
    }
}

TEST(RTbasic, storingDirectly)
{
    printf("storing constant `0` \n");
    char src[MAX_SRC_SIZE] = { 0 };
    char dst[DST_CAPACITY] = { 0 };
    for (size_t n = 1; n < MAX_SRC_SIZE; n++) {
        ZL_Report r = compress(
                dst, DST_CAPACITY, src, n, ZL_Type_serial, graph_store);
        EXPECT_FALSE(ZL_isError(r));
        size_t s = ZL_validResult(r);
        printf("Storing %zu `0` => %zu bytes \n", n, s);
    }
}

} // namespace
