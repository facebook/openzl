// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// Test sending and receiving transform's out-of-band parameters

// standard C
#include <stdio.h>  // printf
#include <stdlib.h> // exit, rand
#include <string.h> // memcpy

// Zstrong
#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_registerSplitEncoder, ZL_Compressor_registerStaticGraph_fromNode
#include "openzl/zl_ctransform.h" // ZL_SplitEncoderDesc
#include "openzl/zl_dtransform.h"

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

/* ------   create a custom splitting transform   -------- */

enum { CT_param1_ID = 1, CT_param2_ID = 2, CT_param3_ID = 3, CT_split3_ID = 9 };
enum {
    param1_value = 124,
    param2_value = 7,
    param3_value = 67,
};
enum { param_size_max = 3 };

static ZL_Report send_paramX(ZL_Encoder* eictx, const ZL_Input* in, size_t n)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    printf("send_param%zu \n", n);
    ZL_REQUIRE_NN(in);
    ZL_REQUIRE(ZL_Input_type(in) == ZL_Type_serial);

    size_t const size    = ZL_Input_numElts(in);
    ZL_Output* const out = ZL_Encoder_createTypedStream(eictx, 0, size, 1);

    const void* const src = ZL_Input_ptr(in);
    void* const dst       = ZL_Output_ptr(out);
    memcpy(dst, src, size);

    ZL_ERR_IF_ERR(ZL_Output_commit(out, size));

    const unsigned char trheader[param_size_max] = { param1_value,
                                                     param2_value,
                                                     param3_value };
    ZL_REQUIRE_LE(n, (int)param_size_max);
    ZL_Encoder_sendCodecHeader(eictx, trheader, n);

    return ZL_returnValue(1); // nb Out Streams
}

static ZL_Report send_param1(ZL_Encoder* eictx, const ZL_Input* in) noexcept
{
    return send_paramX(eictx, in, 1);
}
#define PARAM1_GDESC                                                  \
    (ZL_TypedGraphDesc){ .CTid         = CT_param1_ID,                \
                         .inStreamType = ZL_Type_serial,              \
                         .outStreamTypes =                            \
                                 (const ZL_Type[]){ ZL_Type_serial }, \
                         .nbOutStreams = 1 }
static ZL_TypedEncoderDesc const param1_CDesc = {
    .gd          = PARAM1_GDESC,
    .transform_f = send_param1,
};

static ZL_Report send_param2(ZL_Encoder* eictx, const ZL_Input* in) noexcept
{
    return send_paramX(eictx, in, 2);
}
#define PARAM2_GDESC                                                  \
    (ZL_TypedGraphDesc){ .CTid         = CT_param2_ID,                \
                         .inStreamType = ZL_Type_serial,              \
                         .outStreamTypes =                            \
                                 (const ZL_Type[]){ ZL_Type_serial }, \
                         .nbOutStreams = 1 }
static ZL_TypedEncoderDesc const param2_CDesc = {
    .gd          = PARAM2_GDESC,
    .transform_f = send_param2,
};

static ZL_Report send_param3(ZL_Encoder* eictx, const ZL_Input* in) noexcept
{
    return send_paramX(eictx, in, 3);
}
#define PARAM3_GDESC                                                  \
    (ZL_TypedGraphDesc){ .CTid         = CT_param3_ID,                \
                         .inStreamType = ZL_Type_serial,              \
                         .outStreamTypes =                            \
                                 (const ZL_Type[]){ ZL_Type_serial }, \
                         .nbOutStreams = 1 }
static ZL_TypedEncoderDesc const param3_CDesc = {
    .gd          = PARAM3_GDESC,
    .transform_f = send_param3,
};

