// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitpack/common_bitpack_kernel.h"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h" // ZL_writeLE64
#include "openzl/shared/portability.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_errors.h"

#define ZS_HAS_FAST_BITPACK (ZL_HAS_AVX2 && ZL_HAS_BMI2)

#if ZS_HAS_FAST_BITPACK
#    include <immintrin.h>
#endif

/*
 * ==================
 * ENCODE FUNCTIONS
 * ==================
 */
size_t ZS_bitpackEncodeBound(size_t nbElts, int nbBits)
{
    ZL_ASSERT_GE(nbBits, 0);
    ZL_ASSERT_LE(nbBits, 64);
    ZL_ASSERT_LT(nbElts, UINT64_MAX / 64);
    return (((nbElts * (size_t)nbBits) + 7) / 8);
}

#define DECLARE_VERIFY_FUNCTION(size)                             \
    static int ZS_bitpackEncodeVerify##size(                      \
            const uint##size##_t* src, size_t nbElts, int nbBits) \
    {                                                             \
        ZL_ASSERT_LE((size_t)nbBits, 8 * sizeof(*src));           \
        if (nbBits == 8 * sizeof(*src)) {                         \
            return 1;                                             \
        }                                                         \
        uint##size##_t onBits = 0;                                \
        for (size_t i = 0; i < nbElts; i++) {                     \
            onBits |= src[i];                                     \
        }                                                         \
        if (onBits >> nbBits) {                                   \
            return 0;                                             \
        }                                                         \
        return 1;                                                 \
    }

DECLARE_VERIFY_FUNCTION(8)
DECLARE_VERIFY_FUNCTION(16)
DECLARE_VERIFY_FUNCTION(32)
DECLARE_VERIFY_FUNCTION(64)

int ZS_bitpackEncodeVerify(
        const void* src,
        size_t nbElts,
        size_t eltWidth,
        int nbBits)
{
    switch (eltWidth) {
        case 1:
            return ZS_bitpackEncodeVerify8(src, nbElts, nbBits);
        case 2:
            return ZS_bitpackEncodeVerify16(src, nbElts, nbBits);
        case 4:
            return ZS_bitpackEncodeVerify32(src, nbElts, nbBits);
        case 8:
            return ZS_bitpackEncodeVerify64(src, nbElts, nbBits);
        default:
            ZL_ASSERT_FAIL("Bad eltWidth %u!", (unsigned)eltWidth);
            return 0;
    }
}

/// Detect & handle edge cases - shared code for each width
static size_t ZS_bitpackEncodeEdgeCase(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t nbElts,
        int eltSize,
        int nbBits)
{
    if (nbElts == 0 || nbBits == 0)
        return 0;

    ZL_ASSERT_GT(nbBits, 0);
    ZL_ASSERT_GE(dstCapacity, ZS_bitpackEncodeBound(nbElts, nbBits));

    ZL_ASSERT_NN(src);
    ZL_ASSERT_NN(dst);

    if (nbBits == eltSize * 8) {
        ZL_ASSERT(ZL_isLittleEndian());
        size_t const dstSize = nbElts * (size_t)eltSize;
        ZL_memcpy(dst, src, dstSize);
        return dstSize;
    }

    ZL_ASSERT_LT(nbBits, eltSize * 8);

    return (size_t)-1;
}

// TODO 1 (@Cyan): more generalized version, allowing @nbBits > 64
// TODO 2 (@Cyan): more variants, @dst arrays of 8 & 16 bits
// TODO 3 (@Cyan): speed optimization using Vector instructions (?)
static size_t bit1pack32(
        void* dst,
        size_t dstCapacity,
        const uint32_t* src32,
        size_t nb1BitValues)
{
    ZL_ASSERT_LE(nb1BitValues, 64);
    uint64_t acc = 0;
    for (size_t rn = 0; rn < nb1BitValues; rn++) {
        acc <<= 1;
        acc += src32[nb1BitValues - 1 - rn];
    }
    size_t const dstSize = (nb1BitValues + 7) / 8;
    if (dstCapacity >= 8) {
        ZL_writeLE64(dst, acc);
    } else {
        ZL_writeLE64_N(dst, acc, dstSize);
    }
    return dstSize;
}

// TODO: Optimize
static size_t ZS_bitpackEncode32_generic(
        void* dst,
        const uint32_t* src32,
        size_t nbVal32,
        size_t nbBits)
{
    ZL_DLOG(BLOCK, "bitNpack32, %zu elts, using %i bits each", nbVal32, nbBits);
    size_t const dstSize      = (nbVal32 * nbBits + 7) / 8;
    uint8_t* dstPtr           = dst;
    uint8_t* const dstEnd     = dstPtr + dstSize;
    uint8_t* const dstLimit   = dstEnd - 7;
    uint64_t acc64            = 0;
    size_t const nbPacks      = (size_t)(56 / nbBits);
    size_t const nbFullRounds = nbVal32 / nbPacks;
    size_t valNb              = 0;
    int bitPos                = 0;
    for (size_t nr = 0; nr < nbFullRounds && dstPtr < dstLimit; nr++) {
        for (size_t n = 0; n < nbPacks; n++) {
            uint64_t const newVal = src32[valNb++];
            acc64 |= newVal << bitPos;
            bitPos += (int)nbBits;
        }
        ZL_ASSERT_GE(MEM_ptrDistance(dstPtr, dstEnd), 8);
        ZL_writeLE64(dstPtr, acc64);
        int const nbBytesCommitted = bitPos / 8;
        dstPtr += nbBytesCommitted;
        acc64 >>= nbBytesCommitted * 8;
        bitPos &= 7;
    }
    // last round, non full
    int const bitLimit = 63 - (int)nbBits;
    for (; valNb < nbVal32; valNb++) {
        uint64_t const newVal = (uint64_t)src32[valNb] << bitPos;
        acc64 |= newVal;
        bitPos += (int)nbBits;
        if (bitPos > bitLimit) {
            int const nbBytesCommitted = bitPos / 8;
            if (MEM_ptrDistance(dstPtr, dstEnd) >= 8) {
                ZL_writeLE64(dstPtr, acc64);
            } else {
                ZL_writeLE64_N(dstPtr, acc64, (size_t)nbBytesCommitted);
            }
            dstPtr += nbBytesCommitted;
            acc64 >>= nbBytesCommitted * 8;
            bitPos &= 7;
        }
    }
    size_t const nbBytesCommitted = (size_t)(bitPos + 7) / 8;
    if (MEM_ptrDistance(dstPtr, dstEnd) >= 8) {
        ZL_writeLE64(dstPtr, acc64);
    } else {
        ZL_writeLE64_N(dstPtr, acc64, nbBytesCommitted);
    }
    dstPtr += nbBytesCommitted;

    return MEM_ptrDistance(dst, dstPtr);
}

