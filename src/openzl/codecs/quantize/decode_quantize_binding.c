// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/quantize/decode_quantize_binding.h"

#include "openzl/codecs/quantize/common_quantize.h"
#include "openzl/codecs/quantize/decode_quantize_kernel.h"
#include "openzl/common/debug.h"
#include "openzl/zl_dtransform.h"

static uint8_t ZL_maxCode(ZL_Input const* in)
{
    uint8_t maxCode      = 0;
    uint8_t const* codes = (uint8_t const*)ZL_Input_ptr(in);
    size_t const nbCodes = ZL_Input_numElts(in);
    for (size_t i = 0; i < nbCodes; ++i) {
        if (codes[i] > maxCode) {
            maxCode = codes[i];
        }
    }
    return maxCode;
}

static ZL_Report DI_quantize(
        ZL_Decoder* dictx,
        const ZL_Input* ins[],
        ZL_Quantize32Params const* params)
{
    ZL_Input const* const codes = ins[0];
    ZL_Input const* const bits  = ins[1];

    ZL_RET_R_IF_NE(corruption, ZL_Input_eltWidth(codes), 1, "Unsupported");
    ZL_ASSERT_EQ(ZL_Input_type(bits), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(codes), 1);

    size_t const nbCodes = ZL_Input_numElts(codes);

    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, nbCodes, 4);
    ZL_RET_R_IF_NULL(allocation, out);

    // TODO(terrelln): Get this information from the stream metadata
    // if it is available.
    uint8_t maxCode = ZL_maxCode(codes);

    ZL_Report const ret = ZL_quantize32Decode(
            (uint32_t*)ZL_Output_ptr(out),
            (uint8_t const*)ZL_Input_ptr(codes),
            nbCodes,
            maxCode,
            (uint8_t const*)ZL_Input_ptr(bits),
            ZL_Input_numElts(bits),
            params);
    if (ZL_isError(ret)) {
        return ret;
    }

    ZL_RET_R_IF_ERR(ZL_Output_commit(out, nbCodes));

    return ZL_returnValue(1);
}

ZL_Report DI_quantizeOffsets(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    return DI_quantize(dictx, ins, &ZL_quantizeOffsetsParams);
}

ZL_Report DI_quantizeLengths(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    return DI_quantize(dictx, ins, &ZL_quantizeLengthsParams);
}
