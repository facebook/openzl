// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/tests/test_prob_selector_fixture.h"

#include "custom_transforms/thrift/constants.h" // @manual
#include "custom_transforms/thrift/probabilistic_selector.h"

#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"

namespace openzl::thrift::tests {

ZL_Report ProbSelectorTest::compress(
        ZL_Compressor* const cgraph,
        void* dstBuff,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_GraphID graphid)
{
    ZL_CCtx* const cctx = ZL_CCtx_create();

    ZL_RET_R_IF_ERR(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    ZL_RET_R_IF_ERR(ZL_Compressor_selectStartingGraphID(cgraph, graphid));
    ZL_RET_R_IF_ERR(ZL_CCtx_refCompressor(cctx, cgraph));

    ZL_Report result =
            ZL_CCtx_compress(cctx, dstBuff, dstCapacity, src, srcSize);
    ZL_CCtx_free(cctx);
    EXPECT_EQ(ZL_isError(result), 0) << "compression failed \n";
    return result;
}

ZL_Report ProbSelectorTest::decompress(
        void* dst,
        size_t dstCapacity,
        const void* compressed,
        size_t cSize)
{
    ZL_DCtx* const dctx = ZL_DCtx_create();
    ZL_Report const result =
            ZL_DCtx_decompress(dctx, dst, dstCapacity, compressed, cSize);
    ZL_DCtx_free(dctx);
    return result;
}

void ProbSelectorTest::testRoundTrip(
        ZL_GraphID* selGraphs,
        size_t* probWeights,
        size_t nbSuccessors,
        std::vector<uint8_t>& compressSample)
{
    size_t const compressedBound = ZL_compressBound(compressSample.size());
    void* const dstBuff          = malloc(compressedBound);
    ZL_Compressor* const cgraph  = ZL_Compressor_create();
    ZL_GraphID sel               = getProbabilisticSelectorGraph(
            cgraph,
            probWeights,
            selGraphs,
            nbSuccessors,
            (ZL_Type[]){ ZL_Type_serial },
            1);

    ZL_Report const result = compress(
            cgraph,
            dstBuff,
            compressedBound,
            compressSample.data(),
            compressSample.size(),
            sel);
    ZL_Compressor_free(cgraph);
    EXPECT_EQ(ZL_isError(result), 0) << "compression failed \n";

    void* const decompressed = malloc(compressSample.size());
    ZL_Report const dresult  = decompress(
            decompressed,
            compressSample.size(),
            dstBuff,
            ZL_validResult(result));
    EXPECT_EQ(ZL_isError(dresult), 0) << "decompression failed \n";
    EXPECT_EQ(ZL_validResult(dresult), compressSample.size())
            << "Error : decompressed size != original size \n";
    EXPECT_EQ(
            std::string((char*)compressSample.data(), compressSample.size()),
            std::string((char*)decompressed, compressSample.size()))
            << "Error : decompressed content differs from original (corruption issue) !!!  \n";
    free(decompressed);
    free(dstBuff);
}

size_t ProbSelectorTest::compressWithSelector(
        ZL_GraphID* selGraphs,
        size_t* probWeights,
        size_t nbSuccessors,
        std::vector<uint8_t>& compressSample)
{
    size_t const compressedBound = ZL_compressBound(compressSample.size());
    void* const dstBuff          = malloc(compressedBound);
    ZL_Compressor* const cgraph  = ZL_Compressor_create();
    ZL_GraphID sel               = getProbabilisticSelectorGraph(
            cgraph,
            probWeights,
            selGraphs,
            nbSuccessors,
            (ZL_Type[]){ ZL_Type_serial },
            1);

    ZL_Report const result = compress(
            cgraph,
            dstBuff,
            compressedBound,
            compressSample.data(),
            compressSample.size(),
            sel);
    ZL_Compressor_free(cgraph);
    free(dstBuff);
    return ZL_validResult(result);
}

size_t ProbSelectorTest::compressWithGid(
        ZL_GraphID gid,
        std::vector<uint8_t>& compressSample)
{
    size_t const compressedBound = ZL_compressBound(compressSample.size());
    void* const dstBuff          = malloc(compressedBound);
    ZL_Compressor* const cgraph  = ZL_Compressor_create();
    ZL_Report const result       = compress(
            cgraph,
            dstBuff,
            compressedBound,
            compressSample.data(),
            compressSample.size(),
            gid);
    ZL_Compressor_free(cgraph);
    free(dstBuff);
    return ZL_validResult(result);
}
} // namespace openzl::thrift::tests
