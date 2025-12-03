// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <assert.h>
#include <string.h>

#include "encode_transpose_kernel.h"

#include "openzl/shared/portability.h"

// Local noinline attribute for encode_transpose_kernel
#ifdef _MSC_VER
#    define ZL_TRANSPOSE_ENC_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#    define ZL_TRANSPOSE_ENC_NOINLINE __attribute__((noinline))
#else
#    define ZL_TRANSPOSE_ENC_NOINLINE
#endif

/**
 * Portable implementation, reasonably fast after inlining
 */
static inline void ZS_transposeEncode_generic(
        char* restrict dst,
        const char* restrict src,
        const size_t nbElts,
        const size_t eltWidth)
{
    assert(eltWidth > 0);
    if (nbElts > 0) {
        assert(src != NULL);
        assert(dst != NULL);
    }

    for (size_t elt = 0; elt < nbElts; ++elt) {
        for (size_t pos = 0; pos < eltWidth; ++pos) {
            dst[pos * nbElts + elt] = src[elt * eltWidth + pos];
        }
    }
}

void ZS_transposeEncode(
        void* dst,
        const void* src,
        const size_t nbElts,
        const size_t eltWidth)
{
    switch (eltWidth) {
        case 2:
            ZS_transposeEncode_generic(dst, src, nbElts, 2);
            break;
        case 4:
            ZS_transposeEncode_generic(dst, src, nbElts, 4);
            break;
        case 8:
            ZS_transposeEncode_generic(dst, src, nbElts, 8);
            break;
        default:
            ZS_transposeEncode_generic(dst, src, nbElts, eltWidth);
            break;
    }
}

ZL_FORCE_INLINE
void ZS_splitTransposeEncode_impl(
        uint8_t** const restrict dst,
        uint8_t const* const restrict src,
        size_t const nbElts,
        size_t const eltWidth)
{
    // We might be able to do something smarter here
    // by using the optimized AVX implementation on
    // channels of the data, for example, deal with
    // 8 bytes out of each element in a time.
    // That, however, would require more work than
    // I'm willing to do at the moment.
    for (size_t elt = 0; elt < nbElts; ++elt) {
        for (size_t pos = 0; pos < eltWidth; ++pos) {
            if (elt % 8 == 0) {
                ZL_PREFETCH_L1(&dst[pos][elt + 128]);
            }
            if (pos % 8 == 0) {
                ZL_PREFETCH_L1(&src[elt * eltWidth + pos + 128]);
            }
            dst[pos][elt] = src[elt * eltWidth + pos];
        }
    }
}

#define ZS_GEN_SPLIT_TRANSPOSE_ENCODE(kEltWidth)                               \
    static ZL_TRANSPOSE_ENC_NOINLINE void ZS_splitTransposeEncode_##kEltWidth( \
            uint8_t** dst, uint8_t const* src, size_t const nbElts)            \
    {                                                                          \
        ZS_splitTransposeEncode_impl(dst, src, nbElts, kEltWidth);             \
    }

ZS_GEN_SPLIT_TRANSPOSE_ENCODE(2)
ZS_GEN_SPLIT_TRANSPOSE_ENCODE(4)
ZS_GEN_SPLIT_TRANSPOSE_ENCODE(8)

static ZL_TRANSPOSE_ENC_NOINLINE void ZS_splitTransposeEncode_generic(
        uint8_t** dst,
        uint8_t const* src,
        size_t const nbElts,
        size_t const eltWidth)
{
    ZS_splitTransposeEncode_impl(dst, src, nbElts, eltWidth);
}

#if ZL_HAS_AVX2
#    include <immintrin.h>

// ========================================
// AVX2 Transpose Split 2 - Implementation
// ========================================
#    define AVX2_TRANSPOSE_SPLIT2_BLEND_MASK 170

// Produces a 32-byte vector that can be used as a shuffle mask to permute
// groups of odd positions into the right order
ZL_FORCE_NOINLINE __m256i getTSplit2PermOddMask(void)
{
    return _mm256_set_epi32(7, 5, 3, 1, 6, 4, 2, 0);
}