static ZL_Report split3(
        ZL_Encoder* ctx,
        size_t* usedSizes,
        const void* src,
        size_t srcSize) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(ctx);
    printf("processing `split3` on %zu bytes \n", srcSize);
    ZL_REQUIRE_NN(ctx);
    ZL_REQUIRE_NN(src);
    size_t const seg1size    = srcSize / 3;
    size_t const segSizes[3] = { seg1size, seg1size, srcSize - 2 * seg1size };

    void* outBuffs[3];
    ZL_Report const r =
            ZL_Encoder_createAllOutBuffers(ctx, outBuffs, segSizes, 3);
    ZL_REQUIRE(!ZL_isError(r));

    for (size_t n = 0, pos = 0; n < 3; n++) {
        ZL_REQUIRE_NN(outBuffs[n]);
        memcpy(outBuffs[n], (const char*)src + pos, segSizes[n]);
        pos += segSizes[n];
    }

    // report used sizes
    printf("splitting into %zu + %zu + %zu buffers \n",
           segSizes[0],
           segSizes[1],
           segSizes[2]);
    ZL_REQUIRE_NN(usedSizes);
    memcpy(usedSizes, segSizes, sizeof(segSizes));

    return ZL_returnValue(3);
}
static ZL_SplitEncoderDesc const split3_CDesc = {
    .CTid            = CT_split3_ID,
    .transform_f     = split3,
    .nbOutputStreams = 3,
};

/* ------   create custom graph   -------- */

static ZL_GraphID trivialGraph(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    ZL_NodeID const node_param1 =
            ZL_Compressor_registerTypedEncoder(cgraph, &param1_CDesc);

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_param1, ZL_GRAPH_STORE);
}

static ZL_GraphID split3Graph(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    ZL_NodeID const node_param1 =
            ZL_Compressor_registerTypedEncoder(cgraph, &param1_CDesc);
    ZL_NodeID const node_param2 =
            ZL_Compressor_registerTypedEncoder(cgraph, &param2_CDesc);
    ZL_NodeID const node_param3 =
            ZL_Compressor_registerTypedEncoder(cgraph, &param3_CDesc);
    ZL_NodeID const node_split3 =
            ZL_Compressor_registerSplitEncoder(cgraph, &split3_CDesc);

    ZL_GraphID const graph_param1 =
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, node_param1, ZL_GRAPH_STORE);
    ZL_GraphID const graph_param2 =
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, node_param2, ZL_GRAPH_STORE);
    ZL_GraphID const graph_param3 =
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, node_param3, ZL_GRAPH_STORE);

    const ZL_GraphID graphlist[] = { graph_param1, graph_param3, graph_param2 };
    size_t const graphlist_size  = sizeof(graphlist) / sizeof(graphlist[0]);
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, node_split3, graphlist, graphlist_size);
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

    ZL_Report const r =
            ZL_compress_usingGraphFn(dst, dstCapacity, src, srcSize, graphf);
    EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";

    return ZL_validResult(r);
}

/* ------   decompress   -------- */

static ZL_Report readParamX(ZL_Decoder* dictx, const ZL_Input* ins[], int n)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    printf("processing `readParam%i` \n", n);
    ZL_REQUIRE_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_REQUIRE_NN(in);
    size_t const nbBytes = ZL_Input_numElts(in);
    ZL_REQUIRE(ZL_Input_type(in) == ZL_Type_serial);

    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, nbBytes, 1);

    const void* const src = ZL_Input_ptr(in);
    void* const dst       = ZL_Output_ptr(out);
    memcpy(dst, src, nbBytes);

    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbBytes));

    // Check that the correct param value is received
    ZL_RBuffer const lip = ZL_Decoder_getCodecHeader(dictx);
    ZL_REQUIRE_EQ(lip.size, (size_t)n);
    ZL_REQUIRE(1 <= n && n <= 3);
    const unsigned char correct[param_size_max] = { param1_value,
                                                    param2_value,
                                                    param3_value };
    for (int i = 0; i < n; i++) {
        ZL_REQUIRE_EQ(((const char*)lip.start)[i], correct[i]);
    }

    return ZL_returnValue(1); // nb Out Streams
}

