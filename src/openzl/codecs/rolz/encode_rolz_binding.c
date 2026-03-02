// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <limits.h>

#include "openzl/codecs/rolz/encode_rolz_binding.h"
#include "openzl/codecs/rolz/encode_rolz_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h" // writeLE32

// ZL_TypedEncoderFn
ZL_Report EI_rolz_typed(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(in), 1);
    const void* const src    = ZL_Input_ptr(in);
    size_t const srcSize     = ZL_Input_numElts(in);
    size_t const dstCapacity = EI_rolz_dstBound(src, srcSize);
    ZL_ASSERT_GE(dstCapacity, 4);
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, dstCapacity, 1);
    ZL_ERR_IF_NULL(out, allocation);
    void* const dst = ZL_Output_ptr(out);
    ZL_ASSERT_LT(srcSize, INT_MAX);
    ZL_writeLE32(dst, (uint32_t)srcSize);

    // TODO(@cyan) : encoder_rolz.h still uses the older (incompatible and
    // deprecated) ZL_Report API To be updated
    ZL_Report const r =
            ZS_rolzCompress((char*)dst + 4, dstCapacity - 4, src, srcSize);
    ZL_ERR_IF(ZL_isError(r), transform_executionFailure);

    ZL_ERR_IF_ERR(ZL_Output_commit(out, ZL_validResult(r) + 4));
    return ZL_returnValue(1);
}

// ZL_TypedEncoderFn
ZL_Report
EI_fastlz_typed(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(in), 1);
    const void* const src    = ZL_Input_ptr(in);
    size_t const srcSize     = ZL_Input_numElts(in);
    size_t const dstCapacity = EI_fastlz_dstBound(src, srcSize);
    ZL_ASSERT_GE(dstCapacity, 4);
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, dstCapacity, 1);
    ZL_ERR_IF_NULL(out, allocation);
    void* const dst = ZL_Output_ptr(out);
    ZL_ASSERT_LT(srcSize, INT_MAX);
    ZL_writeLE32(dst, (uint32_t)srcSize);

    // TODO(@cyan) : encoder_rolz.h still uses the older (incompatible and
    // deprecated) ZL_Report API To be updated
    ZL_Report const r =
            ZS_fastLzCompress((char*)dst + 4, dstCapacity - 4, src, srcSize);
    ZL_ERR_IF(ZL_isError(r), transform_executionFailure);

    ZL_ERR_IF_ERR(ZL_Output_commit(out, ZL_validResult(r) + 4));
    return ZL_returnValue(1);
}

/* ===============================================
 * Legacy Encoder Interfaces for Delta transforms
 * using the pipeTransform model
 * (no longer used)
 * ===============================================
 */

// ZL_PipeDstCapacityFn
size_t EI_rolz_dstBound(const void* src, size_t srcSize)
{
    (void)src;
    return ZS_rolzCompressBound(srcSize) + 4;
}

// ZL_PipeEncoderFn
size_t EI_rolz(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    if (srcSize > 0) {
        ZL_ASSERT_NN(src);
    }
    if (dstCapacity > 0) {
        ZL_ASSERT_NN(dst);
    }
    ZL_ASSERT_GE(dstCapacity, EI_rolz_dstBound(src, srcSize));

    ZL_ASSERT_GE(dstCapacity, 4);
    ZL_ASSERT_LT(srcSize, INT_MAX);
    ZL_writeLE32(dst, (uint32_t)srcSize);

    ZL_Report const r =
            ZS_rolzCompress((char*)dst + 4, dstCapacity - 4, src, srcSize);
    ZL_ASSERT(!ZL_isError(r));

    return ZL_validResult(r) + 4;
}

// ZL_PipeDstCapacityFn
size_t EI_fastlz_dstBound(const void* src, size_t srcSize)
{
    (void)src;
    return ZS_fastLzCompressBound(srcSize) + 4;
}

// ZL_PipeEncoderFn
size_t EI_fastlz(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    if (srcSize > 0) {
        ZL_ASSERT_NN(src);
    }
    if (dstCapacity > 0) {
        ZL_ASSERT_NN(dst);
    }
    ZL_ASSERT_GE(dstCapacity, EI_fastlz_dstBound(src, srcSize));

    ZL_ASSERT_GE(dstCapacity, 4);
    ZL_ASSERT_LT(srcSize, INT_MAX);
    ZL_writeLE32(dst, (uint32_t)srcSize);

    ZL_Report const r =
            ZS_fastLzCompress((char*)dst + 4, dstCapacity - 4, src, srcSize);
    ZL_ASSERT(!ZL_isError(r));

    return ZL_validResult(r) + 4;
}
