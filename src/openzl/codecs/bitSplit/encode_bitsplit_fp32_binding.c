// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitSplit/encode_bitsplit_fp32_binding.h"
#include "openzl/codecs/bitSplit/encode_bitSplit_binding.h" /* EI_bitSplit_withWidths */
#include "openzl/common/assertion.h"                        /* ZL_ASSERT */
#include "openzl/zl_data.h"                                 /* ZL_Input_type */
#include "openzl/zl_errors.h"                               /* ZL_RET_R_IF_NE */
#include "openzl/zl_input.h" /* ZL_Input_ptr, ZL_Input_eltWidth, ZL_Input_numElts */

ZL_Report
EI_bitsplit_fp32(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);
    ZL_RET_R_IF_NE(
            node_invalid_input,
            ZL_Input_eltWidth(in),
            4,
            "bitsplit_fp32 requires 4-byte (float32) elements");

    uint8_t bitWidths[3] = { 23, 8, 1 };

    return EI_bitSplit_withWidths(eictx, in, bitWidths, 3);
}
