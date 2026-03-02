// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/parse_int/decode_parse_int_binding.h"
#include "openzl/codecs/parse_int/common_parse_int.h"
#include "openzl/codecs/parse_int/decode_parse_int_kernel.h"
#include "openzl/shared/overflow.h"

#include "openzl/common/assertion.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"

ZL_Report DI_parseInt(ZL_Decoder* decoder, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(decoder);
    ZL_Input const* numbers = ins[0];
    const size_t eltWidth   = ZL_Input_eltWidth(numbers);
    ZL_ERR_IF(eltWidth != 8, node_invalid_input);
    size_t const nbElts = ZL_Input_numElts(numbers);
    size_t outBound;
    ZL_ERR_IF(
            ZL_overflowMulST(nbElts, ZL_PARSE_INT_MAX_STRING_LENGTH, &outBound),
            allocation);
    ZL_Output* outStream =
            ZL_Decoder_create1StringStream(decoder, nbElts, outBound);
    ZL_ERR_IF_NULL(outStream, allocation);
    uint32_t* fieldSizes = ZL_Output_stringLens(outStream);
    ZL_ERR_IF_NULL(fieldSizes, allocation);
    int64_t const* nums = ZL_Input_ptr(numbers);
    size_t outSize = ZL_DecodeParseInt_fillFieldSizes(fieldSizes, nbElts, nums);
    ZL_ASSERT_LE(outSize, outBound);
    ZL_DecodeParseInt_fillContent(
            ZL_Output_ptr(outStream), outSize, nbElts, nums, fieldSizes);
    ZL_ERR_IF_ERR(ZL_Output_commit(outStream, nbElts));
    return ZL_returnSuccess();
}
