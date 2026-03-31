// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/float_deconstruct/encode_float_deconstruct_binding.h"
#include "openzl/codecs/float_deconstruct/common_float_deconstruct_binding.h"
#include "openzl/codecs/float_deconstruct/encode_float_deconstruct_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/common/debug.h"
#include "openzl/zl_errors.h"

ZL_INLINE_KEYWORD ZL_Report float_deconstruct(
        ZL_Encoder* eictx,
        const ZL_Input* in,
        FLTDECON_ElementType_e eltType)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ERR_IF_GT(eltType, FLTDECON_ElementTypeEnumMaxValue, logicError);
    ZL_TRY_LET(size_t, expectedEltWidth, FLTDECON_ElementWidth(eltType));
    ZL_ERR_IF_NE(
            ZL_Input_eltWidth(in), expectedEltWidth, streamParameter_invalid);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);

    size_t const nbElts = ZL_Input_numElts(in);

    if (ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion) >= 5) {
        // We have to guard against the possibility that sizeof(eltType) > 1
        // Otherwise, we might send the wrong header on big-endian systems
        uint8_t safeEltType = (uint8_t)eltType;
        ZL_Encoder_sendCodecHeader(eictx, &safeEltType, sizeof(safeEltType));
    } else {
        ZL_ERR_IF_NE(eltType, FLTDECON_ElementType_float32, logicError);
    }

    ZL_TRY_LET(size_t, signFracWidth, FLTDECON_SignFracWidth(eltType));
    ZL_Output* signFracStream =
            ZL_Encoder_createTypedStream(eictx, 0, nbElts, signFracWidth);

    ZL_TRY_LET(size_t, exponentWidth, FLTDECON_ExponentWidth(eltType));
    ZL_Output* exponentStream =
            ZL_Encoder_createTypedStream(eictx, 1, nbElts, exponentWidth);

    if (signFracStream == NULL || exponentStream == NULL) {
        ZL_ERR(allocation);
    }

    uint8_t* const exponent = (uint8_t*)ZL_Output_ptr(exponentStream);
    uint8_t* const signFrac = (uint8_t*)ZL_Output_ptr(signFracStream);

    void const* const src = (void const*)ZL_Input_ptr(in);

    switch (eltType) {
        case FLTDECON_ElementType_float32:
            FLTDECON_float32_deconstruct_encode(
                    (uint32_t const*)src, exponent, signFrac, nbElts);
            break;
        case FLTDECON_ElementType_bfloat16:
            FLTDECON_bfloat16_deconstruct_encode(
                    (uint16_t const*)src, exponent, signFrac, nbElts);
            break;
        case FLTDECON_ElementType_float16:
            FLTDECON_float16_deconstruct_encode(
                    (uint16_t const*)src, exponent, signFrac, nbElts);
            break;
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(exponentStream, nbElts));
    ZL_ERR_IF_ERR(ZL_Output_commit(signFracStream, nbElts));

    return ZL_returnValue(2);
}

ZL_Report
EI_float32_deconstruct(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return float_deconstruct(eictx, in, FLTDECON_ElementType_float32);
}

ZL_Report
EI_bfloat16_deconstruct(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return float_deconstruct(eictx, in, FLTDECON_ElementType_bfloat16);
}

ZL_Report
EI_float16_deconstruct(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return float_deconstruct(eictx, in, FLTDECON_ElementType_float16);
}