static size_t ZS_bitpackEncode8_generic(
        uint8_t* op,
        uint8_t const* ip,
        size_t nbElts,
        size_t nbBits)
{
    size_t const dstSize      = (nbElts * nbBits + 7) / 8;
    uint8_t const* const iend = ip + nbElts;
    uint8_t* const oend       = op + dstSize;
    size_t bits               = 0;
    size_t state              = 0;
    while (ip < iend) {
        state |= (size_t)*ip++ << bits;
        bits += nbBits;
        if (bits >= 8) {
            *op++ = (uint8_t)state;
            bits -= 8;
            state >>= 8;
        }
    }
    ZL_ASSERT_LT(bits, 8);
    if (bits > 0) {
        *op++ = (uint8_t)state;
    }
    ZL_ASSERT_EQ(op, oend);
    return dstSize;
}

static size_t ZS_bitpackEncode16_generic(
        uint8_t* op,
        uint16_t const* ip,
        size_t nbElts,
        size_t nbBits)
{
    size_t const dstSize       = (nbElts * nbBits + 7) / 8;
    uint16_t const* const iend = ip + nbElts;
    uint8_t* const oend        = op + dstSize;
    size_t bits                = 0;
    size_t state               = 0;
    while (ip < iend) {
        state |= (size_t)ZL_readLE16(ip) << bits;
        ++ip;
        bits += nbBits;
        while (bits >= 8) {
            *op++ = (uint8_t)state;
            bits -= 8;
            state >>= 8;
        }
    }
    ZL_ASSERT_LT(bits, 8);
    if (bits > 0) {
        *op++ = (uint8_t)state;
    }
    ZL_ASSERT_EQ(op, oend);
    return dstSize;
}

static size_t ZS_bitpackEncode64_generic(
        void* dst,
        const uint64_t* src,
        size_t nbElts,
        size_t nbBits)
{
    size_t const dstSize = (nbElts * nbBits + 7) / 8;
    ZS_BitCStreamFF bs   = ZS_BitCStreamFF_init(dst, dstSize);
    if ((unsigned)nbBits <= (ZS_BITSTREAM_WRITE_MAX_BITS - 7)) {
        const size_t nbPacks =
                (ZS_BITSTREAM_WRITE_MAX_BITS - 7) / (unsigned)nbBits;
        size_t i = 0;
        for (; i < (nbElts / nbPacks) * nbPacks; i += nbPacks) {
            for (size_t j = i; j < i + nbPacks; j++) {
                ZS_BitCStreamFF_write(&bs, src[j], (unsigned)nbBits);
            }
            ZS_BitCStreamFF_flush(&bs);
        }
        for (; i < nbElts; i++) {
            ZS_BitCStreamFF_write(&bs, src[i], (unsigned)nbBits);
        }
        ZS_BitCStreamFF_flush(&bs);
    } else {
        // If we write more than ZS_BITSTREAM_WRITE_MAX_BITS - 7 we might
        // not be able to write all bits in one call, instead we do 2 writes and
        // 2 flushes.
        for (size_t i = 0; i < nbElts; i++) {
            ZL_ASSERT_LE(32, ZS_BITSTREAM_WRITE_MAX_BITS - 7);
            ZL_ASSERT_LE(
                    (unsigned)nbBits - 32, ZS_BITSTREAM_WRITE_MAX_BITS - 7);
            ZS_BitCStreamFF_write(&bs, src[i], 32);
            ZS_BitCStreamFF_flush(&bs);
            ZS_BitCStreamFF_write(&bs, src[i] >> 32, (unsigned)nbBits - 32);
            ZS_BitCStreamFF_flush(&bs);
        }
    }
    return ZL_validResult(ZS_BitCStreamFF_finish(&bs));
}

#if ZS_HAS_FAST_BITPACK

#    define ZS_BITPACK_ENCODE_8_T_FN(type) ZS_bitpackEncode8_##type##_bmi2

#    define ZS_BITPACK_ENCODE_8_T(type, convert16Fn, leftoversFn)       \
        ZL_FORCE_NOINLINE size_t ZS_BITPACK_ENCODE_8_T_FN(type)(        \
                uint8_t* restrict op,                                   \
                type const* restrict ip,                                \
                size_t nbElts,                                          \
                size_t nbBits)                                          \
        {                                                               \
            ZL_ASSERT_LE(nbBits, 8);                                    \
            size_t const dstSize   = (nbElts * nbBits + 7) / 8;         \
            type const* const iend = ip + nbElts;                       \
            uint8_t* const oend    = op + dstSize;                      \
            {                                                           \
                size_t const bytesPerLoop = nbBits;                     \
                uint8_t* olimit           = oend - bytesPerLoop - 7;    \
                uint64_t const mask =                                   \
                        ((1ull << nbBits) - 1) * 0x0101010101010101ULL; \
                while (op < olimit) {                                   \
                    uint8_t ints[16];                                   \
                    convert16Fn(ints, ip);                              \
                    ip += 16;                                           \
                    for (size_t i = 0; i < 16; i += 8) {                \
                        uint64_t bytes      = ZL_read64(ints + i);      \
                        uint64_t const bits = _pext_u64(bytes, mask);   \
                        ZS_write64(op, bits);                           \
                        op += bytesPerLoop;                             \
                    }                                                   \
                }                                                       \
            }                                                           \
            ZL_ASSERT_LE(ip, iend);                                     \
            op += leftoversFn(op, ip, (size_t)(iend - ip), nbBits);     \
            ZL_ASSERT_EQ(op, oend);                                     \
            return dstSize;                                             \
        }

static void convert16U8ToU8(uint8_t* dst, uint8_t const* src)
{
    memcpy(dst, src, 16);
}

static void convert16U16ToU8(uint8_t* dst, uint16_t const* src)
{
    __m128i const loV  = _mm_loadu_si128((__m128i_u const*)(src + 0));
    __m128i const hiV  = _mm_loadu_si128((__m128i_u const*)(src + 8));
    __m128i const dstV = _mm_packus_epi16(loV, hiV);
    _mm_storeu_si128((__m128i_u*)dst, dstV);
}

static void convert16U32ToU8(uint8_t* dst, uint32_t const* src)
{
    __m128i const src0V  = _mm_loadu_si128((__m128i_u const*)(src + 0x0));
    __m128i const src4V  = _mm_loadu_si128((__m128i_u const*)(src + 0x4));
    __m128i const src8V  = _mm_loadu_si128((__m128i_u const*)(src + 0x8));
    __m128i const srcCV  = _mm_loadu_si128((__m128i_u const*)(src + 0xC));
    __m128i const src04V = _mm_packus_epi32(src0V, src4V);
    __m128i const src8CV = _mm_packus_epi32(src8V, srcCV);
    __m128i const dstV   = _mm_packus_epi16(src04V, src8CV);
    _mm_storeu_si128((__m128i_u*)dst, dstV);
}

static void convert16U64ToU8(uint8_t* dst, uint64_t const* src)
{
    uint8_t const* src8  = (uint8_t const*)src;
    __m256i const src0V  = _mm256_loadu_si256((__m256i_u const*)(src8 + 0x00));
    __m256i const src4V  = _mm256_loadu_si256((__m256i_u const*)(src8 + 0x1F));
    __m256i const src8V  = _mm256_loadu_si256((__m256i_u const*)(src8 + 0x3E));
    __m256i const srcCV  = _mm256_loadu_si256((__m256i_u const*)(src8 + 0x5D));
    __m256i const src04V = _mm256_or_si256(src0V, src4V);
    __m256i const src8CV = _mm256_or_si256(src8V, srcCV);
    // The low 32-bits are set in each 64-bit value.
    __m256i const src048CV = _mm256_or_si256(src04V, src8CV);
    // Shuffle all the low packed values into the first 128-bits of the vector.
    __m128i const packedV = _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(
            src048CV, _mm256_setr_epi32(0, 2, 4, 6, 0, 2, 4, 6)));
    // Shuffle the packed values into the correct order.
    __m128i const dstV = _mm_shuffle_epi8(
            packedV,
            _mm_setr_epi8(
                    0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15));
    _mm_storeu_si128((__m128i_u*)dst, dstV);
}

