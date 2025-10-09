// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/quantize/encode_quantize_binding.h"

#include "openzl/codecs/quantize/common_quantize.h"
#include "openzl/codecs/quantize/encode_quantize_kernel.h"
#include "openzl/common/debug.h"
#include "openzl/common/errors_internal.h" // ZL_RET_IF*
#include "openzl/zl_ctransform.h"

static ZL_Report EI_quantize(
        ZL_Encoder* eictx,
        const ZL_Input* in,
        ZL_Quantize32Params const* params)
{
    ZL_RET_R_IF_NE(GENERIC, ZL_Input_type(in), ZL_Type_numeric);
    ZL_RET_R_IF_NE(GENERIC, ZL_Input_eltWidth(in), 4);

    size_t const nbElts = ZL_Input_numElts(in);

    ZL_Output* codes = ZL_Encoder_createTypedStream(eictx, 0, nbElts, 1);
    // TODO(terrelln): Bound this allocation tighter
    size_t const bitsCapacity = 4 * nbElts + 9;
    ZL_Output* bits = ZL_Encoder_createTypedStream(eictx, 1, bitsCapacity, 1);

    ZL_RET_R_IF_NULL(allocation, codes);
    ZL_RET_R_IF_NULL(allocation, bits);

    ZL_Report const bitsSize = ZL_quantize32Encode(
            (uint8_t*)ZL_Output_ptr(bits),
            bitsCapacity,
            (uint8_t*)ZL_Output_ptr(codes),
            (uint32_t const*)ZL_Input_ptr(in),
            nbElts,
            params);
    ZL_RET_R_IF_ERR(bitsSize);

    ZL_RET_R_IF_ERR(ZL_Output_commit(codes, nbElts));
    ZL_RET_R_IF_ERR(ZL_Output_commit(bits, ZL_validResult(bitsSize)));

    return ZL_returnValue(2);
}

ZL_Report
EI_quantizeOffsets(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return EI_quantize(eictx, in, &ZL_quantizeOffsetsParams);
}

ZL_Report
EI_quantizeLengths(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return EI_quantize(eictx, in, &ZL_quantizeLengthsParams);
}
