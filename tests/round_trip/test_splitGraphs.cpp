// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// Test sending and receiving transform's out-of-band parameters

// standard C
#include <stdio.h>  // printf
#include <stdlib.h> // exit, rand
#include <string.h> // memcpy

// Zstrong
#include "openzl/common/debug.h"           // ZL_REQUIRE
#include "openzl/compress/private_nodes.h" // ZS2_GRAPH_TRANSPOSE_SPLIT_4BYTES
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_registerStaticGraph_fromNode, ZL_Compressor_registerStaticGraph_fromPipelineNodes1o
#include "openzl/zl_ctransform.h" // ZL_SplitEncoderDesc
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_selector.h"   // ZL_SerialSelectorDesc

#include "tests/utils.h"

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

/* ------   create a custom splitting transforms   -------- */

#define CT_ADD1_ID 1
#define CT_SPLIT2_ID 2
#define CT_SPLIT3_ID 3
#define CT_ADD4_ID 4

#define DECOMPRESSION_LEVEL_TEST 5 // Just a test to query decompression level

static size_t
add1(void* dst, size_t dstCapacity, const void* src, size_t srcSize) noexcept
{
    printf("processing `add1` on %zu bytes \n", srcSize);
    assert(dstCapacity >= srcSize);
    (void)dstCapacity;
    memcpy(dst, src, srcSize);
    unsigned char* const dst8 = (unsigned char*)dst;
    dst8[0]                   = (unsigned char)(dst8[0] + 1);
    return srcSize;
}
static ZL_PipeEncoderDesc const add1_CDesc = { .CTid        = CT_ADD1_ID,
                                               .transform_f = add1,
                                               .dstBound_f  = nullptr };

#define NB_LOCAL_INT_PARAMS 2
enum { paramId1 = 101, paramId2 = 202 };
enum { paramValue1 = 11, paramValue2 = 22 };

static ZL_Report split2(
        ZL_Encoder* eic,
        size_t* usedSizes,
        const void* src,
        size_t srcSize) noexcept
{
    printf("processing `split2` on %zu bytes \n", srcSize);

    int const dlevel = ZL_Encoder_getCParam(eic, ZL_CParam_decompressionLevel);
    printf("test : query decompression level : %i \n", dlevel);
    EXPECT_EQ(dlevel, DECOMPRESSION_LEVEL_TEST);

    assert(eic != nullptr);
    ZL_LocalIntParams const lip = ZL_Encoder_getLocalIntParams(eic);
    size_t const nbIntParams    = lip.nbIntParams;
    printf("test : query %zu local int parameters \n", nbIntParams);
    assert(nbIntParams == NB_LOCAL_INT_PARAMS);
    int const param0 = lip.intParams[0].paramId;
    int const value0 = lip.intParams[0].paramValue;
    printf("param0 (%i)  => %i (value0) \n", param0, value0);
    assert(param0 == paramId1);
    assert(value0 == paramValue1);
    int const param1 = lip.intParams[1].paramId;
    int const value1 = lip.intParams[1].paramValue;
    printf("param1 (%i)  => %i (value1) \n", param1, value1);
    assert(param1 == paramId2);
    assert(value1 == paramValue2);

    size_t const seg1size    = srcSize / 2;
    size_t const segSizes[2] = { seg1size, srcSize - seg1size };

    void* outBuffs[2];
    ZL_Report const r =
            ZL_Encoder_createAllOutBuffers(eic, outBuffs, segSizes, 2);
    assert(!ZL_isError(r));
    (void)r;

    assert(src != nullptr);
    for (size_t n = 0, pos = 0; n < 2; n++) {
        assert(outBuffs[n] != nullptr);
        memcpy(outBuffs[n], (const char*)src + pos, segSizes[n]);
        pos += segSizes[n];
    }

    // report used sizes
    printf("splitting into %zu + %zu buffers \n", segSizes[0], segSizes[1]);
    assert(usedSizes != nullptr);
    memcpy(usedSizes, segSizes, sizeof(segSizes));

    return ZL_returnValue(2);
}

