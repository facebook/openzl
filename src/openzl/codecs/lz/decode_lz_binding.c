// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/lz/decode_lz_binding.h"

#include "openzl/codecs/lz/common_field_lz.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"

ZL_Report DI_fieldLz(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_Input const* const literals            = ins[0];
    ZL_Input const* const tokens              = ins[1];
    ZL_Input const* const offsets             = ins[2];
    ZL_Input const* const extraLiteralLengths = ins[3];
    ZL_Input const* const extraMatchLengths   = ins[4];

    ZL_ASSERT_NN(ins[0]);
    ZL_ASSERT_NN(ins[1]);
    ZL_ASSERT_NN(ins[2]);
    ZL_ASSERT_NN(ins[3]);
    ZL_ASSERT_NN(ins[4]);

    size_t const eltWidth = ZL_Input_eltWidth(literals);
    ZL_RET_R_IF_NOT(corruption, ZL_isPow2(eltWidth));

    ZL_RET_R_IF_NE(corruption, 2, ZL_Input_eltWidth(tokens));
    ZL_RET_R_IF_NE(corruption, 4, ZL_Input_eltWidth(offsets));
    ZL_RET_R_IF_NE(corruption, 4, ZL_Input_eltWidth(extraLiteralLengths));
    ZL_RET_R_IF_NE(corruption, 4, ZL_Input_eltWidth(extraMatchLengths));

    ZL_RET_R_IF_NE(
            corruption,
            ZL_Input_eltWidth(tokens),
            2,
            "FieldLz tokens should be 2 bytes width");
    ZL_RET_R_IF_NE(
            corruption,
            ZL_Input_eltWidth(offsets),
            4,
            "FieldLz offsets should be 4 bytes width");
    ZL_RET_R_IF_NE(
            corruption,
            ZL_Input_eltWidth(extraLiteralLengths),
            4,
            "FieldLz extraLiteralLengths should be 4 bytes width");
    ZL_RET_R_IF_NE(
            corruption,
            ZL_Input_eltWidth(extraMatchLengths),
            4,
            "FieldLz extraMatchLengths should be 4 bytes width");

    ZL_FieldLz_InSequences src = {
        .literalElts   = ZL_Input_ptr(literals),
        .nbLiteralElts = ZL_Input_numElts(literals),

        .tokens   = (uint16_t const*)ZL_Input_ptr(tokens),
        .nbTokens = ZL_Input_numElts(tokens),

        .offsets   = (uint32_t const*)ZL_Input_ptr(offsets),
        .nbOffsets = ZL_Input_numElts(offsets),

        .extraLiteralLengths =
                (uint32_t const*)ZL_Input_ptr(extraLiteralLengths),
        .nbExtraLiteralLengths = ZL_Input_numElts(extraLiteralLengths),

        .extraMatchLengths   = (uint32_t const*)ZL_Input_ptr(extraMatchLengths),
        .nbExtraMatchLengths = ZL_Input_numElts(extraMatchLengths),
    };

    uint64_t dstEltsCapacity = 0;
    {
        ZL_RBuffer const header        = ZL_Decoder_getCodecHeader(dictx);
        uint8_t const* hdr             = (uint8_t const*)header.start;
        uint8_t const* end             = hdr + header.size;
        ZL_RESULT_OF(uint64_t) const r = ZL_varintDecode(&hdr, end);
        if (ZL_RES_isError(r)) {
            ZL_DLOG(ERROR, "header decoding failed");
            ZL_RET_R_ERR(srcSize_tooSmall);
        }
        if (hdr < end) {
            ZL_DLOG(ERROR, "header leftover bytes");
            ZL_RET_R_ERR(GENERIC);
        }
        dstEltsCapacity = ZL_RES_value(r);
    }

    ZL_Output* dst =
            ZL_Decoder_create1OutStream(dictx, dstEltsCapacity, eltWidth);
    ZL_RET_R_IF_NULL(allocation, dst);

    ZL_Report const dstSize = ZL_FieldLz_decompress(
            ZL_Output_ptr(dst), dstEltsCapacity, eltWidth, &src);
    if (ZL_isError(dstSize)) {
        return dstSize;
    }

    ZL_RET_R_IF_ERR(ZL_Output_commit(dst, ZL_validResult(dstSize)));

    return ZL_returnValue(1);
}