ZS_BITPACK_ENCODE_8_T(uint8_t, convert16U8ToU8, ZS_bitpackEncode8_generic)
ZS_BITPACK_ENCODE_8_T(uint16_t, convert16U16ToU8, ZS_bitpackEncode16_generic)
ZS_BITPACK_ENCODE_8_T(uint32_t, convert16U32ToU8, ZS_bitpackEncode32_generic)
ZS_BITPACK_ENCODE_8_T(uint64_t, convert16U64ToU8, ZS_bitpackEncode64_generic)

#    define ZS_BITPACK_ENCODE_16_T_FN(type) ZS_bitpackEncode16_##type##_bmi2

#    define ZS_BITPACK_ENCODE_16_T(type, convert8Fn, leftoversFn)          \
        ZL_FORCE_NOINLINE size_t ZS_BITPACK_ENCODE_16_T_FN(type)(          \
                uint8_t* op, type const* ip, size_t nbElts, size_t nbBits) \
        {                                                                  \
            ZL_ASSERT_LE(nbBits, 16);                                      \
            size_t const dstSize          = (nbElts * nbBits + 7) / 8;     \
            size_t const bytesPerLoop     = nbBits;                        \
            size_t const halfBytesPerLoop = bytesPerLoop / 2;              \
            type const* const iend        = ip + nbElts;                   \
            uint8_t* const oend           = op + dstSize;                  \
            uint8_t* olimit =                                              \
                    oend - ZL_MAX(halfBytesPerLoop + 7, bytesPerLoop);     \
            if (nbBits % 2 == 0) {                                         \
                uint64_t const mask =                                      \
                        ((1ull << nbBits) - 1) * 0x0001000100010001ULL;    \
                while (op < olimit) {                                      \
                    uint16_t ints[8];                                      \
                    convert8Fn(ints, ip);                                  \
                    ip += 8;                                               \
                    for (size_t i = 0; i < 8; i += 4) {                    \
                        uint64_t const bytes = ZL_read64(ints + i);        \
                        uint64_t const bits  = _pext_u64(bytes, mask);     \
                        ZS_write64(op, bits);                              \
                        op += halfBytesPerLoop;                            \
                    }                                                      \
                }                                                          \
            } else {                                                       \
                uint64_t const mask =                                      \
                        ((1ull << nbBits) - 1) * 0x0001000100010001ULL;    \
                size_t const shift0 = nbBits * 4 - 4;                      \
                size_t const shift1 = 4;                                   \
                while (op < olimit) {                                      \
                    uint16_t ints[8];                                      \
                    convert8Fn(ints, ip);                                  \
                    ip += 8;                                               \
                    uint64_t const bytes0 = ZL_read64(ints + 0);           \
                    uint64_t const bits0  = _pext_u64(bytes0, mask);       \
                    ZS_write64(op, bits0);                                 \
                    uint64_t const bytes1 = ZL_read64(ints + 4);           \
                    uint64_t const bits1  = _pext_u64(bytes1, mask);       \
                    ZL_ASSERT_EQ((bits0 >> shift0) & (size_t)~0xf, 0);     \
                    ZS_write64(                                            \
                            op + halfBytesPerLoop,                         \
                            (bits0 >> shift0) | (bits1 << shift1));        \
                    op += bytesPerLoop;                                    \
                }                                                          \
            }                                                              \
            ZL_ASSERT_LE(ip, iend);                                        \
            op += leftoversFn(op, ip, (size_t)(iend - ip), nbBits);        \
            ZL_ASSERT_EQ(op, oend);                                        \
            return dstSize;                                                \
        }

static void convert8U16ToU16(uint16_t* dst, uint16_t const* src)
{
    memcpy(dst, src, 16);
}

static void convert8U32ToU16(uint16_t* dst, uint32_t const* src)
{
    __m128i const src0V = _mm_loadu_si128((__m128i_u const*)(src + 0x0));
    __m128i const src4V = _mm_loadu_si128((__m128i_u const*)(src + 0x4));
    __m128i const dstV  = _mm_packus_epi32(src0V, src4V);
    _mm_storeu_si128((__m128i_u*)dst, dstV);
}

static void convert8U64ToU16(uint16_t* dst, uint64_t const* src)
{
    uint32_t const* src32 = (const uint32_t*)src;
    __m128i const src0V   = _mm_loadu_si128((__m128i_u const*)(src32 + 0x0));
    __m128i const src2V   = _mm_loadu_si128((__m128i_u const*)(src32 + 0x3));
    __m128i const src4V   = _mm_loadu_si128((__m128i_u const*)(src32 + 0x8));
    __m128i const src6V   = _mm_loadu_si128((__m128i_u const*)(src32 + 0xB));
    // 0, 2, 1, 3
    __m128i const src02V = _mm_or_si128(src0V, src2V);
    // 4, 6, 5, 7
    __m128i const src46V = _mm_or_si128(src4V, src6V);
    // 0, 2, 1, 3, 4, 6, 5, 7
    __m128i const packedV = _mm_packus_epi32(src02V, src46V);
    __m128i const dstV    = _mm_shuffle_epi8(
            packedV,
            _mm_setr_epi8(
                    0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15));
    _mm_storeu_si128((__m128i_u*)dst, dstV);
}

ZS_BITPACK_ENCODE_16_T(uint16_t, convert8U16ToU16, ZS_bitpackEncode16_generic)
ZS_BITPACK_ENCODE_16_T(uint32_t, convert8U32ToU16, ZS_bitpackEncode32_generic)
ZS_BITPACK_ENCODE_16_T(uint64_t, convert8U64ToU16, ZS_bitpackEncode64_generic)

#endif // ZS_HAS_FAST_BITPACK

size_t ZS_bitpackEncode8(
        void* dst,
        size_t dstCapacity,
        const uint8_t* src,
        size_t nbElts,
        int nbBits)
{
    size_t const ret = ZS_bitpackEncodeEdgeCase(
            dst, dstCapacity, src, nbElts, sizeof(*src), nbBits);
    if (ret != (size_t)-1)
        return ret;
#if ZS_HAS_FAST_BITPACK
    return ZS_BITPACK_ENCODE_8_T_FN(uint8_t)(
            (uint8_t*)dst, src, nbElts, (size_t)nbBits);
#else
    return ZS_bitpackEncode8_generic(
            (uint8_t*)dst, src, nbElts, (size_t)nbBits);
#endif
}

static size_t
ZS_bytepackEncode16(uint8_t* dst, size_t nbElts, uint16_t const* src)
{
    for (size_t i = 0; i < nbElts; ++i) {
        dst[i] = (uint8_t)src[i];
    }
    return nbElts;
}

