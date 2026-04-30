// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitSplit/encode_bitsplit_bf16_binding.h"
#include "openzl/codecs/bitSplit/encode_bitSplit_binding.h" /* EI_bitSplit_withWidths */
#include "openzl/common/assertion.h"                        /* ZL_ASSERT */
#include "openzl/zl_data.h"                                 /* ZL_Input_type */
#include "openzl/zl_errors.h"                               /* ZL_ERR_IF_NOT */
#include "openzl/zl_input.h" /* ZL_Input_ptr, ZL_Input_eltWidth, ZL_Input_numElts */

ZL_Report
EI_bitsplit_bf16(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);
    size_t const eltWidth = ZL_Input_eltWidth(in);
    ZL_ERR_IF_NOT(
            eltWidth == 2,
            node_invalid_input,
            "bitsplit_bf16 requires 2-byte elements (bfloat16)");

    // bfloat16: [mantissa:7][exponent:8][sign:1] = 16 bits
    uint8_t bitWidths[3];
    bitWidths[0] = 7; // mantissa bits (LSB)
    bitWidths[1] = 8; // exponent bits
    bitWidths[2] = 1; // sign bit     (MSB)

    return EI_bitSplit_withWidths(eictx, in, bitWidths, 3);
}