// Produces a 32-byte vector that can be used as a shuffle mask to permute
// groups of even positions into the right order
ZL_FORCE_NOINLINE __m256i getTSplit2PermEvenMask(void)
{
    return _mm256_set_epi32(6, 4, 2, 0, 7, 5, 3, 1);
}

/// Optimized version for 2 byte split transpose with AVX2
ZL_FORCE_NOINLINE void ZS_splitTransposeEncode_2_avx2(
        uint8_t** restrict dst,
        uint8_t const* restrict src,
        size_t const nbElts)
{
    size_t const kBytesPerElt = 2;
    size_t const kEltsPerIter = 32;
    size_t const nbIter       = nbElts / kEltsPerIter;
    size_t const prefix       = nbElts - nbIter * kEltsPerIter;

    // This implementation involves transposing multiple 32x2 segments at a
    // time. Fallback to transposing one byte at a time until the remaining
    // `nbElts` is a multiple of `kEltsPerIter`
    if (prefix) {
        ZS_splitTransposeEncode_2(dst, src, prefix);
    }

    // Outline to avoid bad compiler optimizations
    __m256i const oddPermuteMask  = getTSplit2PermOddMask();
    __m256i const evenPermuteMask = getTSplit2PermEvenMask();

    __m256i ymm0[2];
    __m256i ymm1[2];
    __m256i oddEvenShuffleMask = _mm256_broadcastsi128_si256(
            _mm_set_epi8(15, 13, 11, 9, 14, 12, 10, 8, 7, 5, 3, 1, 6, 4, 2, 0));
    __m256i evenOddShuffleMask = _mm256_broadcastsi128_si256(
            _mm_set_epi8(14, 12, 10, 8, 15, 13, 11, 9, 6, 4, 2, 0, 7, 5, 3, 1));
    uint8_t const* nextSrc       = src + prefix * kBytesPerElt;
    uint8_t const* const lastSrc = src + nbElts * kBytesPerElt;
    uint8_t* nextDst[2]          = { dst[0] + prefix, dst[1] + prefix };
    while (nextSrc < lastSrc) {
        // Load 32-bytes into each register.
        ymm0[0] = _mm256_loadu_si256((__m256i_u const*)(nextSrc));
        ymm0[1] = _mm256_loadu_si256(
                (__m256i_u const*)(nextSrc + sizeof(__m256i)));
        // After load:
        // ymm0[0] = [00 01 02 ... 0f | 10 11 12 ... 1f]
        // ymm0[1] = [20 21 22 ... 2f | 30 31 32 ... 3f]

        // Shuffle data so odd / even positions are grouped together as
        // [odd even odd even | odd even odd even] and [even odd even odd | even
        // odd even odd]
        ymm0[0] = _mm256_shuffle_epi8(ymm0[0], oddEvenShuffleMask);
        ymm0[1] = _mm256_shuffle_epi8(ymm0[1], evenOddShuffleMask);
        // After shuffle:
        // ymm0[0] = [00 02 04 06 01 03 05 07 ... | 10 12 14 16 11 13 15 17 ...]
        // ymm1[1] = [21 23 25 27 20 22 24 26 ... | 31 33 35 37 30 32 34 36 ...]

        // Blend the data to group only odd / even positions in each register as
        // [odd odd odd odd | odd odd odd odd] and [even even even even | even
        // even even even]
        ymm1[0] = _mm256_blend_epi32(
                ymm0[0], ymm0[1], AVX2_TRANSPOSE_SPLIT2_BLEND_MASK);
        ymm1[1] = _mm256_blend_epi32(
                ymm0[1], ymm0[0], AVX2_TRANSPOSE_SPLIT2_BLEND_MASK);
        // After blend:
        // ymm1[0] = [00 02 04 06 20 22 24 26 ... | 10 12 14 16 30 32 34 36 ...]
        // ymm1[1] = [21 23 25 27 01 03 05 07 ... | 31 33 35 37 11 13 15 17 ...]

        // Permute odd / even groups into the right order
        ymm1[0] = _mm256_permutevar8x32_epi32(ymm1[0], oddPermuteMask);
        ymm1[1] = _mm256_permutevar8x32_epi32(ymm1[1], evenPermuteMask);
        // After permute:
        // ymm1[0] = [00 02 04 ... 0e 10 12 14 ... | 20 22 24 ... 2e 30 32 34
        // ...]
        // ymm1[1] = [01 03 05 ... 0f 11 13 15 ... | 21 23 25 ... 2f 31 33 35
        // ...]

        // Write the transpose data
        _mm256_storeu_si256((__m256i_u*)(nextDst[0]), ymm1[0]);
        _mm256_storeu_si256((__m256i_u*)(nextDst[1]), ymm1[1]);

        // Setup for next iteration
        nextSrc += kEltsPerIter * kBytesPerElt;
        nextDst[0] += kEltsPerIter;
        nextDst[1] += kEltsPerIter;
    }
}

