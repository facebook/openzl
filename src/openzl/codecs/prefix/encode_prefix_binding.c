// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/prefix/encode_prefix_binding.h"
#include "openzl/codecs/prefix/encode_prefix_kernel.h"

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

ZL_Report EI_prefix(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_string);

    const uint8_t* const src        = ZL_Input_ptr(in);
    size_t const nbElts             = ZL_Input_numElts(in);
    const uint32_t* const eltWidths = ZL_Input_stringLens(in);
    size_t const fieldSizesSum      = ZL_Input_contentSize(in);
    ZL_ASSERT_NN(src);

    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, fieldSizesSum, 1);
    ZL_ERR_IF_NULL(
            out,
            allocation,
            "allocation error in prefix while trying to create an output stream of size %zu",
            fieldSizesSum);
    uint32_t* const fieldSizes = ZL_Output_reserveStringLens(out, nbElts);
    ZL_ERR_IF_NULL(
            fieldSizes,
            allocation,
            "allocation error in prefix while trying to create a field size array of size %zu",
            nbElts);
    ZL_Output* const matchSizes =
            ZL_Encoder_createTypedStream(eictx, 1, nbElts, sizeof(uint32_t));
    ZL_ERR_IF_NULL(
            matchSizes,
            allocation,
            "allocation error in prefix while trying to create an output stream of size %zu",
            nbElts);

    ZS_encodePrefix(
            (uint8_t* const)ZL_Output_ptr(out),
            fieldSizes,
            (uint32_t* const)ZL_Output_ptr(matchSizes),
            src,
            nbElts,
            eltWidths,
            fieldSizesSum);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbElts));
    ZL_ERR_IF_ERR(ZL_Output_commit(matchSizes, nbElts));
    return ZL_returnSuccess();
}