static ZL_SplitEncoderDesc const split2_CDesc = {
    .CTid            = CT_SPLIT2_ID,
    .transform_f     = split2,
    .nbOutputStreams = 2,
    .localParams     = { .intParams = ZL_INTPARAMS(
                             { paramId1, paramValue1 },
                             { paramId2, paramValue2 }) },
};

static ZL_Report split3(
        ZL_Encoder* ctx,
        size_t* usedSizes,
        const void* src,
        size_t srcSize) noexcept
{
    printf("processing `split3` on %zu bytes \n", srcSize);
    assert(ctx != nullptr);
    assert(src != nullptr);
    size_t const seg1size    = srcSize / 3;
    size_t const segSizes[3] = { seg1size, seg1size, srcSize - 2 * seg1size };

    void* outBuffs[3];
    ZL_Report const r =
            ZL_Encoder_createAllOutBuffers(ctx, outBuffs, segSizes, 3);
    assert(!ZL_isError(r));
    (void)r;

    for (size_t n = 0, pos = 0; n < 3; n++) {
        assert(outBuffs[n] != nullptr);
        memcpy(outBuffs[n], (const char*)src + pos, segSizes[n]);
        pos += segSizes[n];
    }

    // report used sizes
    printf("splitting into %zu + %zu + %zu buffers \n",
           segSizes[0],
           segSizes[1],
           segSizes[2]);
    assert(usedSizes != nullptr);
    memcpy(usedSizes, segSizes, sizeof(segSizes));

    return ZL_returnValue(3);
}

static ZL_SplitEncoderDesc const split3_CDesc = {
    .CTid            = CT_SPLIT3_ID,
    .transform_f     = split3,
    .nbOutputStreams = 3,
};

enum { add4_nbParams = 1, add4_paramId = 21, add4_paramV_bound = 10000 };

static ZL_Report add4(
        ZL_Encoder* eic,
        size_t* usedSizes,
        const void* src,
        size_t srcSize) noexcept
{
    printf("processing `add4` on %zu bytes \n", srcSize);
    assert(eic != nullptr);
    assert(src != nullptr);

    size_t const segSizes[1] = { srcSize };
    void* outBuffs[1];
    ZL_Report const r =
            ZL_Encoder_createAllOutBuffers(eic, outBuffs, segSizes, 1);
    assert(!ZL_isError(r));
    (void)r;

    memcpy(outBuffs[0], src, srcSize);
    unsigned char* const dst8 = (unsigned char*)outBuffs[0];
    dst8[0]                   = (unsigned char)(dst8[0] + 4);

    assert(usedSizes != nullptr);
    memcpy(usedSizes, segSizes, sizeof(segSizes));

    // Testing local parameters
    const ZL_LocalIntParams ip = ZL_Encoder_getLocalIntParams(eic);
    int const paramV           = ip.intParams[0].paramValue;
    printf("`add4` : paramID=%i , paramValue = %i \n",
           ip.intParams[0].paramId,
           paramV);
    assert(ip.nbIntParams == add4_nbParams);
    assert(ip.intParams[0].paramId == add4_paramId);
    assert(paramV < add4_paramV_bound);

    return ZL_returnValue(1);
}

static ZL_SplitEncoderDesc const add4_CDesc = {
    .CTid            = CT_ADD4_ID,
    .transform_f     = add4,
    .nbOutputStreams = 1,
};

// "fake" selector, always select the first Graph in the provided list
static ZL_GraphID select_firstGraph(
        const void* src,
        size_t srcSize,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs) noexcept
{
    (void)src;
    (void)srcSize;
    assert(nbCustomGraphs > 0);
    (void)nbCustomGraphs;
    return customGraphs[0];
}

/* ------   create custom graph   -------- */