// ====================================================
// AVX2 Transpose Split 4 - Implementation
// ====================================================

static void getTSplit4Group4Masks(__m256i* group4Masks)
{
    group4Masks[0] = _mm256_broadcastsi128_si256(
            _mm_set_epi8(15, 11, 7, 3, 14, 10, 6, 2, 13, 9, 5, 1, 12, 8, 4, 0));
    group4Masks[1] = _mm256_broadcastsi128_si256(
            _mm_set_epi8(14, 10, 6, 2, 13, 9, 5, 1, 12, 8, 4, 0, 15, 11, 7, 3));
    group4Masks[2] = _mm256_broadcastsi128_si256(
            _mm_set_epi8(13, 9, 5, 1, 12, 8, 4, 0, 15, 11, 7, 3, 14, 10, 6, 2));
    group4Masks[3] = _mm256_broadcastsi128_si256(
            _mm_set_epi8(12, 8, 4, 0, 15, 11, 7, 3, 14, 10, 6, 2, 13, 9, 5, 1));
}

static void getTSplit4Perm32Masks(__m256i* perm32Masks)
{
    perm32Masks[0] = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);
    perm32Masks[1] = _mm256_set_epi32(5, 1, 4, 0, 7, 3, 6, 2);
    perm32Masks[2] = _mm256_set_epi32(4, 0, 7, 3, 6, 2, 5, 1);
    perm32Masks[3] = _mm256_set_epi32(6, 2, 5, 1, 4, 0, 7, 3);
}

static ZL_TRANSPOSE_ENC_NOINLINE void ZS_splitTransposeEncode_4_avx2(
        uint8_t** restrict dst,
        uint8_t const* restrict src,
        size_t const nbElts)
{
    size_t const kBytesPerElt = 4;
    size_t const kEltsPerIter = 32;
    size_t const nbIter       = nbElts / kEltsPerIter;
    size_t const prefix       = nbElts - nbIter * kEltsPerIter;

    if (prefix) {
        ZS_splitTransposeEncode_4(dst, src, prefix);
    }

    __m256i ymm0[4];
    __m256i ymm1[4];
    __m256i group4Masks[4];
    __m256i perm32Masks[4];
    getTSplit4Group4Masks(group4Masks);
    getTSplit4Perm32Masks(perm32Masks);
    uint8_t const* nextSrc       = src + prefix * kBytesPerElt;
    const uint8_t* const lastSrc = src + nbElts * kBytesPerElt;
    uint8_t* nextDst[4]          = {
        dst[0] + prefix, dst[1] + prefix, dst[2] + prefix, dst[3] + prefix
    };
    while (nextSrc < lastSrc) {
        // Load 32-bytes into each vector
        for (size_t i = 0; i < 4; ++i) {
            ymm0[i] = _mm256_loadu_si256((__m256i_u const*)nextSrc);
            nextSrc += sizeof(__m256i);
        }

        // Shuffle elements into groups of 4
        for (size_t i = 0; i < 4; ++i) {
            ymm0[i] = _mm256_shuffle_epi8(ymm0[i], group4Masks[i]);
        }

        // Blend elements into groups of 8
        ymm1[0] = _mm256_blend_epi32(ymm0[0], ymm0[1], 0xAA);
        ymm1[1] = _mm256_blend_epi32(ymm0[2], ymm0[3], 0xAA);
        ymm1[2] = _mm256_blend_epi32(ymm0[3], ymm0[0], 0xAA);
        ymm1[3] = _mm256_blend_epi32(ymm0[1], ymm0[2], 0xAA);

        // Blend elements into groups of 16
        ymm0[0] = _mm256_blend_epi32(ymm1[0], ymm1[1], 0xCC);
        ymm0[1] = _mm256_blend_epi32(ymm1[1], ymm1[0], 0xCC);
        ymm0[2] = _mm256_blend_epi32(ymm1[2], ymm1[3], 0xCC);
        ymm0[3] = _mm256_blend_epi32(ymm1[3], ymm1[2], 0xCC);

        // Permute 4-byte groups into the correct 16-byte lane
        for (size_t i = 0; i < 4; ++i) {
            ymm0[i] = _mm256_permutevar8x32_epi32(ymm0[i], perm32Masks[i]);
        }

        // Store vectors into memory
        _mm256_storeu_si256((__m256i_u*)(nextDst[0]), ymm0[0]);
        _mm256_storeu_si256((__m256i_u*)(nextDst[2]), ymm0[1]);
        _mm256_storeu_si256((__m256i_u*)(nextDst[1]), ymm0[2]);
        _mm256_storeu_si256((__m256i_u*)(nextDst[3]), ymm0[3]);

        // Setup for the next iteration
        for (size_t i = 0; i < 4; ++i) {
            nextDst[i] += sizeof(__m256i);
        }
    }
}

