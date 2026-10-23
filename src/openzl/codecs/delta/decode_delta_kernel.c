// Copyright (c) Meta Platforms, Inc. and affiliates.

// note : relative-path include (same directory by default)
#include "decode_delta_kernel.h"

#include <assert.h>
#include <stdint.h> // uint32_t

#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"

// note : some template capability would be welcomed here

static void ZS_deltaDecode8_scalar(
        uint8_t* dst,
        uint8_t first,
        uint8_t const* deltas,
        size_t nbElts)
{
    assert(nbElts > 0);
    dst[0] = first;
    for (size_t n = 1; n < nbElts; ++n) {
        dst[n] = (uint8_t)(dst[n - 1] + deltas[n - 1]);
    }
}

static void ZS_deltaDecode16_scalar(
        uint16_t* dst,
        uint16_t first,
        uint16_t const* deltas,
        size_t nbElts)
{
    assert(nbElts > 0);
    dst[0] = first;
    for (size_t n = 1; n < nbElts; ++n) {
        dst[n] = (uint16_t)(dst[n - 1] + deltas[n - 1]);
    }
}

static void ZS_deltaDecode32_scalar(
        uint32_t* dst,
        uint32_t first,
        uint32_t const* deltas,
        size_t nbElts)
{
    assert(nbElts > 0);
    dst[0] = first;
    for (size_t n = 1; n < nbElts; ++n) {
        dst[n] = dst[n - 1] + deltas[n - 1];
    }
}

static void ZS_deltaDecode64_scalar(
        uint64_t* dst,
        uint64_t first,
        uint64_t const* deltas,
        size_t nbElts)
{
    assert(nbElts > 0);
    dst[0] = first;
    for (size_t n = 1; n < nbElts; ++n) {
        dst[n] = dst[n - 1] + deltas[n - 1];
    }
}

#if ZL_HAS_SSSE3

#    include <tmmintrin.h>

static size_t nbEltsToVectorize(size_t nbElts, size_t eltsPerIter)
{
    return (nbElts / eltsPerIter) * eltsPerIter;
}

static void ZS_deltaDecode8_ssse3(
        uint8_t* dst,
        uint8_t first,
        uint8_t const* deltas,
        size_t nbElts)
{
    assert(nbElts > 0);
    size_t const kEltsPerIter = sizeof(__m128i) / sizeof(*deltas);
    size_t const prefix = nbElts - nbEltsToVectorize(nbElts - 1, kEltsPerIter);

    assert(prefix >= 1);
    ZS_deltaDecode8_scalar(dst, first, deltas, prefix);

    __m128i prev = _mm_set1_epi8((char)dst[prefix - 1]);

    assert((nbElts - prefix) % kEltsPerIter == 0);
    for (size_t elt = prefix; elt < nbElts; elt += kEltsPerIter) {
        __m128i values = _mm_loadu_si128((__m128i_u const*)&deltas[elt - 1]);
        values         = _mm_add_epi8(values, _mm_slli_si128(values, 8));
        values         = _mm_add_epi8(values, _mm_slli_si128(values, 4));
        values         = _mm_add_epi8(values, _mm_slli_si128(values, 2));
        values         = _mm_add_epi8(values, _mm_slli_si128(values, 1));
        values         = _mm_add_epi8(values, prev);
        prev           = _mm_shuffle_epi8(values, _mm_set1_epi8(0x0f));
        _mm_storeu_si128((__m128i_u*)&dst[elt], values);
    }
}

