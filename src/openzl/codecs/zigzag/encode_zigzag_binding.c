// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/zigzag/encode_zigzag_binding.h"
#include "openzl/codecs/zigzag/encode_zigzag_kernel.h" // ZS_zigzagEncodeXX
#include "openzl/common/assertion.h"

// ZL_TypedEncoderFn
ZL_Report EI_zigzag_num(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);
    size_t const numWidth = ZL_Input_eltWidth(in);
    size_t const nbInts   = ZL_Input_numElts(in);
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, nbInts, numWidth);
    ZL_ERR_IF_NULL(out, allocation);
    ZL_ASSERT(numWidth == 1 || numWidth == 2 || numWidth == 4 || numWidth == 8);
    switch (numWidth) {
        case 1:
            ZL_zigzagEncode8(ZL_Output_ptr(out), ZL_Input_ptr(in), nbInts);
            break;
        case 2:
            ZL_zigzagEncode16(ZL_Output_ptr(out), ZL_Input_ptr(in), nbInts);
            break;
        case 4:
            ZL_zigzagEncode32(ZL_Output_ptr(out), ZL_Input_ptr(in), nbInts);
            break;
        case 8:
            ZL_zigzagEncode64(ZL_Output_ptr(out), ZL_Input_ptr(in), nbInts);
            break;
        default:
            ZL_ASSERT_FAIL("Unreachable");
    }
    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbInts));
    return ZL_returnValue(1);
}