static ZL_Report readParam1(ZL_Decoder* dictx, const ZL_Input* ins[]) noexcept
{
    return readParamX(dictx, ins, 1);
}
static ZL_TypedDecoderDesc const param1_DDesc = {
    .gd          = PARAM1_GDESC,
    .transform_f = readParam1,
};

static ZL_Report readParam2(ZL_Decoder* dictx, const ZL_Input* ins[]) noexcept
{
    return readParamX(dictx, ins, 2);
}
static ZL_TypedDecoderDesc const param2_DDesc = {
    .gd          = PARAM2_GDESC,
    .transform_f = readParam2,
};

static ZL_Report readParam3(ZL_Decoder* dictx, const ZL_Input* ins[]) noexcept
{
    return readParamX(dictx, ins, 3);
}
static ZL_TypedDecoderDesc const param3_DDesc = {
    .gd          = PARAM3_GDESC,
    .transform_f = readParam3,
};

static size_t join3Size(const ZL_RBuffer src[]) noexcept
{
    (void)src;
    return src[0].size + src[1].size + src[2].size;
}
static size_t join3(ZL_WBuffer dst, const ZL_RBuffer src[]) noexcept
{
    printf("joining %zu + %zu + %zu bytes \n",
           src[0].size,
           src[1].size,
           src[2].size);

    ZL_REQUIRE_GE(dst.capacity, join3Size(src));
    size_t pos = 0;
    for (size_t n = 0; n < 3; n++) {
        memcpy((char*)dst.start + pos, src[n].start, src[n].size);
        pos += src[n].size;
    }
    return join3Size(src);
}
static ZL_SplitDecoderDesc const join3_DDesc = {
    .CTid           = CT_split3_ID, // Use same ID as compression side
    .nbInputStreams = 3,
    .dstBound_f     = join3Size,
    .transform_f    = join3,
};

// Register custom decoder, then decompress
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

    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx, &param1_DDesc));
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx, &param2_DDesc));
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx, &param3_DDesc));
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerSplitDecoder(dctx, &join3_DDesc));

    // Decompress, using custom decoder(s)
    ZL_Report const r =
            ZL_DCtx_decompress(dctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "decompression failed \n";

    return ZL_validResult(r);
}

/* ------   round trip test   ------ */

static int roundTripTest(ZL_GraphFn graphf, const char* name)
{
    printf("\n=========================== \n");
    printf(" Private Transforms' header : %s \n", name);
    printf("--------------------------- \n");
    // Generate test input
#define NB_CHAR ((size_t)77)
    char input[NB_CHAR];
    for (size_t i = 0; i < NB_CHAR; i++)
        input[i] = (char)i;

#define COMPRESSED_BOUND ZL_COMPRESSBOUND(sizeof(input))
    char compressed[COMPRESSED_BOUND] = { 0 };

    size_t const compressedSize = compress(
            compressed, COMPRESSED_BOUND, input, sizeof(input), graphf);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           sizeof(input),
           compressedSize);

    char decompressed[NB_CHAR] = { 2 };

    size_t const decompressedSize = decompress(
            decompressed, sizeof(decompressed), compressed, compressedSize);
    printf("decompressed %zu input bytes into %zu original bytes \n",
           compressedSize,
           decompressedSize);

    // round-trip check
    EXPECT_EQ(decompressedSize, sizeof(input))
            << "Error : decompressed size != original size \n";
    EXPECT_EQ(memcmp(input, decompressed, sizeof(input)), 0)
            << "Error : decompressed content differs from original (corruption issue) !!!  \n";

    printf("round-trip success \n");
    return 0;
}

int testTrivialTrHeader(void)
{
    return roundTripTest(trivialGraph, "trivial single-transform graph");
}

int test3TrHeader(void)
{
    return roundTripTest(split3Graph, "3 transforms sending 3 sets of headers");
}

TEST(TrHeaderTest, trivial)
{
    testTrivialTrHeader();
}

TEST(TrHeaderTest, _3TrParams)
{
    test3TrHeader();
}

} // namespace
