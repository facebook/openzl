// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/rolz/decode_rolz_binding.h"
#include "openzl/codecs/rolz/decode_rolz_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h" // srcSize

// ZL_TypedEncoderFn
ZL_Report DI_rolz_typed(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(in), 1);
    const void* const src = ZL_Input_ptr(in);
    size_t const srcSize  = ZL_Input_numElts(in);
    ZL_ERR_IF_LT(srcSize, 4, srcSize_tooSmall);
    size_t const dstCapacity = ZL_readLE32(src);
    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, dstCapacity, 1);
    ZL_ERR_IF_NULL(out, allocation);
    void* const dst = ZL_Output_ptr(out);
    // TODO(@Cyan): it seems rolz transform still uses the older (deprecated &
    // incompatible) ZL_Report interface. To be updated
    ZL_Report const r = ZL_rolzDecompress(
            dst, dstCapacity, (const char*)src + 4, srcSize - 4);
    ZL_ERR_IF(ZL_isError(r), transform_executionFailure);
    ZL_ERR_IF_NE(
            ZL_validResult(r),
            dstCapacity,
            GENERIC,
            "corruption: wrong output size");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, dstCapacity));
    return ZL_returnValue(1);
}

// ZL_TypedEncoderFn
ZL_Report DI_fastlz_typed(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(in), 1);
    const void* const src = ZL_Input_ptr(in);
    size_t const srcSize  = ZL_Input_numElts(in);
    ZL_ERR_IF_LT(srcSize, 4, srcSize_tooSmall);
    size_t const dstCapacity = ZL_readLE32(src);
    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, dstCapacity, 1);
    ZL_ERR_IF_NULL(out, allocation);
    void* const dst = ZL_Output_ptr(out);
    // TODO(@Cyan): it seems fastlz transform still uses the older (deprecated &
    // incompatible) ZL_Report interface. To be updated
    ZL_Report const r = ZS_fastLzDecompress(
            dst, dstCapacity, (const char*)src + 4, srcSize - 4);
    ZL_ERR_IF(ZL_isError(r), GENERIC, "corruption: ZS_fastLzDecompress failed");
    ZL_ERR_IF_NE(
            ZL_validResult(r),
            dstCapacity,
            GENERIC,
            "corruption: wrong output size");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, dstCapacity));
    return ZL_returnValue(1);
}
