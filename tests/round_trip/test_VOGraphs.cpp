// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <cstdio>  // printf
#include <cstring> // memcpy

// Zstrong
#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_*, ZL_Compressor_registerStaticGraph_fromNode1o
#include "openzl/zl_ctransform.h" // ZL_VOEncoderDesc
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_dtransform.h" // ZL_VODecoderDesc

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

#define CT_SPLIT_ID 2
#define CT_FAIL_OVERALLOCATE_ID 3

size_t sum(const size_t s[], size_t n)
{
    size_t total = 0;
    for (size_t u = 0; u < n; u++)
        total += s[u];
    return total;
}

// Note : transform kernels are as lean as possible
static void splitN(
        void* dst[],
        const size_t dstSizes[],
        size_t nbDsts,
        const void* src,
        size_t srcSize)
{
    assert(sum(dstSizes, nbDsts) == srcSize);
    (void)srcSize;
    size_t spos = 0;
    for (size_t u = 0; u < nbDsts; u++) {
        memcpy(dst[u], (const char*)src + spos, dstSizes[u]);
        spos += dstSizes[u];
    }
    assert(spos == srcSize);
}

// This custom transform splits input in an arbitrary way
// (currently 4 segments of different sizes).
// The exact way it splits doesn't matter,
// what matters is that it respects the contract of the decoder side.
// In this case, it's "concatenate", which simply expects to
// concatenate all its input streams in the received order,
// which is the same order in which output streams were created during encoding.
static ZL_Report customSplit4_encoder(
        ZL_Encoder* eic,
        const ZL_Input* in) noexcept
{
    printf("starting customSplit4_encoder \n");
    assert(eic != nullptr);
    assert(in != nullptr);
    assert(ZL_Input_type(in) == ZL_Type_serial);
    const void* const src = ZL_Input_ptr(in);
    size_t const srcSize  = ZL_Input_numElts(in);

    // Just split arbitrarily into 4 parts of unequal size
    size_t const nbOuts = 4;
    size_t const s1     = srcSize / 3;
    size_t const s2     = srcSize / 4;
    size_t const s3     = srcSize / 5;
    size_t const s4     = srcSize - (s1 + s2 + s3);

    ZL_Output* const out1 = ZL_Encoder_createTypedStream(eic, 0, s1, 1);
    ZL_RET_R_IF_NULL(allocation, out1);

    ZL_Output* const out2 = ZL_Encoder_createTypedStream(eic, 0, s2, 1);
    ZL_RET_R_IF_NULL(allocation, out2);

    ZL_Output* const out3 = ZL_Encoder_createTypedStream(eic, 0, s3, 1);
    ZL_RET_R_IF_NULL(allocation, out3);

    ZL_Output* const out4 = ZL_Encoder_createTypedStream(eic, 0, s4, 1);
    ZL_RET_R_IF_NULL(allocation, out4);

    void* dstArray[] = {
        ZL_Output_ptr(out1),
        ZL_Output_ptr(out2),
        ZL_Output_ptr(out3),
        ZL_Output_ptr(out4),
    };

    const size_t dstSizes[] = { s1, s2, s3, s4 };

    printf("Splitting into 4 segments of size %zu, %zu, %zu, %zu \n",
           s1,
           s2,
           s3,
           s4);

    splitN(dstArray, dstSizes, nbOuts, src, srcSize);

    ZL_RET_R_IF_ERR(ZL_Output_commit(out1, s1));
    ZL_RET_R_IF_ERR(ZL_Output_commit(out2, s2));
    ZL_RET_R_IF_ERR(ZL_Output_commit(out3, s3));
    ZL_RET_R_IF_ERR(ZL_Output_commit(out4, s4));

    return ZL_returnSuccess();
}

static const ZL_VOGraphDesc split4_gd = {
    .CTid         = CT_SPLIT_ID,
    .inStreamType = ZL_Type_serial,
    .voTypes      = (const ZL_Type[]){ ZL_Type_serial },
    .nbVOs        = 1,
};

static ZL_VOEncoderDesc const split4_CDesc = {
    .gd          = split4_gd,
    .transform_f = customSplit4_encoder,
};

/* This transform has 1 singleton outcome, and 1 VO outcome.
 * The code confuses the 2, and over-allocates twice the singleton outcome.
 * This must result in a "clean" error, aka return NULL.
 * The transform checks the return value, see it's NULL, and errors out.
 */
static ZL_Report fail_overAllocateStream(
        ZL_Encoder* eic,
        const ZL_Input* in) noexcept
{
    printf("starting fail_overAllocateStream \n");
    (void)eic; // not used
    (void)in;  // not used

    size_t size = 10; // anything

    ZL_Output* const out1 = ZL_Encoder_createTypedStream(eic, 0, size, 1);
    assert(out1 != nullptr);
    (void)out1; // unused for this test

    ZL_Output* const out2 = ZL_Encoder_createTypedStream(eic, 0, size, 1);
    assert(out2 == nullptr);

    if (out2 == nullptr) {
        ZL_RET_R_ERR(allocation);
    }

    assert(0); // should never reach that point
    return ZL_returnSuccess();
}

static const ZL_VOGraphDesc fail_das_gd = {
    .CTid           = CT_FAIL_OVERALLOCATE_ID,
    .inStreamType   = ZL_Type_serial,
    .singletonTypes = (const ZL_Type[]){ ZL_Type_serial },
    .nbSingletons   = 1,
    .voTypes        = (const ZL_Type[]){ ZL_Type_serial },
    .nbVOs          = 1,
};