static void ZS_deltaDecode16_ssse3(
        uint16_t* dst,
        uint16_t first,
        uint16_t const* deltas,
        size_t nbElts)
{
    assert(nbElts > 0);
    size_t const kEltsPerIter = sizeof(__m128i) / sizeof(*deltas);
    size_t const prefix = nbElts - nbEltsToVectorize(nbElts - 1, kEltsPerIter);

    assert(prefix >= 1);
    ZS_deltaDecode16_scalar(dst, first, deltas, prefix);
    __m128i prev = _mm_set1_epi16((int16_t)dst[prefix - 1]);

    assert((nbElts - prefix) % kEltsPerIter == 0);
    for (size_t elt = prefix; elt < nbElts; elt += kEltsPerIter) {
        __m128i values = _mm_loadu_si128((__m128i_u const*)&deltas[elt - 1]);
        values         = _mm_add_epi16(values, _mm_slli_si128(values, 8));
        values         = _mm_add_epi16(values, _mm_slli_si128(values, 4));
        values         = _mm_add_epi16(values, _mm_slli_si128(values, 2));
        values         = _mm_add_epi16(values, prev);
        prev           = _mm_shuffle_epi8(values, _mm_set1_epi16(0x0f0e));
        _mm_storeu_si128((__m128i_u*)&dst[elt], values);
    }
}

static void ZS_deltaDecode32_ssse3(
        uint32_t* dst,
        uint32_t first,
        uint32_t const* deltas,
        size_t nbElts)
{
    // TODO: Consider aligning either the source or the destination buffer,
    // or both. I've measured that this loop is 10% slower when working on
    // unaligned output buffers. Interestingly, the other vectorized delta
    // decoders aren't affected by the alignment.
    // We could either align here by passing a prefix to the scalar decoder.
    // Or ask zstrong to give us aligned buffers when it doesn't need an
    // extra memcpy to do so.
    assert(nbElts > 0);
    size_t const kEltsPerIter = sizeof(__m128i) / sizeof(*deltas);
    size_t const prefix = nbElts - nbEltsToVectorize(nbElts - 1, kEltsPerIter);

    assert(prefix >= 1);
    ZS_deltaDecode32_scalar(dst, first, deltas, prefix);

    __m128i prev = _mm_set1_epi32((int32_t)dst[prefix - 1]);

    assert((nbElts - prefix) % kEltsPerIter == 0);
    for (size_t elt = prefix; elt < nbElts; elt += kEltsPerIter) {
        __m128i values = _mm_loadu_si128((__m128i_u const*)&deltas[elt - 1]);
        values         = _mm_add_epi32(values, _mm_slli_si128(values, 8));
        values         = _mm_add_epi32(values, _mm_slli_si128(values, 4));
        values         = _mm_add_epi32(values, prev);
        prev           = _mm_shuffle_epi32(values, 0xff);
        _mm_storeu_si128((__m128i_u*)&dst[elt], values);
    }
}

#endif // ZL_HAS_SSSE3

#if ZL_HAS_NEON

#    include <arm_neon.h>