static ZL_GraphID treeGraph(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    ZL_NodeID const node_add1_orig =
            ZL_Compressor_registerPipeEncoder(cgraph, &add1_CDesc);
    EXPECT_NE(node_add1_orig, ZL_NODE_ILLEGAL);

    // Let's exercise registerParameterizedNode on a PipeTransform
    ZL_LocalParams lparams_add1;
    memset(&lparams_add1, 0, sizeof(lparams_add1));
    const ZL_ParameterizedNodeDesc pndesc = {
        .node        = node_add1_orig,
        .localParams = &lparams_add1,
    };
    ZL_NodeID const node_add1 =
            ZL_Compressor_registerParameterizedNode(cgraph, &pndesc);
    EXPECT_NE(node_add1, ZL_NODE_ILLEGAL);
    ZL_NodeID const node_split2 =
            ZL_Compressor_registerSplitEncoder(cgraph, &split2_CDesc);
    EXPECT_NE(node_split2, ZL_NODE_ILLEGAL);
    ZL_NodeID const node_split3 =
            ZL_Compressor_registerSplitEncoder(cgraph, &split3_CDesc);
    EXPECT_NE(node_split3, ZL_NODE_ILLEGAL);
    ZL_NodeID const node_add4 =
            ZL_Compressor_registerSplitEncoder(cgraph, &add4_CDesc);
    EXPECT_NE(node_add4, ZL_NODE_ILLEGAL);

    // Test : ZL_Compressor_registerParameterizedNode() with non-constant
    // parameters Ensure it still works when @lparams lies in non-constant
    // memory, in this case, on stack, memory content no longer valid at
    // function's end
    ZL_IntParam intParam;
    intParam.paramId = add4_paramId;
    intParam.paramValue =
            (int)(((size_t)&intParam)
                  % add4_paramV_bound); // dynamic value => can't be a const
    ZL_LocalParams lparams;
    memset(&lparams, 0, sizeof(lparams));
    lparams.intParams.nbIntParams          = add4_nbParams;
    lparams.intParams.intParams            = &intParam;
    const ZL_ParameterizedNodeDesc pndesc2 = {
        .node        = node_add4,
        .localParams = &lparams,
    };
    ZL_NodeID const node_add4_v2 =
            ZL_Compressor_registerParameterizedNode(cgraph, &pndesc2);
    EXPECT_NE(node_add4_v2, ZL_NODE_ILLEGAL);

    // Create & combine sub-graphs
    ZL_GraphID const graph_add1 = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_add1, ZL_GRAPH_STORE);
    ZL_GraphID const graph_add4 = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_add4_v2, ZL_GRAPH_STORE);
    const ZL_GraphID successors[] = { graph_add1, ZL_GRAPH_STORE };
    ZL_GraphID const graph_split_leftadd =
            ZL_Compressor_registerStaticGraph_fromNode(
                    cgraph, node_split2, successors, 2);
    const ZL_GraphID successors2[] = { graph_split_leftadd, ZL_GRAPH_STORE };
    ZL_GraphID const graph_doublesplit =
            ZL_Compressor_registerStaticGraph_fromNode(
                    cgraph, node_split2, successors2, 2);

    // add custom selector
    ZL_SerialSelectorDesc const selectAdd4_desc = {
        .selector_f     = select_firstGraph,
        .customGraphs   = &graph_add4,
        .nbCustomGraphs = 1,
    };
    const ZL_GraphID graph_selectAdd4 =
            ZL_Compressor_registerSerialSelectorGraph(cgraph, &selectAdd4_desc);

    const ZL_GraphID successors3[] = { graph_split_leftadd,
                                       graph_selectAdd4,
                                       graph_doublesplit };
    ZL_GraphID const graph_triplesplit =
            ZL_Compressor_registerStaticGraph_fromNode(
                    cgraph, node_split3, successors3, 3);

    // test global parameter
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_decompressionLevel, DECOMPRESSION_LEVEL_TEST));

    return graph_triplesplit;
}

static ZL_GraphID transposeSplit4(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    return ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            cgraph,
            ZL_NODELIST(
                    ZL_NODE_CONVERT_SERIAL_TO_TOKEN4, ZL_NODE_TRANSPOSE_SPLIT),
            ZL_GRAPH_STORE);
}

/* ------   compress, using provided graph function   -------- */

