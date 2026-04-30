// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/quantize/decode_quantize_kernel.h"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/codecs/quantize/common_quantize.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

// Local noinline attribute for decode_quantize_kernel
#ifdef _MSC_VER
#    define ZL_QUANTIZE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#    define ZL_QUANTIZE_NOINLINE __attribute__((noinline))
#else
#    define ZL_QUANTIZE_NOINLINE
#endif

ZL_FORCE_INLINE uint32_t ZL_decode32(
        uint8_t code,
        ZS_BitDStreamFF* bitstream,
        uint32_t const* base,
        uint8_t const* bits)
{
    uint32_t const extra =
            (uint32_t)ZS_BitDStreamFF_read(bitstream, bits[code]);
    return base[code] + extra;
}

ZL_FORCE_INLINE ZL_Report ZL_quantize32Decode_impl(
        uint32_t* dst,
        uint8_t const* codes,
        size_t nbCodes,
        uint8_t const* extraBits,
        size_t extraBitsSize,
        ZL_Quantize32Params const* params,
        size_t const kUnroll)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZS_BitDStreamFF bitstream  = ZS_BitDStreamFF_init(extraBits, extraBitsSize);
    uint32_t const* const base = params->base;
    uint8_t const* const bits  = params->bits;

    size_t const preamble = nbCodes % kUnroll;
    if (preamble > 0) {
        for (size_t i = 0; i < preamble; ++i) {
            dst[i] = ZL_decode32(codes[i], &bitstream, base, bits);
        }
        ZS_BitDStreamFF_reload(&bitstream);
    }
    ZL_ASSERT_EQ((nbCodes - preamble) % kUnroll, 0);

    for (size_t i = preamble; i < nbCodes; i += kUnroll) {
#ifdef __clang__
#    pragma clang loop unroll(full)
#endif
        for (size_t u = 0; u < kUnroll; ++u) {
            dst[i + u] = ZL_decode32(codes[i + u], &bitstream, base, bits);
        }
        ZS_BitDStreamFF_reload(&bitstream);
    }

    ZL_Report const ret = ZS_BitDStreamFF_finish(&bitstream);
    ZL_ERR_IF(ZL_isError(ret), srcSize_tooSmall);

    return ZL_returnSuccess();
}

ZL_FORCE_INLINE uint32_t
ZL_decode32Pow2(uint8_t code, ZS_BitDStreamFF* bitstream)
{
    uint32_t const extra = (uint32_t)ZS_BitDStreamFF_read(bitstream, code);
    return (1u << code) + extra;
}

ZL_FORCE_INLINE ZL_Report ZL_quantize32DecodePow2_impl(
        uint32_t* dst,
        uint8_t const* codes,
        size_t nbCodes,
        uint8_t const* extraBits,
        size_t extraBitsSize,
        size_t const kUnroll)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZS_BitDStreamFF bitstream = ZS_BitDStreamFF_init(extraBits, extraBitsSize);

    size_t const preamble = nbCodes % kUnroll;
    if (preamble > 0) {
        for (size_t i = 0; i < preamble; ++i) {
            dst[i] = ZL_decode32Pow2(codes[i], &bitstream);
        }
        ZS_BitDStreamFF_reload(&bitstream);
    }
    ZL_ASSERT_EQ((nbCodes - preamble) % kUnroll, 0);

    for (size_t i = preamble; i < nbCodes; i += kUnroll) {
#ifdef __clang__
#    pragma clang loop unroll(full)
#endif
        for (size_t u = 0; u < kUnroll; ++u) {
            dst[i + u] = ZL_decode32Pow2(codes[i + u], &bitstream);
        }
        ZS_BitDStreamFF_reload(&bitstream);
    }

    ZL_Report const ret = ZS_BitDStreamFF_finish(&bitstream);
    ZL_ERR_IF(ZL_isError(ret), srcSize_tooSmall);

    return ZL_returnSuccess();
}