static void ZS_deltaDecode8_neon(
        uint8_t* dst,
        uint8_t first,
        uint8_t const* deltas,
        size_t nbElts)
{
    dst[0] = first;

    uint8x16_t const zero = vdupq_n_u8(0);
    uint8x16_t prev       = vdupq_n_u8(first);
    size_t n              = 1;
    for (; n + 64 <= nbElts; n += 64) {
        uint8x16_t v0 = vld1q_u8(deltas + n - 1);
        uint8x16_t v1 = vld1q_u8(deltas + n + 15);
        uint8x16_t v2 = vld1q_u8(deltas + n + 31);
        uint8x16_t v3 = vld1q_u8(deltas + n + 47);

        v0 = vaddq_u8(v0, vextq_u8(zero, v0, 8));
        v1 = vaddq_u8(v1, vextq_u8(zero, v1, 8));
        v2 = vaddq_u8(v2, vextq_u8(zero, v2, 8));
        v3 = vaddq_u8(v3, vextq_u8(zero, v3, 8));
        v0 = vaddq_u8(v0, vextq_u8(zero, v0, 12));
        v1 = vaddq_u8(v1, vextq_u8(zero, v1, 12));
        v2 = vaddq_u8(v2, vextq_u8(zero, v2, 12));
        v3 = vaddq_u8(v3, vextq_u8(zero, v3, 12));
        v0 = vaddq_u8(v0, vextq_u8(zero, v0, 14));
        v1 = vaddq_u8(v1, vextq_u8(zero, v1, 14));
        v2 = vaddq_u8(v2, vextq_u8(zero, v2, 14));
        v3 = vaddq_u8(v3, vextq_u8(zero, v3, 14));
        v0 = vaddq_u8(v0, vextq_u8(zero, v0, 15));
        v1 = vaddq_u8(v1, vextq_u8(zero, v1, 15));
        v2 = vaddq_u8(v2, vextq_u8(zero, v2, 15));
        v3 = vaddq_u8(v3, vextq_u8(zero, v3, 15));

        v1 = vaddq_u8(v1, vdupq_laneq_u8(v0, 15));
        v2 = vaddq_u8(v2, vdupq_laneq_u8(v1, 15));
        v3 = vaddq_u8(v3, vdupq_laneq_u8(v2, 15));
        vst1q_u8(dst + n + 0, vaddq_u8(prev, v0));
        vst1q_u8(dst + n + 16, vaddq_u8(prev, v1));
        vst1q_u8(dst + n + 32, vaddq_u8(prev, v2));
        vst1q_u8(dst + n + 48, vaddq_u8(prev, v3));
        prev = vaddq_u8(prev, vdupq_laneq_u8(v3, 15));
    }

    ZL_UNROLL_LOOP(1)
    for (; n < nbElts; ++n) {
        dst[n] = (uint8_t)(dst[n - 1] + deltas[n - 1]);
    }
}

static void ZS_deltaDecode16_neon(
        uint16_t* dst,
        uint16_t first,
        uint16_t const* deltas,
        size_t nbElts)
{
    dst[0] = first;

    uint16x8_t const zero = vdupq_n_u16(0);
    uint16x8_t one        = vdupq_n_u16(1);
    uint16x8_t prev       = vdupq_n_u16(first);
    size_t n              = 1;

    /* Keep MLA-by-one operations intact instead of rewriting them as DUP+ADD
     * pairs. The compiler guard requires `one` to be non-const. */
    ZL_COMPILER_GUARD_NEON_1(one);
    for (; n + 32 <= nbElts; n += 32) {
        uint16x8_t v0 = vld1q_u16(deltas + n - 1);
        uint16x8_t v1 = vld1q_u16(deltas + n + 7);
        uint16x8_t v2 = vld1q_u16(deltas + n + 15);
        uint16x8_t v3 = vld1q_u16(deltas + n + 23);

        v0 = vaddq_u16(v0, vextq_u16(zero, v0, 4));
        v1 = vaddq_u16(v1, vextq_u16(zero, v1, 4));
        v2 = vaddq_u16(v2, vextq_u16(zero, v2, 4));
        v3 = vaddq_u16(v3, vextq_u16(zero, v3, 4));
        v0 = vaddq_u16(v0, vextq_u16(zero, v0, 6));
        v1 = vaddq_u16(v1, vextq_u16(zero, v1, 6));
        v2 = vaddq_u16(v2, vextq_u16(zero, v2, 6));
        v3 = vaddq_u16(v3, vextq_u16(zero, v3, 6));
        v0 = vaddq_u16(v0, vextq_u16(zero, v0, 7));
        v1 = vaddq_u16(v1, vextq_u16(zero, v1, 7));
        v2 = vaddq_u16(v2, vextq_u16(zero, v2, 7));
        v3 = vaddq_u16(v3, vextq_u16(zero, v3, 7));

        v1 = vmlaq_laneq_u16(v1, one, v0, 7);
        v2 = vmlaq_laneq_u16(v2, one, v1, 7);
        v3 = vmlaq_laneq_u16(v3, one, v2, 7);
        vst1q_u16(dst + n + 0, vaddq_u16(prev, v0));
        vst1q_u16(dst + n + 8, vaddq_u16(prev, v1));
        vst1q_u16(dst + n + 16, vaddq_u16(prev, v2));
        vst1q_u16(dst + n + 24, vaddq_u16(prev, v3));
        prev = vmlaq_laneq_u16(prev, one, v3, 7);
    }

    ZL_UNROLL_LOOP(1)
    for (; n < nbElts; ++n) {
        dst[n] = (uint16_t)(dst[n - 1] + deltas[n - 1]);
    }
}