// ====================================================
// AVX2 Transpose Split 8 - Implementation
// ====================================================

ZL_FORCE_NOINLINE __m256i getTSplit8Group4Mask(void)
{
    return _mm256_broadcastsi128_si256(_mm_set_epi8(
            -1, -1, 0, 0, -1, -1, 0, 0, -1, -1, 0, 0, -1, -1, 0, 0));
}

/// Optimized version for 8 byte split transpose with AVX2
ZL_FORCE_NOINLINE void ZS_splitTransposeEncode_8_avx2(
        uint8_t** restrict dst,
        uint8_t const* restrict src,
        size_t const nbElts)
{
    size_t const kBytesPerElt = 8;
    size_t const kEltsPerIter = 32;
    size_t const nbIter       = nbElts / kEltsPerIter;
    size_t const prefix       = nbElts - nbIter * kEltsPerIter;

    if (prefix) {
        ZS_splitTransposeEncode_8(dst, src, prefix);
    }

    // Outline to avoid a bad compiler optimization
    __m256i const group4Mask = getTSplit8Group4Mask();

    __m256i ymm0[8];
    __m256i ymm1[8];
    __m256i group2Masks[8] = {
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                15, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 0)),
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 0, 15, 7)),
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 0, 15, 7, 14, 6)),
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                12, 4, 11, 3, 10, 2, 9, 1, 8, 0, 15, 7, 14, 6, 13, 5)),
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                11, 3, 10, 2, 9, 1, 8, 0, 15, 7, 14, 6, 13, 5, 12, 4)),
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                10, 2, 9, 1, 8, 0, 15, 7, 14, 6, 13, 5, 12, 4, 11, 3)),
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                9, 1, 8, 0, 15, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2)),
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                8, 0, 15, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1)),
    };
    __m256i evenShuffleMasks[4] = {
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2)),
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10)),
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6)),
        _mm256_broadcastsi128_si256(_mm_set_epi8(
                13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14)),
    };
    __m256i group32Mask = _mm256_broadcastsi128_si256(
            _mm_set_epi8(15, 14, 11, 10, 13, 12, 9, 8, 7, 6, 3, 2, 5, 4, 1, 0));
    __m256i const permute32Masks[4] = {
        _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0),
        _mm256_set_epi32(5, 1, 4, 0, 7, 3, 6, 2),
        _mm256_set_epi32(4, 0, 7, 3, 6, 2, 5, 1),
        _mm256_set_epi32(6, 2, 5, 1, 4, 0, 7, 3),
    };
    uint8_t const* nextSrc       = src + prefix * kBytesPerElt;
    uint8_t const* const lastSrc = src + nbElts * kBytesPerElt;
    uint8_t* nextDst[8] = { dst[0] + prefix, dst[1] + prefix, dst[2] + prefix,
                            dst[3] + prefix, dst[4] + prefix, dst[5] + prefix,
                            dst[6] + prefix, dst[7] + prefix };
    while (nextSrc < lastSrc) {
        // Load 32-bytes into each vector
        for (size_t i = 0; i < 8; ++i) {
            ymm0[i] = _mm256_loadu_si256((__m256i_u const*)nextSrc);
            nextSrc += sizeof(__m256i);
        }
        // After load:
        // ymm0[i] = [1_1, 1_2, 1_3, ..., 1_8, 1_1, 1_2, 1_3, ..., 1_8]

        // Shuffle elements into groups of 2
        for (size_t i = 0; i < 8; ++i) {
            ymm0[i] = _mm256_shuffle_epi8(ymm0[i], group2Masks[i]);
        }
        // After shuffle:
        // ymm0[0] = [2_1, 2_2, 2_3, 2_4, 2_5, 2_6, 2_7, 2_8]
        // ymm0[1] = [2_8, 2_1, 2_2, 2_3, 2_4, 2_5, 2_6, 2_7]
        // ymm0[2] = [2_7, 2_8, 2_1, 2_2, 2_3, 2_4, 2_5, 2_6]
        // ymm0[3] = [2_6, 2_7, 2_8, 2_1, 2_2, 2_3, 2_4, 2_5]
        // ymm0[4] = [2_5, 2_6, 2_7, 2_8, 2_1, 2_2, 2_3, 2_4]
        // ymm0[5] = [2_4, 2_5, 2_6, 2_7, 2_8, 2_1, 2_2, 2_3]
        // ymm0[6] = [2_3, 2_4, 2_5, 2_6, 2_7, 2_8, 2_1, 2_2]
        // ymm0[7] = [2_2, 2_3, 2_4, 2_5, 2_6, 2_7, 2_8, 2_1]

        // Blend elements into groups of 4
        ymm1[0] = _mm256_blendv_epi8(
                ymm0[0], ymm0[1], group4Mask); // [4_1, 4_3, 4_5, 4_7]
        ymm1[1] = _mm256_blendv_epi8(
                ymm0[2], ymm0[3], group4Mask); // [4_7, 4_1, 4_3, 4_5]
        ymm1[2] = _mm256_blendv_epi8(
                ymm0[4], ymm0[5], group4Mask); // [4_5, 4_7, 4_1, 4_3]
        ymm1[3] = _mm256_blendv_epi8(
                ymm0[6], ymm0[7], group4Mask); // [4_3, 4_5, 4_7, 4_1]
        ymm1[4] = _mm256_blendv_epi8(
                ymm0[7], ymm0[0], group4Mask); // [4_2, 4_4, 4_6, 4_8]
        ymm1[5] = _mm256_blendv_epi8(
                ymm0[1], ymm0[2], group4Mask); // [4_8, 4_2, 4_4, 4_6]
        ymm1[6] = _mm256_blendv_epi8(
                ymm0[3], ymm0[4], group4Mask); // [4_6, 4_8, 4_2, 4_4]
        ymm1[7] = _mm256_blendv_epi8(
                ymm0[5], ymm0[6], group4Mask); // [4_4, 4_6, 4_8, 4_2]

        // Blend elements into groups of 8
        ymm0[0] = _mm256_blend_epi32(ymm1[0], ymm1[1], 0xAA); // [8_1, 8_5]
        ymm0[1] = _mm256_blend_epi32(ymm1[2], ymm1[3], 0xAA); // [8_5, 8_1]
        ymm0[2] = _mm256_blend_epi32(ymm1[3], ymm1[0], 0xAA); // [8_3, 8_7]
        ymm0[3] = _mm256_blend_epi32(ymm1[1], ymm1[2], 0xAA); // [8_7, 8_3]
        ymm0[4] = _mm256_blend_epi32(ymm1[4], ymm1[5], 0xAA); // [8_2, 8_6]
        ymm0[5] = _mm256_blend_epi32(ymm1[6], ymm1[7], 0xAA); // [8_6, 8_2]
        ymm0[6] = _mm256_blend_epi32(ymm1[7], ymm1[4], 0xAA); // [8_4, 8_8]
        ymm0[7] = _mm256_blend_epi32(ymm1[5], ymm1[6], 0xAA); // [8_8, 8_4]

        // Blend elements into groups of 16
        ymm1[0] = _mm256_blend_epi32(ymm0[0], ymm0[1], 0xCC); // [16_1]
        ymm1[1] = _mm256_blend_epi32(ymm0[1], ymm0[0], 0xCC); // [16_5]
        ymm1[2] = _mm256_blend_epi32(ymm0[2], ymm0[3], 0xCC); // [16_3]
        ymm1[3] = _mm256_blend_epi32(ymm0[3], ymm0[2], 0xCC); // [16_7]
        ymm1[4] = _mm256_blend_epi32(ymm0[4], ymm0[5], 0xCC); // [16_2]
        ymm1[5] = _mm256_blend_epi32(ymm0[5], ymm0[4], 0xCC); // [16_6]
        ymm1[6] = _mm256_blend_epi32(ymm0[6], ymm0[7], 0xCC); // [16_4]
        ymm1[7] = _mm256_blend_epi32(ymm0[7], ymm0[6], 0xCC); // [16_8]

        // Shuffle even rows into permutable groups
        for (size_t i = 0; i < 4; ++i) {
            ymm1[4 + i] = _mm256_shuffle_epi8(ymm1[4 + i], evenShuffleMasks[i]);
        }

        // Permute groups of 4 into the correct lane
        for (size_t i = 0; i < 4; ++i) {
            ymm1[i] = _mm256_permutevar8x32_epi32(ymm1[i], permute32Masks[i]);
            ymm1[i + 4] =
                    _mm256_permutevar8x32_epi32(ymm1[4 + i], permute32Masks[0]);
        }

        // Shuffle elements into the correct position
        for (size_t i = 0; i < 4; ++i) {
            ymm1[i]     = _mm256_shuffle_epi8(ymm1[i], group32Mask);
            ymm1[4 + i] = _mm256_shuffle_epi8(ymm1[4 + i], group32Mask);
        }

        // Store vectors in memory - vectors are out of order so manually
        // specify where they should go
        _mm256_storeu_si256((__m256i_u*)(nextDst[0]), ymm1[0]);
        _mm256_storeu_si256((__m256i_u*)(nextDst[4]), ymm1[1]);
        _mm256_storeu_si256((__m256i_u*)(nextDst[2]), ymm1[2]);
        _mm256_storeu_si256((__m256i_u*)(nextDst[6]), ymm1[3]);
        _mm256_storeu_si256((__m256i_u*)(nextDst[1]), ymm1[4]);
        _mm256_storeu_si256((__m256i_u*)(nextDst[5]), ymm1[5]);
        _mm256_storeu_si256((__m256i_u*)(nextDst[3]), ymm1[6]);
        _mm256_storeu_si256((__m256i_u*)(nextDst[7]), ymm1[7]);

        // Setup for the next iteration
        for (size_t i = 0; i < 8; ++i) {
            nextDst[i] += sizeof(__m256i);
        }
    }
}

