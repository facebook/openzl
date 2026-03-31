// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/quantize/encode_quantize_kernel.h"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_errors.h"

static uint8_t ZL_code32(uint32_t value, ZL_Quantize32Params const* params)
{
    if (value >= params->maxPow2) {
        uint32_t const code = (uint32_t)ZL_highbit32(value) + params->delta;
        ZL_ASSERT_LT(code, 256);
        return (uint8_t)code;
    }
    return params->valueToCode[value];
}

ZL_Report ZS2_quantize32Encode(
        uint8_t* bits,
        size_t bitsCapacity,
        uint8_t* codes,
        uint32_t const* src,
        size_t srcSize,
        ZL_Quantize32Params const* params)
{
    ZL_ASSERT(ZL_isPow2(params->maxPow2));
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    ZS_BitCStreamFF bitstream = ZS_BitCStreamFF_init(bits, bitsCapacity);
    for (size_t i = 0; i < srcSize; ++i) {
        uint32_t const value = src[i];
        if (params->maxPow2 == 0 && value == 0) {
            ZL_ERR(GENERIC);
        }
        uint8_t const code = ZL_code32(value, params);
        codes[i]           = code;
        ZS_BitCStreamFF_write(&bitstream, value, params->bits[code]);
        ZS_BitCStreamFF_flush(&bitstream);
    }
    ZL_Report const ret = ZS_BitCStreamFF_finish(&bitstream);
    ZL_ERR_IF(ZL_isError(ret), internalBuffer_tooSmall);
    return ret;
}
