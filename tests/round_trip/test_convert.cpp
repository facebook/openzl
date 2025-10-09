// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// Test implicit and explicit conversion transforms

// standard C
#include <cstdio>  // printf
#include <cstdlib> // exit, rand
#include <cstring> // memcpy

// Zstrong
#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_registerStaticGraph_fromPipelineNodes1o
#include "openzl/zl_decompress.h" // ZL_decompress

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

/* ------   create a custom ndoes   -------- */

// None needed

/* ------   create custom graph   -------- */

// This graph function follows the ZL_GraphFn definition.
static ZL_GraphID conversionGraph(ZL_Compressor* cgraph) noexcept
{
    printf("running conversionGraph() \n");
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    ZL_IntParam const tokenL4              = { ZL_trlip_tokenSize, 4 };
    ZL_LocalParams const castToToken4Param = { .intParams = { &tokenL4, 1 } };

    const ZL_NodeID pipeline[] = {
        ZL_NODE_INTERPRET_AS_LE32,
        ZL_NODE_DELTA_INT,
        /* implicit conversion: numeric -> token */
        // ZL_NODE_CONVERT_NUM_TOKEN,
        ZL_NODE_CONVERT_TOKEN_TO_SERIAL,
        ZL_NODE_CONVERT_SERIAL_TO_TOKEN2,
        /* implicit conversion: token -> serial */
        ZL_NODE_CONVERT_SERIAL_TO_TOKEN4,
        /* serial->token4 using generic TOKENX conversion transform with length
           parameter */
        ZL_Compressor_cloneNode(
                cgraph, ZL_NODE_CONVERT_SERIAL_TO_TOKENX, &castToToken4Param),
        ZL_NODE_INTERPRET_TOKEN_AS_LE,
        ZL_NODE_CONVERT_NUM_TO_SERIAL,
    };
    size_t const pSize = sizeof(pipeline) / sizeof(pipeline[0]);
    return ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            cgraph, pipeline, pSize, ZL_GRAPH_ZSTD);
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
    printf(" %s \n", name);
    printf("--------------------------- \n");
    // Generate test input
#define NB_INTS 78
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

#define COMPRESSED_BOUND ZL_COMPRESSBOUND(sizeof(input))
    char compressed[COMPRESSED_BOUND] = { 0 };

    size_t const compressedSize = compress(
            compressed, COMPRESSED_BOUND, input, sizeof(input), graphf);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           sizeof(input),
           compressedSize);

    int decompressed[NB_INTS] = { 2, 28 };

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

TEST(ConversionTest, pipelineRT)
{
    roundTripTest(conversionGraph, "Long pipeline with multiple conversions");
}

} // namespace