size_t ZS_bitpackEncode16(
        void* dst,
        size_t dstCapacity,
        const uint16_t* src,
        size_t nbElts,
        int nbBits)
{
    size_t const ret = ZS_bitpackEncodeEdgeCase(
            dst, dstCapacity, src, nbElts, sizeof(*src), nbBits);
    if (ret != (size_t)-1)
        return ret;

    if (nbBits % 8 == 0) {
        ZL_ASSERT_EQ(nbBits, 8);
        return ZS_bytepackEncode16((uint8_t*)dst, nbElts, src);
    }

#if ZS_HAS_FAST_BITPACK
    if (nbBits <= 8) {
        return ZS_BITPACK_ENCODE_8_T_FN(uint16_t)(
                (uint8_t*)dst, src, nbElts, (size_t)nbBits);
    }
    return ZS_BITPACK_ENCODE_16_T_FN(uint16_t)(
            (uint8_t*)dst, src, nbElts, (size_t)nbBits);
#else
    return ZS_bitpackEncode16_generic(
            (uint8_t*)dst, src, nbElts, (size_t)nbBits);
#endif
}

static void ZS_writeLEN32(uint8_t* dst, uint32_t val, size_t n)
{
    ZL_ASSERT_GE(n, 1);
    ZL_ASSERT_LE(n, 4);
    if (!ZL_isLittleEndian()) {
        val = ZL_swap32(val);
    }
    memcpy(dst, &val, n);
}

ZL_FORCE_INLINE size_t ZS_bytepackEncode32_impl(
        uint8_t* dst,
        size_t nbElts,
        uint32_t const* src,
        size_t n)
{
    for (size_t i = 0; i < nbElts; ++i) {
        ZS_writeLEN32(dst + n * i, src[i], n);
    }
    return n * nbElts;
}

static size_t ZS_bytepackEncode32(
        uint8_t* dst,
        size_t nbElts,
        uint32_t const* src,
        int nbBits)
{
    ZL_ASSERT_EQ(nbBits % 8, 0);
    if (nbBits == 8) {
        return ZS_bytepackEncode32_impl(dst, nbElts, src, 1);
    }
    if (nbBits == 16) {
        return ZS_bytepackEncode32_impl(dst, nbElts, src, 2);
    }
    ZL_ASSERT_EQ(nbBits, 24);
    return ZS_bytepackEncode32_impl(dst, nbElts, src, 3);
}

size_t ZS_bitpackEncode32(
        void* dst,
        size_t dstCapacity,
        const uint32_t* src,
        size_t nbElts,
        int nbBits)
{
    size_t const ret = ZS_bitpackEncodeEdgeCase(
            dst, dstCapacity, src, nbElts, sizeof(*src), nbBits);
    if (ret != (size_t)-1)
        return ret;
    // Dispatch to optimized variant for a small number of 1-bit values
    if (nbBits == 1 && nbElts <= 64)
        return bit1pack32(dst, dstCapacity, src, nbElts);

    if (nbBits % 8 == 0) {
        return ZS_bytepackEncode32((uint8_t*)dst, nbElts, src, nbBits);
    }

#if ZS_HAS_FAST_BITPACK
    if (nbBits <= 8) {
        return ZS_BITPACK_ENCODE_8_T_FN(uint32_t)(
                (uint8_t*)dst, src, nbElts, (size_t)nbBits);
    }
    if (nbBits <= 16) {
        return ZS_BITPACK_ENCODE_16_T_FN(uint32_t)(
                (uint8_t*)dst, src, nbElts, (size_t)nbBits);
    }
#endif

    // Otherwise return the generic variant
    return ZS_bitpackEncode32_generic(dst, src, nbElts, (size_t)nbBits);
}

static void ZS_writeLEN64(uint8_t* dst, uint64_t val, size_t n)
{
    ZL_ASSERT_GE(n, 1);
    ZL_ASSERT_LE(n, 8);
    if (!ZL_isLittleEndian()) {
        val = ZL_swap64(val);
    }
    memcpy(dst, &val, n);
}

ZL_FORCE_INLINE size_t ZS_bytepackEncode64_impl(
        uint8_t* dst,
        size_t nbElts,
        uint64_t const* src,
        size_t n)
{
    for (size_t i = 0; i < nbElts; ++i) {
        ZS_writeLEN64(dst + n * i, src[i], n);
    }
    return n * nbElts;
}

static size_t ZS_bytepackEncode64(
        uint8_t* dst,
        size_t nbElts,
        uint64_t const* src,
        int nbBits)
{
    // TODO: This could be faster, especially for powers of 2.
    ZL_ASSERT_EQ(nbBits % 8, 0);
    switch (nbBits) {
        case 8:
            return ZS_bytepackEncode64_impl(dst, nbElts, src, 1);
        case 16:
            return ZS_bytepackEncode64_impl(dst, nbElts, src, 2);
        case 24:
            return ZS_bytepackEncode64_impl(dst, nbElts, src, 3);
        case 32:
            return ZS_bytepackEncode64_impl(dst, nbElts, src, 4);
        case 40:
            return ZS_bytepackEncode64_impl(dst, nbElts, src, 5);
        case 48:
            return ZS_bytepackEncode64_impl(dst, nbElts, src, 6);
        case 56:
            return ZS_bytepackEncode64_impl(dst, nbElts, src, 7);
        default:
            ZL_ASSERT(false, "Unreachable");
            return 0;
    }
}

size_t ZS_bitpackEncode64(
        void* dst,
        const size_t dstCapacity,
        const uint64_t* src,
        const size_t nbElts,
        const int nbBits)
{
    size_t const ret = ZS_bitpackEncodeEdgeCase(
            dst, dstCapacity, src, nbElts, sizeof(*src), nbBits);
    if (ret != (size_t)-1)
        return ret;

    if (nbBits % 8 == 0) {
        return ZS_bytepackEncode64((uint8_t*)dst, nbElts, src, nbBits);
    }

#if ZS_HAS_FAST_BITPACK
    if (nbBits <= 8) {
        return ZS_BITPACK_ENCODE_8_T_FN(uint64_t)(
                (uint8_t*)dst, src, nbElts, (size_t)nbBits);
    }
    if (nbBits <= 16) {
        return ZS_BITPACK_ENCODE_16_T_FN(uint64_t)(
                (uint8_t*)dst, src, nbElts, (size_t)nbBits);
    }
#endif

    return ZS_bitpackEncode64_generic(dst, src, nbElts, (size_t)nbBits);
}

size_t ZS_bitpackEncode(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t nbElts,
        size_t eltWidth,
        int nbBits)
{
    switch (eltWidth) {
        case 1:
            return ZS_bitpackEncode8(dst, dstCapacity, src, nbElts, nbBits);
        case 2:
            return ZS_bitpackEncode16(dst, dstCapacity, src, nbElts, nbBits);
        case 4:
            return ZS_bitpackEncode32(dst, dstCapacity, src, nbElts, nbBits);
        case 8:
            return ZS_bitpackEncode64(dst, dstCapacity, src, nbElts, nbBits);
        default:
            ZL_ASSERT_FAIL("Bad eltWidth %u!", (unsigned)eltWidth);
            return 0;
    }
}

