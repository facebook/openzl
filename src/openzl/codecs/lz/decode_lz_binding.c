// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/lz/decode_lz_binding.h"

#include "openzl/codecs/lz/common_field_lz.h"
#include "openzl/codecs/lz/decode_lz_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"

ZL_Report DI_fieldLz(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
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
    ZL_ERR_IF_NOT(ZL_isPow2(eltWidth), corruption);

    ZL_ERR_IF_NE(2, ZL_Input_eltWidth(tokens), corruption);
    ZL_ERR_IF_NE(4, ZL_Input_eltWidth(offsets), corruption);
    ZL_ERR_IF_NE(4, ZL_Input_eltWidth(extraLiteralLengths), corruption);
    ZL_ERR_IF_NE(4, ZL_Input_eltWidth(extraMatchLengths), corruption);

    ZL_ERR_IF_NE(
            ZL_Input_eltWidth(tokens),
            2,
            corruption,
            "FieldLz tokens should be 2 bytes width");
    ZL_ERR_IF_NE(
            ZL_Input_eltWidth(offsets),
            4,
            corruption,
            "FieldLz offsets should be 4 bytes width");
    ZL_ERR_IF_NE(
            ZL_Input_eltWidth(extraLiteralLengths),
            4,
            corruption,
            "FieldLz extraLiteralLengths should be 4 bytes width");
    ZL_ERR_IF_NE(
            ZL_Input_eltWidth(extraMatchLengths),
            4,
            corruption,
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
            ZL_ERR(srcSize_tooSmall);
        }
        if (hdr < end) {
            ZL_DLOG(ERROR, "header leftover bytes");
            ZL_ERR(GENERIC);
        }
        dstEltsCapacity = ZL_RES_value(r);
    }

    ZL_Output* dst =
            ZL_Decoder_create1OutStream(dictx, dstEltsCapacity, eltWidth);
    ZL_ERR_IF_NULL(dst, allocation);

    ZL_Report const dstSize = ZS2_FieldLz_decompress(
            ZL_Output_ptr(dst), dstEltsCapacity, eltWidth, &src);
    if (ZL_isError(dstSize)) {
        return dstSize;
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(dst, ZL_validResult(dstSize)));

    return ZL_returnValue(1);
}

ZL_Report DI_lz(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);

    ZL_Input const* const literals    = ins[0];
    ZL_Input const* const offsets     = ins[1];
    ZL_Input const* const literalLens = ins[2];
    ZL_Input const* const matchLens   = ins[3];

    size_t const offsetsEltWidth     = ZL_Input_eltWidth(offsets);
    size_t const literalLensEltWidth = ZL_Input_eltWidth(literalLens);
    size_t const matchLensEltWidth   = ZL_Input_eltWidth(matchLens);

    size_t const numSequences = ZL_Input_numElts(offsets);
    ZL_ERR_IF_NE(
            numSequences,
            ZL_Input_numElts(literalLens),
            corruption,
            "LZ: offsets and literal_lengths must have same count");
    ZL_ERR_IF_NE(
            numSequences,
            ZL_Input_numElts(matchLens),
            corruption,
            "LZ: offsets and match_lengths must have same count");

    ZL_RBuffer const header        = ZL_Decoder_getCodecHeader(dictx);
    uint8_t const* headerStart     = (uint8_t const*)header.start;
    uint8_t const* const headerEnd = headerStart + header.size;
    ZL_TRY_LET_CONST(
            uint64_t, dstSize, ZL_varintDecode(&headerStart, headerEnd));
    ZL_ERR_IF_NE(
            headerStart, headerEnd, corruption, "LZ: header leftover bytes");

    ZL_Output* dst = ZL_Decoder_create1OutStream(dictx, dstSize, 1);
    ZL_ERR_IF_NULL(dst, allocation);

    ZL_Lz_InSequences const src = {
        .literals               = (const uint8_t*)ZL_Input_ptr(literals),
        .numLiterals            = ZL_Input_numElts(literals),
        .offsets                = ZL_Input_ptr(offsets),
        .offsetsEltWidth        = offsetsEltWidth,
        .literalLengths         = ZL_Input_ptr(literalLens),
        .literalLengthsEltWidth = literalLensEltWidth,
        .matchLengths           = ZL_Input_ptr(matchLens),
        .matchLengthsEltWidth   = matchLensEltWidth,
        .numSequences           = numSequences,
    };
    ZL_LzError const err =
            ZL_Lz_decode((uint8_t*)ZL_Output_ptr(dst), (size_t)dstSize, &src);
    ZL_ERR_IF_NE(err, ZL_LzError_ok, corruption, "LZ decode failed");

    ZL_ERR_IF_ERR(ZL_Output_commit(dst, (size_t)dstSize));

    return ZL_returnSuccess();
}