#define GEN_ZS2_QUANTIZE32_DECODE(kUnroll)                                \
    static ZL_QUANTIZE_NOINLINE ZL_Report ZS2_quantize32Decode_##kUnroll( \
            uint32_t* dst,                                                \
            uint8_t const* codes,                                         \
            size_t nbCodes,                                               \
            uint8_t const* bits,                                          \
            size_t bitsSize,                                              \
            ZL_Quantize32Params const* params)                            \
    {                                                                     \
        return ZL_quantize32Decode_impl(                                  \
                dst, codes, nbCodes, bits, bitsSize, params, kUnroll);    \
    }

GEN_ZS2_QUANTIZE32_DECODE(1)
GEN_ZS2_QUANTIZE32_DECODE(2)
GEN_ZS2_QUANTIZE32_DECODE(3)
GEN_ZS2_QUANTIZE32_DECODE(4)

#define GEN_ZS2_QUANTIZE32_POW2_DECODE(kUnroll)                               \
    static ZL_QUANTIZE_NOINLINE ZL_Report ZS2_quantize32DecodePow2_##kUnroll( \
            uint32_t* dst,                                                    \
            uint8_t const* codes,                                             \
            size_t nbCodes,                                                   \
            uint8_t const* bits,                                              \
            size_t bitsSize)                                                  \
    {                                                                         \
        return ZL_quantize32DecodePow2_impl(                                  \
                dst, codes, nbCodes, bits, bitsSize, kUnroll);                \
    }

GEN_ZS2_QUANTIZE32_POW2_DECODE(1)
GEN_ZS2_QUANTIZE32_POW2_DECODE(2)
GEN_ZS2_QUANTIZE32_POW2_DECODE(3)
GEN_ZS2_QUANTIZE32_POW2_DECODE(4)

static bool ZL_isPow2Code(ZL_Quantize32Params const* params)
{
    if (params->maxPow2 != 0) {
        return false;
    }
    if (params->nbCodes != 32) {
        return false;
    }
    size_t const nbCodes = 32;
    bool isPow2          = true;
    for (size_t code = 0; code < nbCodes; ++code) {
        isPow2 &= params->base[code] == (1u << code);
        isPow2 &= params->bits[code] == code;
    }
    return isPow2;
}

ZL_Report ZS2_quantize32Decode(
        uint32_t* dst,
        uint8_t const* codes,
        size_t nbCodes,
        uint8_t maxCode,
        uint8_t const* bits,
        size_t bitsSize,
        ZL_Quantize32Params const* params)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_GE(maxCode, params->nbCodes, corruption);

    size_t const maxNbBits = params->bits[maxCode];

    if (ZL_isPow2Code(params)) {
        if (maxNbBits <= 14) {
            return ZS2_quantize32DecodePow2_4(
                    dst, codes, nbCodes, bits, bitsSize);
        } else if (maxNbBits <= 19) {
            return ZS2_quantize32DecodePow2_3(
                    dst, codes, nbCodes, bits, bitsSize);
        } else if (maxNbBits <= 28) {
            return ZS2_quantize32DecodePow2_2(
                    dst, codes, nbCodes, bits, bitsSize);
        } else {
            return ZS2_quantize32DecodePow2_1(
                    dst, codes, nbCodes, bits, bitsSize);
        }
    } else {
        if (maxNbBits <= 14) {
            return ZS2_quantize32Decode_4(
                    dst, codes, nbCodes, bits, bitsSize, params);
        } else if (maxNbBits <= 19) {
            return ZS2_quantize32Decode_3(
                    dst, codes, nbCodes, bits, bitsSize, params);
        } else if (maxNbBits <= 28) {
            return ZS2_quantize32Decode_2(
                    dst, codes, nbCodes, bits, bitsSize, params);
        } else {
            return ZS2_quantize32Decode_1(
                    dst, codes, nbCodes, bits, bitsSize, params);
        }
    }
}