/*
 * ==================
 * DECODE FUNCTIONS
 * ==================
 */

/// Detect & handle edge cases - shared code for each width
static size_t ZS_bitpackDecodeEdgeCase(
        void* dst,
        size_t nbElts,
        void const* src,
        size_t srcCapacity,
        int eltSize,
        int nbBits)
{
    size_t const srcSize = (nbElts * (size_t)nbBits + 7) / 8;
    if (nbElts == 0)
        return 0;
    if (nbBits == 0) {
        memset(dst, 0, nbElts * (size_t)eltSize);
        return 0;
    }

    ZL_ASSERT_GT(nbBits, 0);
    ZL_ASSERT_GE(srcCapacity, srcSize);

    ZL_ASSERT_NN(src);
    ZL_ASSERT_NN(dst);

    if (nbBits == eltSize * 8) {
        ZL_ASSERT(ZL_isLittleEndian());
        memcpy(dst, src, srcSize);
        return srcSize;
    }

    ZL_ASSERT_LT(nbBits, eltSize * 8);

    return (size_t)-1;
}

// TODO 1 (@Cyan): more generalized version, allowing @nbBits > 64
// TODO 2 (@Cyan): more variants, @dst arrays of 8 & 16 bits
// TODO 3 (@Cyan): speed optimization using Vector instructions (?)
static size_t bit1depack32(
        uint32_t* dst32,
        size_t nb1BitValues,
        const void* src,
        size_t srcCapacity)
{
    ZL_ASSERT_NN(dst32);
    if (nb1BitValues)
        ZL_ASSERT_NN(src);
    size_t const srcConsumed = (nb1BitValues + 7) / 8;
    ZL_ASSERT_LE(srcConsumed, srcCapacity);
    ZL_ASSERT_LE(nb1BitValues, 64); // current limitation
    uint64_t acc = (srcCapacity < 8) ? ZL_readLE64_N(src, srcCapacity)
                                     : ZL_readLE64(src);
    for (size_t n = 0; n < nb1BitValues; n++) {
        dst32[n] = acc & 1;
        acc >>= 1;
    }
    return srcConsumed;
}

// Same implementation as above, just a @dst type difference.
// Sharing implementation code would require template or equivalent.
static size_t bit1depack8(
        uint8_t* dst8,
        size_t nb1BitValues,
        const void* src,
        size_t srcCapacity)
{
    ZL_ASSERT_NN(dst8);
    if (nb1BitValues)
        ZL_ASSERT_NN(src);
    size_t const srcConsumed = (nb1BitValues + 7) / 8;
    ZL_ASSERT_LE(srcConsumed, srcCapacity);
    ZL_ASSERT_LE(nb1BitValues, 64); // current limitation
    uint64_t acc = (srcCapacity < 8) ? ZL_readLE64_N(src, srcCapacity)
                                     : ZL_readLE64(src);
    for (size_t n = 0; n < nb1BitValues; n++) {
        dst8[n] = acc & 1;
        acc >>= 1;
    }
    return srcConsumed;
}

static size_t ZS_bitpackDecode64_generic(
        uint64_t* dst,
        const size_t nbElts,
        void const* src,
        const size_t nbBits)
{
    size_t const srcSize = (nbElts * (size_t)nbBits + 7) / 8;

    ZS_BitDStreamFF bs = ZS_BitDStreamFF_init(src, srcSize);
    ZS_BitDStreamFF_reload(&bs);
    if ((unsigned)nbBits <= ZS_BITSTREAM_READ_MAX_BITS - 7) {
        const size_t nbPacks =
                (ZS_BITSTREAM_READ_MAX_BITS - 7) / (unsigned)nbBits;
        size_t i = 0;
        for (; i < (nbElts / nbPacks) * nbPacks; i += nbPacks) {
            for (size_t j = i; j < i + nbPacks; j++) {
                dst[j] = ZS_BitDStreamFF_read(&bs, (unsigned)nbBits);
            }
            ZS_BitDStreamFF_reload(&bs);
        }
        for (; i < nbElts; i++) {
            dst[i] = ZS_BitDStreamFF_read(&bs, (unsigned)nbBits);
            ZS_BitDStreamFF_reload(&bs);
        }
    } else {
        for (size_t i = 0; i < nbElts; i++) {
            dst[i] = ZS_BitDStreamFF_read(&bs, 32);
            ZS_BitDStreamFF_reload(&bs);
            dst[i] |= ZS_BitDStreamFF_read(&bs, (unsigned)nbBits - 32) << 32;
            ZS_BitDStreamFF_reload(&bs);
        }
    }
    return ((unsigned)nbBits * nbElts + 7) / 8;
}

static size_t ZS_bitpackDecode32_generic(
        uint32_t* dst32,
        size_t nbValues,
        const void* src,
        size_t nbBits)
{
    ZL_DLOG(BLOCK,
            "bitNdepack32, %zu elts, using %d bits",
            nbValues,
            (int)nbBits);
    if (!nbValues)
        return 0;
    ZL_ASSERT_NN(dst32);
    ZL_ASSERT_NN(src);
    ZL_ASSERT_GT(nbBits, 0);
    ZL_ASSERT_LT(nbBits, 32);
    size_t const srcCapacity = (nbValues * nbBits + 7) / 8;
    size_t const srcConsumed = srcCapacity;
    ZL_ASSERT_LE(srcConsumed, srcCapacity);
    size_t const nbDepacks        = 56 / nbBits;
    size_t const nbFullRounds     = nbValues / nbDepacks;
    uint64_t const mask64         = ((uint64_t)1 << nbBits) - 1;
    const uint8_t* srcPtr         = src;
    const uint8_t* const srcEnd   = srcPtr + srcCapacity;
    const uint8_t* const srcLimit = srcEnd - 7;
    uint64_t acc64 = (srcCapacity < 8) ? ZL_readLE64_N(srcPtr, srcCapacity)
                                       : ZL_readLE64(srcPtr);
    size_t idx     = 0;
    int bitPos     = 0;
    for (size_t nr = 0; nr < nbFullRounds; nr++) {
        for (size_t n = 0; n < nbDepacks; n++) {
            dst32[idx++] = (uint32_t)(acc64 & mask64);
            acc64 >>= nbBits;
        }
        bitPos += (int)nbDepacks * (int)nbBits;
        int const nbBytesConsumed = bitPos / 8;
        srcPtr += nbBytesConsumed;
        bitPos &= 7;
        if (ZL_LIKELY(srcPtr < srcLimit)) {
            ZL_ASSERT_GE(MEM_ptrDistance(srcPtr, srcEnd), 8);
            acc64 = ZL_readLE64(srcPtr);
            acc64 >>= bitPos;
        } else {
            size_t const remainingSrc = MEM_ptrDistance(srcPtr, srcEnd);
            acc64                     = ZL_readLE64_N(srcPtr, remainingSrc);
            acc64 >>= bitPos;
            break;
        }
    }
    // last round, non full
    int const bitLimit = 63 - (int)nbBits;
    while (idx < nbValues) {
        dst32[idx] = (uint32_t)(acc64 & mask64);
        idx++;
        acc64 >>= nbBits;
        bitPos += (int)nbBits;
        if (bitPos > bitLimit) {
            int const nbBytesConsumed = bitPos / 8;
            srcPtr += nbBytesConsumed;
            bitPos &= 7;
            size_t const remainingSrc = MEM_ptrDistance(srcPtr, srcEnd);
            acc64 = (remainingSrc < 8) ? ZL_readLE64_N(srcPtr, remainingSrc)
                                       : ZL_readLE64(srcPtr);
            acc64 >>= bitPos;
        }
    }
    ZL_ASSERT_EQ(
            MEM_ptrDistance(src, srcPtr + ((bitPos + 7) / 8)), srcConsumed);
    return srcConsumed;
}

