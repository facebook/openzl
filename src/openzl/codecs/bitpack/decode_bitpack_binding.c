// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitpack/decode_bitpack_binding.h"

#include "openzl/codecs/bitpack/common_bitpack_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"

static ZL_Report
DI_bitpack_typed(ZL_Decoder* dictx, const ZL_Input* ins[], ZL_Type type)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(in), 1);
    const uint8_t* src = (uint8_t const*)ZL_Input_ptr(in);
    size_t srcSize     = ZL_Input_numElts(in);

    ZL_RBuffer const headerBuffer = ZL_Decoder_getCodecHeader(dictx);
    ZL_ERR_IF_GT(headerBuffer.size, 2, header_unknown);
    ZL_ERR_IF_LE(headerBuffer.size, 0, header_unknown);
    uint8_t const header     = *(uint8_t const*)headerBuffer.start;
    bool const hasExtraSpace = headerBuffer.size > 1;
    size_t const dstEltWidth = (size_t)1 << ((header >> 6) & 0x3);
    int const nbBits         = 1 + (header & 0x3F);

    ZL_ERR_IF_GT((size_t)nbBits, dstEltWidth * 8, internalBuffer_tooSmall);
    ZL_ERR_IF_LE(nbBits, 0, header_unknown);
    if (type == ZL_Type_serial) {
        ZL_ERR_IF_NE(dstEltWidth, 1, header_unknown, "Serialized has width 1!");
    }

    size_t dstNbElts;
    if (hasExtraSpace) {
        size_t const maxNbElts    = (srcSize * 8) / (size_t)nbBits;
        uint8_t const nbExtraElts = ((uint8_t const*)headerBuffer.start)[1];
        ZL_ERR_IF_GT(
                nbExtraElts, maxNbElts, corruption, "bitpack header corrupt");
        dstNbElts = maxNbElts - nbExtraElts;
    } else {
        dstNbElts = (srcSize * 8) / (size_t)nbBits;
    }
    ZL_Output* const out =
            ZL_Decoder_create1OutStream(dictx, dstNbElts, dstEltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    size_t const srcConsumed = ZS_bitpackDecode(
            ZL_Output_ptr(out), dstNbElts, dstEltWidth, src, srcSize, nbBits);
    ZL_ERR_IF_NE(
            srcConsumed, srcSize, corruption, "entire source not consumed");

    ZL_ERR_IF_ERR(ZL_Output_commit(out, dstNbElts));

    // Return the number of output streams.
    return ZL_returnValue(1);
}

ZL_Report DI_bitpack_numeric(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    return DI_bitpack_typed(dictx, ins, ZL_Type_numeric);
}

ZL_Report DI_bitpack_serialized(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    return DI_bitpack_typed(dictx, ins, ZL_Type_serial);
}
