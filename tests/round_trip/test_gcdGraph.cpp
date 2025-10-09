// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/codecs/divide_by/decode_divide_by_binding.h"
#include "openzl/codecs/divide_by/encode_divide_by_binding.h"
#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/zl_compressor.h" // ZL_Compressor_*, ZL_Compressor_registerStaticGraph_fromNode1o
#include "openzl/zl_data.h"
#include "openzl/zl_opaque_types.h"

namespace {

static ZL_GraphID gcdGraph(ZL_Compressor* cgraph, uint64_t divisor)
{
    ZL_NodeID const node_divideBy =
            ZL_Compressor_registerDivideByNode(cgraph, divisor);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_divideBy, ZL_GRAPH_COMPRESS_GENERIC);
}

static ZL_TypedRef*
initInput(const void* src, size_t inputSize, size_t intWidth)
{
    // 32-bit only
    assert(inputSize % intWidth == 0);
    return ZL_TypedRef_createNumeric(src, intWidth, inputSize / intWidth);
}

static size_t compress(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t inputSize,
        size_t intWidth,
        ZL_GraphFn graphf)
{
    ZL_REQUIRE_GE(dstCapacity, ZL_compressBound(inputSize));

    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);

    ZL_TypedRef* const tref = initInput(src, inputSize, intWidth);
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
    EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";

    ZL_Compressor_free(cgraph);
    ZL_TypedRef_free(tref);
    ZL_CCtx_free(cctx);
    return ZL_validResult(r);
}

static size_t decompress(
        void* dst,
        size_t dstCapacity,
        size_t intWidth,
        const void* compressed,
        size_t cSize)
{
    // Collect Frame info
    ZL_FrameInfo* const fi = ZL_FrameInfo_create(compressed, cSize);
    ZL_REQUIRE_NN(fi);

    size_t const nbOutputs = ZL_validResult(ZL_FrameInfo_getNumOutputs(fi));
    ZL_REQUIRE_EQ(nbOutputs, 1);

    ZL_Type const outputType =
            (ZL_Type)ZL_validResult(ZL_FrameInfo_getOutputType(fi, 0));
    ZL_REQUIRE_EQ((int)ZL_Type_numeric, (int)outputType);

    size_t const dstSize =
            ZL_validResult(ZL_FrameInfo_getDecompressedSize(fi, 0));
    ZL_REQUIRE_GE(dstCapacity, dstSize);

    ZL_FrameInfo_free(fi);

    ZL_Report const dsdr = ZL_getDecompressedSize(compressed, cSize);
    EXPECT_EQ(ZL_isError(dsdr), 0);
    EXPECT_EQ((int)dstSize, (int)ZL_validResult(dsdr));

    // Create a static decompression state, to store the custom decoder(s)
    // The decompression state will be re-employed
    static ZL_DCtx* const dctx = ZL_DCtx_create();
    ZL_REQUIRE_NN(dctx);

    ZL_OutputInfo outInfo = {};
    ZL_Report const rsb   = ZL_DCtx_decompressTyped(
            dctx, &outInfo, dst, dstCapacity, compressed, cSize);
    EXPECT_EQ(ZL_isError(rsb), 0) << "decompression failed \n";
    EXPECT_EQ(outInfo.type, ZL_Type_numeric);
    EXPECT_EQ((int)outInfo.decompressedByteSize, (int)ZL_validResult(rsb));
    EXPECT_GT((int)outInfo.fixedWidth, 0);
    EXPECT_EQ(outInfo.fixedWidth, intWidth);
    ZL_DLOG(SEQ, "outInfo.nbElts = %zu", outInfo.numElts);
    ZL_DLOG(SEQ, "outInfo.fixedWidth = %zu", outInfo.fixedWidth);
    EXPECT_EQ(
            (int)(outInfo.numElts * outInfo.fixedWidth),
            (int)outInfo.decompressedByteSize);
    return ZL_validResult(rsb);
}

// Note: use graphf=gcdGraph
static int roundTripTest(
        ZL_GraphFn graphf,
        const void* input,
        size_t inputSize,
        size_t intWidth)
{
    // Generate test input
    size_t const compressedBound = ZL_compressBound(inputSize);
    void* const compressed       = malloc(compressedBound);
    ZL_REQUIRE_NN(compressed);

    size_t const compressedSize = compress(
            compressed, compressedBound, input, inputSize, intWidth, graphf);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           inputSize * intWidth,
           compressedSize);

    void* const decompressed = malloc(inputSize);
    ZL_REQUIRE_NN(decompressed);

    size_t const decompressedSize = decompress(
            decompressed, inputSize, intWidth, compressed, compressedSize);
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

template <typename T>
class GcdGraphTest : public testing::Test {};

using GcdGraphTypes = ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(GcdGraphTest, GcdGraphTypes, );

TYPED_TEST(GcdGraphTest, roundTrip)
{
    std::vector<TypeParam> input(10, 0);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = i * 15;
    }
    ZL_GraphFn gcdGraphf = [](ZL_Compressor* cgraph) noexcept -> ZL_GraphID {
        return gcdGraph(cgraph, 15);
    };
    roundTripTest(
            gcdGraphf,
            input.data(),
            input.size() * sizeof(TypeParam),
            sizeof(TypeParam));
}

TYPED_TEST(GcdGraphTest, roundTripEmptyInput)
{
    std::vector<TypeParam> input(0, 0);
    ZL_GraphFn gcdGraphf = [](ZL_Compressor* cgraph) noexcept -> ZL_GraphID {
        return gcdGraph(cgraph, 15);
    };
    roundTripTest(
            gcdGraphf,
            input.data(),
            input.size() * sizeof(TypeParam),
            sizeof(TypeParam));
}
} // namespace