static size_t ZS_bitpackDecode16_generic(
        uint16_t* op,
        size_t nbElts,
        uint8_t const* ip,
        size_t nbBits)
{
    ZL_ASSERT(ZL_isLittleEndian());
    size_t const srcSize      = (nbElts * nbBits + 7) / 8;
    uint8_t const* const iend = ip + srcSize;
    uint16_t* const oend      = op + nbElts;

    size_t bits       = 0;
    size_t state      = 0;
    size_t const mask = ((1u << nbBits) - 1);
    while (op < oend) {
        while (bits < nbBits) {
            state |= ((size_t)*ip++) << bits;
            bits += 8;
        }
        ZL_writeLE16(op, (uint16_t)(state & mask));
        ++op;
        state >>= nbBits;
        bits -= nbBits;
    }
    ZL_ASSERT_EQ(ip, iend);
    return srcSize;
}

static size_t ZS_bitpackDecode8_generic(
        uint8_t* op,
        size_t nbElts,
        uint8_t const* ip,
        size_t nbBits)
{
    ZL_ASSERT(ZL_isLittleEndian());
    size_t const srcSize      = (nbElts * nbBits + 7) / 8;
    uint8_t const* const iend = ip + srcSize;
    uint8_t* const oend       = op + nbElts;
    size_t bits               = 0;
    size_t state              = 0;
    size_t const mask         = ((1u << nbBits) - 1);
    while (op < oend) {
        if (bits < nbBits) {
            state |= ((size_t)*ip++) << bits;
            bits += 8;
        }
        *op++ = (uint8_t)(state & mask);
        state >>= nbBits;
        bits -= nbBits;
    }
    ZL_ASSERT_EQ(ip, iend);
    return srcSize;
}

#if ZS_HAS_FAST_BITPACK

static ZL_MAYBE_UNUSED_FUNCTION size_t ZS_bitpackDecode16_bmi2(
        uint16_t* op,
        size_t nbElts,
        uint8_t const* ip,
        size_t nbBits)
{
    ZL_ASSERT(ZL_isLittleEndian());
    size_t const srcSize      = (nbElts * nbBits + 7) / 8;
    uint8_t const* const iend = ip + srcSize;
    uint16_t* const oend      = op + nbElts;

    if (nbBits % 2 == 0) {
        uint8_t const* ilimit     = iend - 8;
        size_t const bytesPerLoop = nbBits / 2;
        uint64_t const mask = ((1ull << nbBits) - 1) * 0x0001000100010001ULL;
        while (ip < ilimit) {
            uint64_t const bits = ZL_readLE64(ip);
            ip += bytesPerLoop;
            uint64_t const bytes = _pdep_u64(bits, mask);
            ZL_writeLE64(op, bytes);
            op += 4;
        }
    } else {
        uint8_t const* ilimit         = iend - 16;
        size_t const bytesPerLoop     = nbBits;
        size_t const halfBytesPerLoop = bytesPerLoop / 2;
        uint64_t const mask = ((1ull << nbBits) - 1) * 0x0001000100010001ULL;
        ZL_ASSERT_EQ((nbBits * 4) % 8, 4);
        size_t const shift1 = 4;
        while (ip < ilimit) {
            uint64_t const bits0  = ZL_readLE64(ip);
            uint64_t const bytes0 = _pdep_u64(bits0, mask);
            ZL_writeLE64(op, bytes0);
            uint64_t const bits1  = ZL_readLE64(ip + halfBytesPerLoop);
            uint64_t const bytes1 = _pdep_u64((bits1 >> shift1), mask);
            ZL_writeLE64(op + 4, bytes1);
            ip += bytesPerLoop;
            op += 8;
        }
    }
    ZL_ASSERT_LE(op, oend);
    ip += ZS_bitpackDecode16_generic(op, (size_t)(oend - op), ip, nbBits);
    ZL_ASSERT_EQ(ip, iend);
    return srcSize;
}

#    define ZS_BITPACK_DECODE_8_T_FN(type) ZS_bitpackDecode8_##type##_bmi2

#    define ZS_BITPACK_DECODE_8_T(type, convert16Fn, leftoversFn)       \
        ZL_FORCE_NOINLINE size_t ZS_BITPACK_DECODE_8_T_FN(type)(        \
                type* restrict op,                                      \
                size_t nbElts,                                          \
                uint8_t const* restrict ip,                             \
                size_t nbBits)                                          \
        {                                                               \
            ZL_ASSERT(nbBits <= 8);                                     \
            ZL_ASSERT(ZL_isLittleEndian());                             \
            size_t const srcSize      = (nbElts * nbBits + 7) / 8;      \
            uint8_t const* const iend = ip + srcSize;                   \
            type* const oend          = op + nbElts;                    \
            {                                                           \
                size_t const bytesPerLoop = nbBits;                     \
                uint8_t const* ilimit     = iend - bytesPerLoop - 7;    \
                uint64_t const mask =                                   \
                        ((1ull << nbBits) - 1) * 0x0101010101010101ULL; \
                while (ip < ilimit) {                                   \
                    uint8_t ints[16];                                   \
                    for (size_t i = 0; i < 16; i += 8) {                \
                        uint64_t const bits = ZL_read64(ip);            \
                        ip += bytesPerLoop;                             \
                        uint64_t const bytes = _pdep_u64(bits, mask);   \
                        ZS_write64(ints + i, bytes);                    \
                    }                                                   \
                    convert16Fn(op, ints);                              \
                    op += 16;                                           \
                }                                                       \
            }                                                           \
            ZL_ASSERT_LE(op, oend);                                     \
            ip += leftoversFn(op, (size_t)(oend - op), ip, nbBits);     \
            ZL_ASSERT_EQ(ip, iend);                                     \
            return srcSize;                                             \
        }

static void convert16U8ToU16(uint16_t* dst, uint8_t const* src)
{
    __m128i const srcV = _mm_loadu_si128((__m128i_u const*)src);
    __m256i const dstV = _mm256_cvtepu8_epi16(srcV);
    _mm256_storeu_si256((__m256i_u*)dst, dstV);
}