static size_t compress(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_GraphFn graphf)
{
    assert(dstCapacity >= ZL_compressBound(srcSize));

    ZL_Report const r =
            ZL_compress_usingGraphFn(dst, dstCapacity, src, srcSize, graphf);
    EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";

    return ZL_validResult(r);
}

/* ------ define custom decoder transforms ------- */

static size_t
sub1(void* dst, size_t dstCapacity, const void* src, size_t srcSize) noexcept
{
    printf("decoding `add1` \n");
    assert(dstCapacity >= srcSize);
    (void)dstCapacity;
    assert(dst != nullptr);
    assert(src != nullptr);
    memcpy(dst, src, srcSize);
    unsigned char* const dst8 = (unsigned char*)dst;
    assert(srcSize >= 1);
    dst8[0] = (unsigned char)(dst8[0] - 1);
    return srcSize;
}
static ZL_PipeDecoderDesc const sub1_DDesc = {
    .CTid        = CT_ADD1_ID,
    .transform_f = sub1,
};

// custom decoder transform description
static size_t join2Size(const ZL_RBuffer src[]) noexcept
{
    (void)src;
    return src[0].size + src[1].size;
}
static size_t join2(ZL_WBuffer dst, const ZL_RBuffer src[]) noexcept
{
    printf("joining %zu + %zu bytes \n", src[0].size, src[1].size);

    assert(dst.capacity >= join2Size(src));
    size_t pos = 0;
    for (size_t n = 0; n < 2; n++) {
        memcpy((char*)dst.start + pos, src[n].start, src[n].size);
        pos += src[n].size;
    }
    return join2Size(src);
}
static ZL_SplitDecoderDesc const join2_DDesc = {
    .CTid           = CT_SPLIT2_ID, // Use same ID as compression side
    .nbInputStreams = 2,
    .dstBound_f     = join2Size,
    .transform_f    = join2,
};

// custom decoder transform description
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

    assert(dst.capacity >= join3Size(src));
    size_t pos = 0;
    for (size_t n = 0; n < 3; n++) {
        memcpy((char*)dst.start + pos, src[n].start, src[n].size);
        pos += src[n].size;
    }
    return join3Size(src);
}
static ZL_SplitDecoderDesc const join3_DDesc = {
    .CTid           = CT_SPLIT3_ID, // Use same ID as compression side
    .nbInputStreams = 3,
    .dstBound_f     = join3Size,
    .transform_f    = join3,
};

// custom decoder transform description
static size_t sub4Size(const ZL_RBuffer src[]) noexcept
{
    (void)src;
    return src[0].size;
}
static size_t sub4(ZL_WBuffer dst, const ZL_RBuffer src[]) noexcept
{
    printf("sub4 to input buffer %zu bytes \n", src[0].size);

    printf("decoding `add4` \n");
    assert(dst.start != nullptr);
    assert(src[0].start != nullptr);
    size_t const srcSize = sub4Size(src);
    assert(dst.capacity >= srcSize);
    memcpy(dst.start, src[0].start, srcSize);
    unsigned char* const dst8 = (unsigned char*)dst.start;
    assert(srcSize >= 1);
    dst8[0] = (unsigned char)(dst8[0] - 4);
    return srcSize;
}
static ZL_SplitDecoderDesc const sub4_DDesc = {
    .CTid           = CT_ADD4_ID, // Use same ID as compression side
    .nbInputStreams = 1,
    .dstBound_f     = sub4Size,
    .transform_f    = sub4,
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
    (void)join2_DDesc;
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerPipeDecoder(dctx, &sub1_DDesc));
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerSplitDecoder(dctx, &join2_DDesc));
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerSplitDecoder(dctx, &join3_DDesc));
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerSplitDecoder(dctx, &sub4_DDesc));

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

int multiSplitsTest(void)
{
    return roundTripIntegers(treeGraph, "Example graph with multiple splits");
}

int transposeSplit4Test(void)
{
    return roundTripIntegers(transposeSplit4, "Transpose + split 4 graph");
}

TEST(SplitGraphs, multiSplits)
{
    multiSplitsTest();
}

TEST(SplitGraphs, transposeSplit4)
{
    transposeSplit4Test();
}

} // namespace
