// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/float_deconstruct/decode_float_deconstruct_binding.h"
#include "openzl/codecs/float_deconstruct/decode_float_deconstruct_kernel.h"

#include "openzl/codecs/float_deconstruct/common_float_deconstruct_binding.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/decompress/dictx.h"
#include "openzl/shared/mem.h"
#include "openzl/zl_dtransform.h"

ZL_Report DI_float_deconstruct(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_Input const* const signFracStream = ins[0];
    ZL_Input const* const exponentStream = ins[1];

    ZL_ASSERT_EQ(ZL_Input_type(exponentStream), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_type(signFracStream), ZL_Type_struct);

    size_t const nbElts = ZL_Input_numElts(exponentStream);
    ZL_ERR_IF_NE(nbElts, ZL_Input_numElts(signFracStream), corruption);

    FLTDECON_ElementType_e eltType = FLTDECON_ElementType_float32;
    if (DI_getFrameFormatVersion(dictx) >= 5) {
        ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);
        ZL_ERR_IF_NE(header.size, 1, corruption);
        uint8_t const* const hdr = (uint8_t const*)header.start;
        ZL_ERR_IF_GT(*hdr, FLTDECON_ElementTypeEnumMaxValue, corruption);
        eltType = (FLTDECON_ElementType_e)(*hdr);
    }

    ZL_TRY_LET_R(signFracWidth, FLTDECON_SignFracWidth(eltType));
    ZL_TRY_LET_R(exponentWidth, FLTDECON_ExponentWidth(eltType));
    ZL_ERR_IF_NE(ZL_Input_eltWidth(signFracStream), signFracWidth, corruption);
    ZL_ERR_IF_NE(ZL_Input_eltWidth(exponentStream), exponentWidth, corruption);

    ZL_TRY_LET_R(eltWidth, FLTDECON_ElementWidth(eltType));
    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, nbElts, eltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    uint8_t const* const exponent =
            (uint8_t const*)ZL_Input_ptr(exponentStream);
    uint8_t const* const signFrac =
            (uint8_t const*)ZL_Input_ptr(signFracStream);

    void* const dst = (void*)ZL_Output_ptr(out);

    switch (eltType) {
        case FLTDECON_ElementType_float32:
            FLTDECON_float32_deconstruct_decode(
                    (uint32_t*)dst, exponent, signFrac, nbElts);
            break;
        case FLTDECON_ElementType_bfloat16:
            FLTDECON_bfloat16_deconstruct_decode(
                    (uint16_t*)dst, exponent, signFrac, nbElts);
            break;
        case FLTDECON_ElementType_float16:
            FLTDECON_float16_deconstruct_decode(
                    (uint16_t*)dst, exponent, signFrac, nbElts);
            break;
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbElts));
    return ZL_returnValue(1);
}
