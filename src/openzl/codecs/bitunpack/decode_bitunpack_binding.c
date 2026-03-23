// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitunpack/decode_bitunpack_binding.h"

#include "openzl/codecs/bitpack/common_bitpack_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"

ZL_Report DI_bitunpack(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);
    const size_t eltWidth = ZL_Input_eltWidth(in);

    const void* src = (void const*)ZL_Input_ptr(in);
    size_t nbElts   = ZL_Input_numElts(in);

    ZL_RBuffer const headerBuffer = ZL_Decoder_getCodecHeader(dictx);
    ZL_ERR_IF_LT(headerBuffer.size, 1, header_unknown);
    ZL_ERR_IF_GT(headerBuffer.size, 2, header_unknown);
    uint8_t const nbBits = *(uint8_t const*)headerBuffer.start;
    ZL_ERR_IF_GT(nbBits, 8 * eltWidth, corruption);

    size_t const dstSize = ZS_bitpackEncodeBound(nbElts, nbBits);
    ZL_Output* const dst = ZL_Decoder_create1OutStream(dictx, dstSize, 1);
    ZL_ERR_IF_NULL(dst, allocation);

    void* dstBuffer = ZL_Output_ptr(dst);
    const size_t bytesWritten =
            ZS_bitpackEncode(dstBuffer, dstSize, src, nbElts, eltWidth, nbBits);
    ZL_ERR_IF_NE(bytesWritten, dstSize, GENERIC);

    if (headerBuffer.size == 2) {
        // We have some leftover bits
        uint8_t remBits        = ((uint8_t const*)headerBuffer.start)[1];
        const size_t remNbBits = dstSize * 8 - nbElts * nbBits;
        ZL_ASSERT_LT(remNbBits, 8);
        ZL_ERR_IF_EQ(
                remNbBits,
                0,
                corruption,
                "remNbBits is zero although trailing bits are expected");
        ZL_ERR_IF_EQ(
                dstSize,
                0,
                corruption,
                "dstSize is zero although trailing bits are expected");
        ((uint8_t*)dstBuffer)[dstSize - 1] |=
                (uint8_t)(remBits << (8 - remNbBits));
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(dst, dstSize));

    // Return the number of output streams.
    return ZL_returnValue(1);
}