static void convert16U8ToU32(uint32_t* dst, uint8_t const* src)
{
    // The 128-bit load & shift don't actually happen in practice, because we
    // are loading src from 2 uint64_t. So the compiler will just load directly
    // from the 64-bit registers.
    __m128i const srcV = _mm_loadu_si128((__m128i_u const*)src);
    __m256i const loV  = _mm256_cvtepu8_epi32(srcV);
    __m256i const hiV  = _mm256_cvtepu8_epi32(_mm_srli_si128(srcV, 8));
    _mm256_storeu_si256((__m256i_u*)dst, loV);
    _mm256_storeu_si256((__m256i_u*)(dst + 8), hiV);
}

static void convert16U8ToU64(uint64_t* dst, uint8_t const* src)
{
    __m128i const srcV  = _mm_loadu_si128((__m128i_u const*)src);
    __m256i const dst0V = _mm256_cvtepu8_epi64(srcV);
    __m256i const dst1V = _mm256_cvtepu8_epi64(_mm_srli_si128(srcV, 4));
    __m256i const dst2V = _mm256_cvtepu8_epi64(_mm_srli_si128(srcV, 8));
    __m256i const dst3V = _mm256_cvtepu8_epi64(_mm_srli_si128(srcV, 12));
    _mm256_storeu_si256((__m256i_u*)dst, dst0V);
    _mm256_storeu_si256((__m256i_u*)(dst + 4), dst1V);
    _mm256_storeu_si256((__m256i_u*)(dst + 8), dst2V);
    _mm256_storeu_si256((__m256i_u*)(dst + 12), dst3V);
}

ZS_BITPACK_DECODE_8_T(uint8_t, convert16U8ToU8, ZS_bitpackDecode8_generic)
ZS_BITPACK_DECODE_8_T(uint16_t, convert16U8ToU16, ZS_bitpackDecode16_generic)
ZS_BITPACK_DECODE_8_T(uint32_t, convert16U8ToU32, ZS_bitpackDecode32_generic)
ZS_BITPACK_DECODE_8_T(uint64_t, convert16U8ToU64, ZS_bitpackDecode64_generic)

#    define ZS_BITPACK_DECODE_16_T_FN(type) ZS_bitpackDecode16_##type##_bmi2

#    define ZS_BITPACK_DECODE_16_T(type, convert8Fn, leftoversFn)            \
        static size_t ZS_BITPACK_DECODE_16_T_FN(type)(                       \
                type * op, size_t nbElts, uint8_t const* ip, size_t nbBits)  \
        {                                                                    \
            ZL_ASSERT(nbBits <= 16);                                         \
            ZL_ASSERT(ZL_isLittleEndian());                                  \
            size_t const srcSize      = (nbElts * nbBits + 7) / 8;           \
            uint8_t const* const iend = ip + srcSize;                        \
            type* const oend          = op + nbElts;                         \
                                                                             \
            if (nbBits % 2 == 0) {                                           \
                size_t const bytesPerLoop = nbBits / 2;                      \
                uint8_t const* ilimit     = iend - bytesPerLoop - 7;         \
                uint64_t const mask =                                        \
                        ((1ull << nbBits) - 1) * 0x0001000100010001ULL;      \
                while (ip < ilimit) {                                        \
                    uint16_t ints[8];                                        \
                    for (int i = 0; i < 8; i += 4) {                         \
                        uint64_t const bits = ZL_read64(ip);                 \
                        ip += bytesPerLoop;                                  \
                        uint64_t const bytes = _pdep_u64(bits, mask);        \
                        ZS_write64(ints + i, bytes);                         \
                    }                                                        \
                    convert8Fn(op, ints);                                    \
                    op += 8;                                                 \
                }                                                            \
            } else {                                                         \
                size_t const bytesPerLoop     = nbBits;                      \
                size_t const halfBytesPerLoop = bytesPerLoop / 2;            \
                uint8_t const* ilimit         = iend - halfBytesPerLoop - 7; \
                uint64_t const mask =                                        \
                        ((1ull << nbBits) - 1) * 0x0001000100010001ULL;      \
                ZL_ASSERT_EQ((nbBits * 4) % 8, 4);                           \
                size_t const shift1 = 4;                                     \
                while (ip < ilimit) {                                        \
                    uint16_t ints[8];                                        \
                    uint64_t const bits0  = ZL_read64(ip);                   \
                    uint64_t const bytes0 = _pdep_u64(bits0, mask);          \
                    ZS_write64(ints + 0, bytes0);                            \
                    uint64_t const bits1 = ZL_read64(ip + halfBytesPerLoop); \
                    uint64_t const bytes1 =                                  \
                            _pdep_u64((bits1 >> shift1), mask);              \
                    ZS_write64(ints + 4, bytes1);                            \
                    convert8Fn(op, ints);                                    \
                    ip += bytesPerLoop;                                      \
                    op += 8;                                                 \
                }                                                            \
            }                                                                \
            ZL_ASSERT_LE(op, oend);                                          \
            ip += leftoversFn(op, (size_t)(oend - op), ip, nbBits);          \
            ZL_ASSERT_EQ(ip, iend);                                          \
            return srcSize;                                                  \
        }

static void convert8U16ToU32(uint32_t* dst, uint16_t const* src)
{
    __m128i const srcV = _mm_loadu_si128((__m128i_u const*)src);
    __m256i const dstV = _mm256_cvtepu16_epi32(srcV);
    _mm256_storeu_si256((__m256i_u*)dst, dstV);
}

static void convert8U16ToU64(uint64_t* dst, uint16_t const* src)
{
    __m128i const srcV = _mm_loadu_si128((__m128i_u const*)src);
    __m256i const loV  = _mm256_cvtepu16_epi64(srcV);
    __m256i const hiV  = _mm256_cvtepu16_epi64(_mm_srli_si128(srcV, 8));
    _mm256_storeu_si256((__m256i_u*)dst, loV);
    _mm256_storeu_si256((__m256i_u*)(dst + 4), hiV);
}

ZS_BITPACK_DECODE_16_T(uint16_t, convert8U16ToU16, ZS_bitpackDecode16_generic)
ZS_BITPACK_DECODE_16_T(uint32_t, convert8U16ToU32, ZS_bitpackDecode32_generic)
ZS_BITPACK_DECODE_16_T(uint64_t, convert8U16ToU64, ZS_bitpackDecode64_generic)

#endif // ZS_HAS_FAST_BITPACK

size_t ZS_bitpackDecode8(
        uint8_t* dst,
        size_t nbElts,
        void const* src,
        size_t srcCapacity,
        int nbBits)
{
    size_t const ret = ZS_bitpackDecodeEdgeCase(
            dst, nbElts, src, srcCapacity, sizeof(*dst), nbBits);
    if (ret != (size_t)-1)
        return ret;

    // Dispatch to optimized variant for a small number of 1-bit values
    if (nbBits == 1 && nbElts <= 64)
        return bit1depack8(dst, nbElts, src, srcCapacity);

#if ZS_HAS_FAST_BITPACK
    return ZS_BITPACK_DECODE_8_T_FN(uint8_t)(dst, nbElts, src, (size_t)nbBits);
#else
    return ZS_bitpackDecode8_generic(dst, nbElts, src, (size_t)nbBits);
#endif
}

static size_t
ZS_bytepackDecode16(uint16_t* dst, size_t nbElts, uint8_t const* src)
{
    for (size_t i = 0; i < nbElts; ++i) {
        dst[i] = src[i];
    }
    return nbElts;
}

