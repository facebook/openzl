// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/sparse_num/encode_sparse_num_binding.h"

#include "openzl/codecs/sparse_num/encode_sparse_num_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"

ZL_Report EI_sparse_num(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);

    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);

    size_t const valueWidth = ZL_Input_eltWidth(in);
    ZL_ERR_IF_NOT(
            ZL_sparseNumValidValueWidth(valueWidth),
            node_invalid_input,
            "sparse_num expects numeric width of 1, 2, 4 or 8 bytes");

    ZL_SparseNumEncodeInfo const info = ZL_sparseNumComputeEncodeInfo(
            ZL_Input_ptr(in), ZL_Input_numElts(in), valueWidth);

    ZL_Output* const distances = ZL_Encoder_createTypedStream(
            eictx, 0, info.numDistances, info.distanceWidth);
    ZL_ERR_IF_NULL(distances, allocation);
    ZL_Output* const values =
            ZL_Encoder_createTypedStream(eictx, 1, info.numValues, valueWidth);
    ZL_ERR_IF_NULL(values, allocation);

    ZL_sparseNumEncode(
            ZL_Output_ptr(distances),
            ZL_Output_ptr(values),
            ZL_Input_ptr(in),
            ZL_Input_numElts(in),
            valueWidth,
            info.distanceWidth);

    ZL_ERR_IF_ERR(ZL_Output_commit(distances, info.numDistances));
    ZL_ERR_IF_ERR(ZL_Output_commit(values, info.numValues));

    return ZL_returnSuccess();
}
