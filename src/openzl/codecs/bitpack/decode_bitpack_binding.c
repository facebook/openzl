// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitpack/decode_bitpack_binding.h"

#include "openzl/codecs/bitpack/common_bitpack_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"

ZL_Report ZL_BitpackHeader_parse(
        ZL_BitpackHeader* parsed,
        const void* headerData,
        size_t headerSize,
        size_t packedSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_LE(headerSize, 0, header_unknown, "Empty bitpack header");
    ZL_ERR_IF_GT(headerSize, 2, header_unknown, "Bitpack header too large");

    const uint8_t* header = (const uint8_t*)headerData;
    parsed->eltWidth      = (size_t)1 << ((header[0] >> 6) & 0x3);
    parsed->nbBits        = (size_t)(1 + (header[0] & 0x3F));
    ZL_ERR_IF_GT(parsed->nbBits, parsed->eltWidth * 8, corruption);

    size_t nbExtraElts = 0;
    if (headerSize > 1) {
        nbExtraElts = header[1];
    }

    const size_t maxNbElts = (packedSize * 8) / parsed->nbBits;
    ZL_ERR_IF_GT(nbExtraElts, maxNbElts, corruption, "bitpack header corrupt");
    parsed->numElts = maxNbElts - nbExtraElts;

    return ZL_returnSuccess();
}

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
    ZL_BitpackHeader bpHeader;
    ZL_ERR_IF_ERR(ZL_BitpackHeader_parse(
            &bpHeader, headerBuffer.start, headerBuffer.size, srcSize));
    if (type == ZL_Type_serial) {
        ZL_ERR_IF_NE(
                bpHeader.eltWidth,
                1,
                header_unknown,
                "Serialized has width 1!");
    }

    ZL_Output* const out = ZL_Decoder_create1OutStream(
            dictx, bpHeader.numElts, bpHeader.eltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    size_t const srcConsumed = ZS_bitpackDecode(
            ZL_Output_ptr(out),
            bpHeader.numElts,
            bpHeader.eltWidth,
            src,
            srcSize,
            (int)bpHeader.nbBits);
    ZL_ERR_IF_NE(
            srcConsumed, srcSize, corruption, "entire source not consumed");

    ZL_ERR_IF_ERR(ZL_Output_commit(out, bpHeader.numElts));

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