static void ZS_deltaDecode32_neon(
        uint32_t* dst,
        uint32_t first,
        uint32_t const* deltas,
        size_t nbElts)
{
    dst[0] = first;

    uint32x4_t const zero = vdupq_n_u32(0);
    uint32x4_t one        = vdupq_n_u32(1);
    uint32x4_t prev       = vdupq_n_u32(first);
    size_t n              = 1;

    /* Keep MLA-by-one operations intact instead of rewriting them as DUP+ADD
     * pairs. The compiler guard requires `one` to be non-const. */
    ZL_COMPILER_GUARD_NEON_1(one);
    for (; n + 16 <= nbElts; n += 16) {
        uint32x4_t v0 = vld1q_u32(deltas + n - 1);
        uint32x4_t v1 = vld1q_u32(deltas + n + 3);
        uint32x4_t v2 = vld1q_u32(deltas + n + 7);
        uint32x4_t v3 = vld1q_u32(deltas + n + 11);

        v0 = vaddq_u32(v0, vextq_u32(zero, v0, 2));
        v1 = vaddq_u32(v1, vextq_u32(zero, v1, 2));
        v2 = vaddq_u32(v2, vextq_u32(zero, v2, 2));
        v3 = vaddq_u32(v3, vextq_u32(zero, v3, 2));
        v0 = vaddq_u32(v0, vextq_u32(zero, v0, 3));
        v1 = vaddq_u32(v1, vextq_u32(zero, v1, 3));
        v2 = vaddq_u32(v2, vextq_u32(zero, v2, 3));
        v3 = vaddq_u32(v3, vextq_u32(zero, v3, 3));

        v1 = vmlaq_laneq_u32(v1, one, v0, 3);
        v2 = vmlaq_laneq_u32(v2, one, v1, 3);
        v3 = vmlaq_laneq_u32(v3, one, v2, 3);
        vst1q_u32(dst + n + 0, vaddq_u32(prev, v0));
        vst1q_u32(dst + n + 4, vaddq_u32(prev, v1));
        vst1q_u32(dst + n + 8, vaddq_u32(prev, v2));
        vst1q_u32(dst + n + 12, vaddq_u32(prev, v3));
        prev = vmlaq_laneq_u32(prev, one, v3, 3);
    }

    ZL_UNROLL_LOOP(1)
    for (; n < nbElts; ++n) {
        dst[n] = dst[n - 1] + deltas[n - 1];
    }
}

static void ZS_deltaDecode64_neon(
        uint64_t* dst,
        uint64_t first,
        uint64_t const* deltas,
        size_t nbElts)
{
    dst[0] = first;

    size_t n = 1;
    for (; n + 8 <= nbElts; n += 8) {
        uint64_t v0 = deltas[n - 1];
        uint64_t v1 = deltas[n + 0];
        uint64_t v2 = deltas[n + 1];
        uint64_t v3 = deltas[n + 2];
        uint64_t v4 = deltas[n + 3];
        uint64_t v5 = deltas[n + 4];
        uint64_t v6 = deltas[n + 5];
        uint64_t v7 = deltas[n + 6];

        v1 += v0;
        v0 += first;
        v3 += v2;
        v1 += first;
        v5 += v4;
        v7 += v6;

        /*
         * Preserve the parallel prefix-sum dependency tree. Without these
         * guards, compilers may favor fewer instructions by collapsing it
         * into a mostly serial dependency chain. Despite the smaller code,
         * the resulting loss of instruction-level parallelism is considerably
         * slower on out-of-order CPUs. The first guard also improves GCC code
         * generation on some CPUs, while Clang requires the additional guard
         * below to preserve the intended dependency structure.
         */
        ZL_COMPILER_GUARD_4(v1, v3, v5, v7);
        v2 += v1;
        v3 += v1;
        v6 += v5;
        v7 += v5;
        ZL_COMPILER_GUARD_CLANG_3(v3, v6, v7);

        v4 += v3;
        v5 += v3;
        v6 += v3;
        first = v7 + v3;

        dst[n + 0] = v0;
        dst[n + 1] = v1;
        dst[n + 2] = v2;
        dst[n + 3] = v3;
        dst[n + 4] = v4;
        dst[n + 5] = v5;
        dst[n + 6] = v6;
        dst[n + 7] = first;
    }

    ZL_UNROLL_LOOP(1)
    for (; n < nbElts; ++n) {
        dst[n] = dst[n - 1] + deltas[n - 1];
    }
}