static ZL_VOEncoderDesc const fail_das_CDesc = {
    .gd          = fail_das_gd,
    .transform_f = fail_overAllocateStream,
};

/* ------   create custom graph   -------- */

// The trivial VO Graph just registers custom transform split4
// which is working as a VO transform
// and defines a simple graph with it
// where the only outcome of all its outputs is a simple STORE operation.
static ZL_GraphID trivialVOGraph(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_Compressor_registerVOEncoder(cgraph, &split4_CDesc),
            ZL_GRAPH_STORE);
}

/* This graph requires input to be at least 48+ bytes long */
static ZL_GraphID failTransform_streamOverAllocation(
        ZL_Compressor* cgraph) noexcept
{
    printf("running failTransform_streamOverAllocation() \n");
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    const ZL_GraphID gidList[] = { ZL_GRAPH_STORE, ZL_GRAPH_STORE };
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph,
            ZL_Compressor_registerVOEncoder(cgraph, &fail_das_CDesc),
            gidList,
            2);
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
    // test StreamArena setting
    EXPECT_EQ(
            ZL_isError(ZL_CCtx_setDataArena(cctx, ZL_DataArenaType_stack)), 0);
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    (void)graphf(cgraph);
    // Note: rely on implicit selection of last registered graph as starting
    // graph
    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, cgraph);
    EXPECT_EQ(ZL_isError(rcgr), 0) << "CGraph reference failed\n";
    ZL_Report const r = ZL_CCtx_compress(cctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";

    ZL_Compressor_free(cgraph);
    ZL_CCtx_free(cctx);
    return ZL_validResult(r);
}

/* ------ define custom decoder transforms ------- */

// raw transform, minimalist interface
// @return: size written into dst (necessarily <= dstCapacity)
// requirement : sum(srcSizes[]) <= dstCapacity
static size_t concatenate(
        void* dst,
        size_t dstCapacity,
        const void* srcs[],
        size_t srcSizes[],
        size_t nbSrcs)
{
    if (nbSrcs)
        assert(srcSizes != nullptr && srcs != nullptr);
    assert(dstCapacity <= sum(srcSizes, nbSrcs));
    if (dstCapacity)
        assert(dst != nullptr);
    size_t pos = 0;
    for (size_t n = 0; n < nbSrcs; n++) {
        memcpy((char*)dst + pos, srcs[n], srcSizes[n]);
        pos += srcSizes[n];
    }
    assert(pos <= dstCapacity);
    return pos;
}

// decoder interface, respecting the Zstrong VOTransform contract
static ZL_Report concat_decoder(
        ZL_Decoder* eictx,
        const ZL_Input* O1srcs[],
        size_t nbO1Srcs,
        const ZL_Input* VOsrcs[],
        size_t nbVOSrcs) noexcept
{
    assert(nbO1Srcs == 0);
    (void)nbO1Srcs;
    (void)O1srcs;
    assert(VOsrcs != nullptr);
    for (size_t n = 0; n < nbVOSrcs; n++)
        assert(VOsrcs[n] != nullptr);
    for (size_t n = 0; n < nbVOSrcs; n++)
        assert(ZL_Input_type(O1srcs[n]) == ZL_Type_serial);

#define NB_SRCS_MAX 4
    assert(nbVOSrcs <= NB_SRCS_MAX);
    size_t srcSizes[NB_SRCS_MAX];
    for (size_t n = 0; n < nbVOSrcs; n++)
        srcSizes[n] = ZL_Input_numElts(VOsrcs[n]);

    const void* srcPtrs[NB_SRCS_MAX];
    for (size_t n = 0; n < nbVOSrcs; n++)
        srcPtrs[n] = ZL_Input_ptr(VOsrcs[n]);

    size_t const dstSize = sum(srcSizes, nbVOSrcs);

    ZL_Output* const out = ZL_Decoder_create1OutStream(eictx, dstSize, 1);
    ZL_RET_R_IF_NULL(allocation, out);

    size_t const r = concatenate(
            ZL_Output_ptr(out), dstSize, srcPtrs, srcSizes, nbVOSrcs);
    assert(r == dstSize);
    (void)r;

    ZL_RET_R_IF_ERR(ZL_Output_commit(out, dstSize));

    return ZL_returnSuccess();
}

// custom decoder transform description
static ZL_VODecoderDesc const concat_DDesc = { .gd          = split4_gd,
                                               .transform_f = concat_decoder,
                                               .name = "split4_decoder" };

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
    // test streamArena setting
    EXPECT_EQ(
            ZL_isError(ZL_DCtx_setStreamArena(dctx, ZL_DataArenaType_stack)),
            0);

    // register custom decoders
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerVODecoder(dctx, &concat_DDesc));

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
    EXPECT_EQ(decompressedSize, inputSize)
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

/* ------   exposed tests   ------ */

TEST(VOGraphs, trivial_VOTransform)
{
    (void)roundTripIntegers(
            trivialVOGraph,
            "Trivial graph employing a Variable Output transform (just split+concat)");
}

TEST(VOGraphs, fail_StreamOverAllocation)
{
    (void)cFailTest(
            failTransform_streamOverAllocation,
            "custom transform clean failure (without crash): "
            "attempt to generate 2 outputs for the same singleton outcome");
}

} // namespace
