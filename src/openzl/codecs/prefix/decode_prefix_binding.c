// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/prefix/decode_prefix_binding.h"
#include "openzl/codecs/prefix/decode_prefix_kernel.h"
#include "openzl/zl_data.h"

ZL_Report DI_prefix(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in         = ins[0];
    const ZL_Input* const matchSizes = ins[1];
    ZL_ASSERT(
            ZL_Input_type(in) == ZL_Type_string
            && ZL_Input_type(matchSizes) == ZL_Type_numeric);

    ZL_ERR_IF_NE(
            ZL_Input_numElts(matchSizes), ZL_Input_numElts(in), corruption);
    ZL_ERR_IF_NE(ZL_Input_eltWidth(matchSizes), sizeof(uint32_t), corruption);

    const uint8_t* const src            = ZL_Input_ptr(in);
    const uint32_t* const matchSizesSrc = ZL_Input_ptr(matchSizes);
    const uint32_t* const eltWidths     = ZL_Input_stringLens(in);
    size_t const eltWidthsSum           = ZL_Input_contentSize(in);
    size_t const nbElts                 = ZL_Input_numElts(in);
    ZL_ASSERT_NN(src);
    ZL_ASSERT_NN(matchSizesSrc);

    size_t const dstSize =
            ZS_calcOriginalPrefixSize(matchSizesSrc, eltWidthsSum, nbElts);
    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, dstSize, 1);
    ZL_ERR_IF_NULL(
            out,
            allocation,
            "allocation error in prefix (DI) while trying to create an output stream of size %zu",
            dstSize);
    uint32_t* const dstFieldSizes = ZL_Output_reserveStringLens(out, nbElts);
    ZL_ERR_IF_NULL(
            dstFieldSizes,
            allocation,
            "allocation error in prefix DI while trying to create an array of size %zu",
            nbElts);

    ZL_ERR_IF_ERR(ZS_decodePrefix(
            (uint8_t* const)ZL_Output_ptr(out),
            dstFieldSizes,
            src,
            nbElts,
            eltWidths,
            matchSizesSrc));
    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbElts));
    return ZL_returnSuccess();
}