size_t ZS_bitpackDecode16(
        uint16_t* dst,
        size_t nbElts,
        void const* src,
        size_t srcCapacity,
        int nbBits)
{
    size_t const ret = ZS_bitpackDecodeEdgeCase(
            dst, nbElts, src, srcCapacity, sizeof(*dst), nbBits);
    if (ret != (size_t)-1)
        return ret;

    if (nbBits == 8) {
        return ZS_bytepackDecode16(dst, nbElts, src);
    }

#if ZS_HAS_FAST_BITPACK
    if (nbBits <= 8) {
        return ZS_BITPACK_DECODE_8_T_FN(uint16_t)(
                dst, nbElts, src, (size_t)nbBits);
    }
    return ZS_BITPACK_DECODE_16_T_FN(uint16_t)(
            dst, nbElts, src, (size_t)nbBits);
#else
    return ZS_bitpackDecode16_generic(dst, nbElts, src, (size_t)nbBits);
#endif
}

static uint32_t ZS_readLEN32(uint8_t const* src, size_t n)
{
    ZL_ASSERT_GE(n, 1);
    ZL_ASSERT_LE(n, 4);
    uint32_t val = 0;
    memcpy(&val, src, n);
    if (!ZL_isLittleEndian()) {
        val = ZL_swap32(val);
        val >>= (4 - n) * 8;
    }
    return val;
}

ZL_FORCE_INLINE size_t ZS_bytepackDecode32_impl(
        uint32_t* dst,
        size_t nbElts,
        uint8_t const* src,
        size_t n)
{
    for (size_t i = 0; i < nbElts; ++i) {
        dst[i] = ZS_readLEN32(src + n * i, n);
    }
    return n * nbElts;
}

static size_t ZS_bytepackDecode32(
        uint32_t* dst,
        size_t nbElts,
        uint8_t const* src,
        int nbBits)
{
    ZL_ASSERT_EQ(nbBits % 8, 0);
    if (nbBits == 8) {
        return ZS_bytepackDecode32_impl(dst, nbElts, src, 1);
    }
    if (nbBits == 16) {
        return ZS_bytepackDecode32_impl(dst, nbElts, src, 2);
    }
    ZL_ASSERT_EQ(nbBits, 24);
    return ZS_bytepackDecode32_impl(dst, nbElts, src, 3);
}

size_t ZS_bitpackDecode32(
        uint32_t* dst,
        size_t nbElts,
        void const* src,
        size_t srcCapacity,
        int nbBits)
{
    size_t const ret = ZS_bitpackDecodeEdgeCase(
            dst, nbElts, src, srcCapacity, sizeof(*dst), nbBits);
    if (ret != (size_t)-1)
        return ret;

    // Dispatch to optimized variant for a small number of 1-bit values
    if (nbBits == 1 && nbElts <= 64)
        return bit1depack32(dst, nbElts, src, srcCapacity);

    if (nbBits % 8 == 0) {
        ZL_ASSERT_NE(nbBits, 0);
        ZL_ASSERT_LT(nbBits, 32);
        return ZS_bytepackDecode32(dst, nbElts, src, nbBits);
    }

#if ZS_HAS_FAST_BITPACK
    if (nbBits <= 8) {
        return ZS_BITPACK_DECODE_8_T_FN(uint32_t)(
                dst, nbElts, src, (size_t)nbBits);
    } else if (nbBits <= 16) {
        return ZS_BITPACK_DECODE_16_T_FN(uint32_t)(
                dst, nbElts, src, (size_t)nbBits);
    }
#endif

    return ZS_bitpackDecode32_generic(dst, nbElts, src, (size_t)nbBits);
}

static uint64_t ZS_readLEN64(uint8_t const* src, size_t n)
{
    ZL_ASSERT_GE(n, 1);
    ZL_ASSERT_LE(n, 8);
    uint64_t val = 0;
    memcpy(&val, src, n);
    if (!ZL_isLittleEndian()) {
        val = ZL_swap64(val);
        val >>= (8 - n) * 8;
    }
    return val;
}

ZL_FORCE_INLINE size_t ZS_bytepackDecode64_impl(
        uint64_t* dst,
        size_t nbElts,
        uint8_t const* src,
        size_t n)
{
    for (size_t i = 0; i < nbElts; ++i) {
        dst[i] = ZS_readLEN64(src + n * i, n);
    }
    return n * nbElts;
}

static size_t ZS_bytepackDecode64(
        uint64_t* dst,
        size_t nbElts,
        uint8_t const* src,
        int nbBits)
{
    ZL_ASSERT_EQ(nbBits % 8, 0);
    switch (nbBits) {
        case 8:
            return ZS_bytepackDecode64_impl(dst, nbElts, src, 1);
        case 16:
            return ZS_bytepackDecode64_impl(dst, nbElts, src, 2);
        case 24:
            return ZS_bytepackDecode64_impl(dst, nbElts, src, 3);
        case 32:
            return ZS_bytepackDecode64_impl(dst, nbElts, src, 4);
        case 40:
            return ZS_bytepackDecode64_impl(dst, nbElts, src, 5);
        case 48:
            return ZS_bytepackDecode64_impl(dst, nbElts, src, 6);
        case 56:
            return ZS_bytepackDecode64_impl(dst, nbElts, src, 7);
        default:
            ZL_ASSERT(false, "Unreachable");
            return 0;
    }
}

size_t ZS_bitpackDecode64(
        uint64_t* dst,
        const size_t nbElts,
        void const* src,
        const size_t srcCapacity,
        const int nbBits)
{
    size_t const ret = ZS_bitpackDecodeEdgeCase(
            dst, nbElts, src, srcCapacity, sizeof(*dst), nbBits);
    if (ret != (size_t)-1)
        return ret;

    if (nbBits % 8 == 0) {
        return ZS_bytepackDecode64(dst, nbElts, src, nbBits);
    }

#if ZS_HAS_FAST_BITPACK
    if (nbBits <= 8) {
        return ZS_BITPACK_DECODE_8_T_FN(uint64_t)(
                dst, nbElts, src, (size_t)nbBits);
    } else if (nbBits <= 16) {
        return ZS_BITPACK_DECODE_16_T_FN(uint64_t)(
                dst, nbElts, src, (size_t)nbBits);
    }
#endif

    return ZS_bitpackDecode64_generic(dst, nbElts, src, (size_t)nbBits);
}

size_t ZS_bitpackDecode(
        void* dst,
        size_t nbElts,
        size_t eltWidth,
        void const* src,
        size_t srcCapacity,
        int nbBits)
{
    switch (eltWidth) {
        case 1:
            return ZS_bitpackDecode8(dst, nbElts, src, srcCapacity, nbBits);
        case 2:
            return ZS_bitpackDecode16(dst, nbElts, src, srcCapacity, nbBits);
        case 4:
            return ZS_bitpackDecode32(dst, nbElts, src, srcCapacity, nbBits);
        case 8:
            return ZS_bitpackDecode64(dst, nbElts, src, srcCapacity, nbBits);
        default:
            ZL_ASSERT_FAIL("Bad eltWidth %u!", (unsigned)eltWidth);
            return 0;
    }
}