#endif // ZL_HAS_NEON

void ZS_deltaDecode8(
        uint8_t* dst,
        uint8_t first,
        uint8_t const* deltas,
        size_t nbElts)
{
    if (nbElts == 0) {
        return;
    }
#if ZL_HAS_SSSE3
    ZS_deltaDecode8_ssse3(dst, first, deltas, nbElts);
#elif ZL_HAS_NEON
    ZS_deltaDecode8_neon(dst, first, deltas, nbElts);
#else
    ZS_deltaDecode8_scalar(dst, first, deltas, nbElts);
#endif
}

void ZS_deltaDecode16(
        uint16_t* dst,
        uint16_t first,
        uint16_t const* deltas,
        size_t nbElts)
{
    if (nbElts == 0) {
        return;
    }
#if ZL_HAS_SSSE3
    ZS_deltaDecode16_ssse3(dst, first, deltas, nbElts);
#elif ZL_HAS_NEON
    ZS_deltaDecode16_neon(dst, first, deltas, nbElts);
#else
    ZS_deltaDecode16_scalar(dst, first, deltas, nbElts);
#endif
}

void ZS_deltaDecode32(
        uint32_t* dst,
        uint32_t first,
        uint32_t const* deltas,
        size_t nbElts)
{
    if (nbElts == 0) {
        return;
    }
#if ZL_HAS_SSSE3
    ZS_deltaDecode32_ssse3(dst, first, deltas, nbElts);
#elif ZL_HAS_NEON
    ZS_deltaDecode32_neon(dst, first, deltas, nbElts);
#else
    ZS_deltaDecode32_scalar(dst, first, deltas, nbElts);
#endif
}

void ZS_deltaDecode64(
        uint64_t* dst,
        uint64_t first,
        uint64_t const* deltas,
        size_t nbElts)
{
    if (nbElts == 0) {
        return;
    }
#if ZL_HAS_NEON
    ZS_deltaDecode64_neon(dst, first, deltas, nbElts);
#else
    ZS_deltaDecode64_scalar(dst, first, deltas, nbElts);
#endif
}

void ZS_deltaDecode(
        void* dst,
        void const* first,
        void const* deltas,
        size_t nbElts,
        size_t eltWidth)
{
    if (nbElts == 0) {
        return;
    }
    switch (eltWidth) {
        case 1:
            ZS_deltaDecode8(
                    (uint8_t*)dst,
                    ZL_read8(first),
                    (uint8_t const*)deltas,
                    nbElts);
            break;
        case 2:
            ZS_deltaDecode16(
                    (uint16_t*)dst,
                    ZL_readLE16(first),
                    (uint16_t const*)deltas,
                    nbElts);
            break;
        case 4:
            ZS_deltaDecode32(
                    (uint32_t*)dst,
                    ZL_readLE32(first),
                    (uint32_t const*)deltas,
                    nbElts);
            break;
        case 8:
            ZS_deltaDecode64(
                    (uint64_t*)dst,
                    ZL_readLE64(first),
                    (uint64_t const*)deltas,
                    nbElts);
            break;
        default:
            assert(false);
            break;
    }
}