#endif // ZL_HAS_AVX2

void ZS_splitTransposeEncode(
        uint8_t** dst,
        void const* src,
        size_t const nbElts,
        size_t const eltWidth)
{
#if ZL_HAS_AVX2
    switch (eltWidth) {
        case 1:
            memcpy(dst[0], src, nbElts);
            break;
        case 2:
            ZS_splitTransposeEncode_2_avx2(dst, src, nbElts);
            break;
        case 4:
            ZS_splitTransposeEncode_4_avx2(dst, src, nbElts);
            break;
        case 8:
            ZS_splitTransposeEncode_8_avx2(dst, src, nbElts);
            break;
        default:
            ZS_splitTransposeEncode_generic(dst, src, nbElts, eltWidth);
            break;
    }
#else
    switch (eltWidth) {
        case 1:
            memcpy(dst[0], src, nbElts);
            break;
        case 2:
            ZS_splitTransposeEncode_2(dst, src, nbElts);
            break;
        case 4:
            ZS_splitTransposeEncode_4(dst, src, nbElts);
            break;
        case 8:
            ZS_splitTransposeEncode_8(dst, src, nbElts);
            break;
        default:
            ZS_splitTransposeEncode_generic(dst, src, nbElts, eltWidth);
            break;
    }
#endif // ZL_HAS_AVX2
}
