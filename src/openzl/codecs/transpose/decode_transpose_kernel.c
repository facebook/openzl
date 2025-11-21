// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "decode_transpose_kernel.h"

#include <assert.h>
#include <string.h>

#include "openzl/shared/portability.h"
#include "openzl/shared/utils.h"

// Local noinline attribute for decode_transpose_kernel
#ifdef _MSC_VER
#    define ZL_TRANSPOSE_DEC_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#    define ZL_TRANSPOSE_DEC_NOINLINE __attribute__((noinline))
#else
#    define ZL_TRANSPOSE_DEC_NOINLINE
#endif
/// Portable implementation, reasonably fast after inlining
static inline void ZS_transposeDecode_generic(
        uint8_t* restrict dst,
        const uint8_t* restrict src,
        const size_t nbElts,
        const size_t eltWidth)
{
    for (size_t elt = 0; elt < nbElts; ++elt) {
        for (size_t pos = 0; pos < eltWidth; ++pos) {
            dst[elt * eltWidth + pos] = src[pos * nbElts + elt];
        }
    }
}

void ZS_transposeDecode(
        void* dst,
        const void* src,
        size_t nbElts,
        size_t eltWidth)
{
    if (nbElts > 0) {
        assert(src != NULL);
        assert(dst != NULL);
    }
    assert(eltWidth > 0);

    if (eltWidth <= 8) {
        /// Call the optimized version
        uint8_t const* srcs[8];
        srcs[0] = (uint8_t const*)src;
        for (size_t i = 1; i < eltWidth; ++i) {
            srcs[i] = (uint8_t const*)srcs[i - 1] + nbElts;
        }
        ZS_splitTransposeDecode(dst, srcs, nbElts, eltWidth);
    } else {
        /// Fallback to scalar code
        ZS_transposeDecode_generic(dst, src, nbElts, eltWidth);
    }
}

/// Portable implementation of transpose
ZL_FORCE_INLINE void ZS_splitTransposeDecode_impl(
        uint8_t* restrict dst,
        const uint8_t* restrict* src,
        size_t nbElts,
        size_t eltWidth)
{
    for (size_t elt = 0; elt < nbElts; ++elt) {
        for (size_t pos = 0; pos < eltWidth; ++pos) {
            dst[elt * eltWidth + pos] = src[pos][elt];
        }
    }
}

#define ZS_GEN_SPLIT_TRANSPOSE_DECODE(kEltWidth)                       \
    static ZL_MAYBE_UNUSED_FUNCTION ZL_TRANSPOSE_DEC_NOINLINE void     \
    ZS_splitTransposeDecode_##kEltWidth(                               \
            uint8_t* dst, uint8_t const* restrict* src, size_t nbElts) \
    {                                                                  \
        ZS_splitTransposeDecode_impl(dst, src, nbElts, kEltWidth);     \
    }

ZS_GEN_SPLIT_TRANSPOSE_DECODE(2)
ZS_GEN_SPLIT_TRANSPOSE_DECODE(4)
ZS_GEN_SPLIT_TRANSPOSE_DECODE(8)

/// Handles all unoptimized eltWidth values (e.g. not 1, 2, 4, 8)
static ZL_TRANSPOSE_DEC_NOINLINE void ZS_splitTransposeDecode_generic(
        uint8_t* restrict dst,
        uint8_t const* restrict* src,
        size_t nbElts,
        size_t eltWidth)
{
    ZS_splitTransposeDecode_impl(dst, src, nbElts, eltWidth);
}

#if ZL_HAS_AVX2

#    include <immintrin.h>
#    if defined(__clang__) && defined(ZS_ENABLE_CLANG_PRAGMA)
#        define ZS_PRAGMA_VECTORIZE
#    endif

/// Optimized version for 2 byte transpose with AVX2
static ZL_TRANSPOSE_DEC_NOINLINE void ZS_splitTransposeDecode_2_avx2(
        uint8_t* restrict dst,
        const uint8_t* restrict* src,
        size_t nbElts)
{
    // Tell clang to vectorize the loop and emit a warning if vectorization
    // fails This will alert us if auto-vectorization starts to fail in our
    // -Werror builds Clang does a good job with the vectorization, we may be
    // able to get a tiny bit more performance, but this is pretty close to
    // optimal.
#    ifdef ZS_PRAGMA_VECTORIZE
#        pragma clang loop vectorize(enable)
#    endif
    for (size_t elt = 0; elt < nbElts; ++elt) {
        // We need to force unrolling, otherwise -O1 will fail to vectorize
        // and compilation will fail with -Werror
#    ifdef ZS_PRAGMA_VECTORIZE
#        pragma clang loop unroll(full)
#    endif
        for (size_t pos = 0; pos < 2; ++pos) {
            dst[2 * elt + pos] = src[pos][elt];
        }
    }
}

/// Optimized version for 4 byte transpose with AVX2
static ZL_TRANSPOSE_DEC_NOINLINE void ZS_splitTransposeDecode_4_avx2(
        uint8_t* restrict dst,
        const uint8_t* restrict* src,
        size_t nbElts)
{
    // Tell clang to vectorize the loop and emit a warning if vectorization
    // fails This will alert us if auto-vectorization starts to fail in our
    // -Werror builds Clang does a good job with the vectorization, we may be
    // able to get more performance, but this is pretty close to optimal.
#    ifdef ZS_PRAGMA_VECTORIZE
#        pragma clang loop vectorize(enable)
#    endif
    for (size_t elt = 0; elt < nbElts; ++elt) {
        // We need to force unrolling, otherwise -O1 will fail to vectorize
        // and compilation will fail with -Werror
#    ifdef ZS_PRAGMA_VECTORIZE
#        pragma clang loop unroll(full)
#    endif
        for (size_t pos = 0; pos < 4; ++pos) {
            dst[4 * elt + pos] = src[pos][elt];
        }
    }
}

static __m256i readUnaligned256(void const* src)
{
    return _mm256_loadu_si256((__m256i_u const*)src);
}

static void write256(void* dst, __m256i const data)
{
    _mm256_storeu_si256((__m256i_u*)dst, data);
}

static __m256i getPermuteMask(void)
{
    return _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);
}

static size_t nbEltsToVectorize(size_t nbElts, size_t eltsPerIter)
{
    return (nbElts / eltsPerIter) * eltsPerIter;
}

/// Optimized version for 8 byte transpose with AVX2
/// We need this because compilers don't do a great job
/// autovectorizing this loop. It is hard because it is
/// completely bound by the number of shuffles, and reducing
/// shuffles is too complex for current compilers.
static ZL_TRANSPOSE_DEC_NOINLINE void ZS_splitTransposeDecode_8_avx2(
        uint64_t* dst,
        uint8_t const* src[8],
        size_t nbElts)
{
    size_t const kEltsPerIter = 32;
    size_t const prefix = nbElts - nbEltsToVectorize(nbElts, kEltsPerIter);

    assert((nbElts - prefix) % kEltsPerIter == 0);

    // Transpose until the remaining nbElts are a multiple of kEltsPerIter.
    if (prefix) {
        ZS_splitTransposeDecode_8((uint8_t*)dst, src, prefix);
    }

    // Copy src into a local variable because otherwise clang will
    // reload the pointers from src[] every iteration.
    uint8_t const* localSrc[8];
    memcpy(localSrc, src, sizeof(localSrc));

    // Get the permute mask, it must be outlined to avoid a bad clang
    // optimization that hurts performance.
    __m256i const permute = getPermuteMask();

    __m256i ymm0[8], ymm1[8];
    for (size_t elt = prefix; elt < nbElts; elt += kEltsPerIter) {
        // Load 32-bytes from each source. We'll perform a 32x8 transpose,
        // producing 256 bytes of output total.
        for (size_t j = 0; j < 8; j++) {
            ymm0[j] = readUnaligned256(&localSrc[j][elt]);
        }
        // After load:
        // ymm0[0] = [00 08 10 18 ... 78 | 80 ... f8]
        // ymm0[1] = [01 09 11 19 ... 79 | 81 ... f9]

        // Interleave each pair of consecutive vectors 1 byte at a time.
        // After this we logically have 16x uint16_t per vector, though
        // the order isn't right.
        for (size_t j = 0; j < 4; j++) {
            // Interleave the low 64-bits of each 128-bit lane 1 byte at a
            // time.
            ymm1[j] = _mm256_unpacklo_epi8(ymm0[j * 2], ymm0[j * 2 + 1]);
            // interleave the high 64-bits of each 128-bit lane 1 byte at a
            // time.
            ymm1[4 + j] = _mm256_unpackhi_epi8(ymm0[j * 2], ymm0[j * 2 + 1]);
        }
        // After unpack*_epi8:
        // ymm1[0] = [00 01 08 09 10 11 18 19 20 21 28 29 30 31 38 39 | 80 ..]
        // ymm1[1] = [02 03 0a 0b 12 13 1a 1b 22 23 2a 2b 32 33 3a 3b | 82 ..]
        // ymm1[4] = [40 41 48 49 50 51 58 59 60 61 68 69 70 71 78 79 | c0 ..]

        // Interleave each pair of consecutive vectors 2 bytes at a time.
        // After this we have 4-byte consecutive numbers.
        // After this we logically have 8x uint32_t per vector, though the
        // order isn't right.
        for (size_t j = 0; j < 4; j++) {
            // Interleave the low 64-bits of each 128-bit lane 2 bytes at a
            // time.
            ymm0[j] = _mm256_unpacklo_epi16(ymm1[j * 2], ymm1[j * 2 + 1]);
            // interleave the high 64-bits of each 128-bit lane 2 bytes at a
            // time.
            ymm0[4 + j] = _mm256_unpackhi_epi16(ymm1[j * 2], ymm1[j * 2 + 1]);
        }
        // After unpack*_epi16:
        // ymm0[0] = [00 01 02 03 08 09 01 0b 10 11 12 13 18 19 1a 1b | 80 ..]
        // ymm0[4] = [20 21 22 23 28 29 2a 2b 30 31 32 33 38 39 3a 3b | a0 ..]

        // Fix the order of the 8x logical uint32_t's so the next operation
        // leaves everything in the right order.
        // For even vectors, the low 32-bytes of each 64-byte element should
        // be in the final position. For odd vectors the top 32-bytes should be
        // in the final position.
        for (size_t j = 0; j < 8; j++) {
            ymm0[j] = _mm256_permutevar8x32_epi32(ymm0[j], permute);
        }
        // After permute8x32_epi32 (where "x ..." == [x, x+1, x+2, x+3]):
        // ymm0[0] = [00 ... 80 ... 08 ... 88 ... | 10 ... 90 ... 18 ... 98 ..]
        // ymm0[1] = [04 ... 84 ... 0c ... 8c ... | 14 ... 94 ... 1c ... 9c ..]

        // Blend the vectors into the final positions.
        // We blend even vectors with the next vector shifted left by 32-bits.
        // We blend odd vectors with the previous vector shifted right by
        // 32-bits.
        for (size_t j = 0; j < 4; ++j) {
            ymm1[j]     = _mm256_slli_epi64(ymm0[j * 2 + 1], 32);
            ymm1[j]     = _mm256_blend_epi32(ymm0[j * 2], ymm1[j], 170);
            ymm1[4 + j] = _mm256_srli_epi64(ymm0[j * 2], 32);
            ymm1[4 + j] = _mm256_blend_epi32(ymm1[4 + j], ymm0[j * 2 + 1], 170);
        }
        // After shift & blend (where "x ..." == [x, x+1, ..., x+7]):
        // ymm1[0] = [00 ... 08 ... | 10 ... 18 ..]
        // ymm1[1] = [40 ... 48 ... | 50 ... 58 ..]
        // ymm1[4] = [80 ... 88 ... | 90 ... 98 ..]

        // Write the transposed data.
        // The vectors aren't stored in order. The code could probably be
        // modified to do that so this could be a loop, but this is simple
        // enough. And it doesn't matter for performance, since the compiler
        // puts all the vectors in registers.
        write256(&dst[elt + 0], ymm1[0]);
        write256(&dst[elt + 4], ymm1[2]);
        write256(&dst[elt + 8], ymm1[1]);
        write256(&dst[elt + 12], ymm1[3]);
        write256(&dst[elt + 16], ymm1[4]);
        write256(&dst[elt + 20], ymm1[6]);
        write256(&dst[elt + 24], ymm1[5]);
        write256(&dst[elt + 28], ymm1[7]);
    }
}

#endif // ZL_HAS_AVX2

void ZS_splitTransposeDecode(
        void* dst,
        uint8_t const** src,
        size_t nbElts,
        size_t eltWidth)
{
// TODO: Get the cpuid from the zstrong decoder context.
#if ZL_HAS_AVX2
    switch (eltWidth) {
        case 1:
            memcpy(dst, src[0], nbElts);
            break;
        case 2:
            ZS_splitTransposeDecode_2_avx2(dst, src, nbElts);
            break;
        case 4:
            ZS_splitTransposeDecode_4_avx2(dst, src, nbElts);
            break;
        case 8:
            ZS_splitTransposeDecode_8_avx2((uint64_t*)dst, src, nbElts);
            break;
        default:
            ZS_splitTransposeDecode_generic(dst, src, nbElts, eltWidth);
    }
#else
    switch (eltWidth) {
        case 1:
            memcpy(dst, src[0], nbElts);
            break;
        case 2:
            ZS_splitTransposeDecode_2(dst, src, nbElts);
            break;
        case 4:
            ZS_splitTransposeDecode_4(dst, src, nbElts);
            break;
        case 8:
            ZS_splitTransposeDecode_8(dst, src, nbElts);
            break;
        default:
            ZS_splitTransposeDecode_generic(dst, src, nbElts, eltWidth);
    }
#endif
}

#if 0
// This code can help with understanding the transpose operation
// Leaving it here for future developers working on optimizing
// this code.

#    include <stdio.h>
static void printVecs(__m256i const ymms[8])
{
    for (size_t i = 0; i < 8; ++i) {
        uint8_t data[32];
        write256(data, ymms[i], false);
        fprintf(stderr, "ymm%d = ", (int)i);
        for (size_t j = 0; j < 32; ++j) {
            if (1 || data[j] < 16 || data[j] >= 248
                || (data[j] >= 120 && data[j] < 136))
                fprintf(stderr, "%02x ", (int)data[j]);
            else
                fprintf(stderr, "   ");
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}

static void printExample(void)
{
    uint64_t dst[32];
    uint8_t src[8][32];

    for (size_t b = 0; b < 8; ++b) {
        for (size_t p = 0; p < 32; ++p) {
            uint8_t val = (uint8_t)(p * 8 + b);
            src[b][p]   = val;
        }
    }

    __m256i ymm0[8], ymm1[8];
    for (size_t j = 0; j < 8; j++) {
        ymm0[j] = readUnaligned256(&src[j][0]);
    }
    fprintf(stderr, "LOAD:\n");
    printVecs(ymm0);
    for (size_t j = 0; j < 4; j++) {
        /* Compute the low 32 bytes */
        ymm1[j] = _mm256_unpacklo_epi8(ymm0[j * 2], ymm0[j * 2 + 1]);
        /* Compute the hi 32 bytes */
        ymm1[4 + j] = _mm256_unpackhi_epi8(ymm0[j * 2], ymm0[j * 2 + 1]);
    }
    fprintf(stderr, "UNPACK8:\n");
    printVecs(ymm1);
    /* Shuffle words */
    for (size_t j = 0; j < 4; j++) {
        /* Compute the low 32 bytes */
        ymm0[j] = _mm256_unpacklo_epi16(ymm1[j * 2], ymm1[j * 2 + 1]);
        /* Compute the hi 32 bytes */
        ymm0[4 + j] = _mm256_unpackhi_epi16(ymm1[j * 2], ymm1[j * 2 + 1]);
    }
    fprintf(stderr, "UNPACK16:\n");
    printVecs(ymm0);
    for (size_t j = 0; j < 8; j++) {
        __m256i const permute = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);
        ymm0[j] = _mm256_permutevar8x32_epi32(ymm0[j], permute);
    }
    fprintf(stderr, "PERMUTE:\n");
    printVecs(ymm0);

    /* Shuffle 4-byte dwords */
    for (size_t j = 0; j < 4; j++) {
        ymm1[j]     = _mm256_slli_epi64(ymm0[j * 2 + 1], 32);
        ymm1[j]     = _mm256_blend_epi32(ymm0[j * 2], ymm1[j], 170);
        ymm1[4 + j] = _mm256_srli_epi64(ymm0[j * 2], 32);
        ymm1[4 + j] = _mm256_blend_epi32(ymm1[4 + j], ymm0[j * 2 + 1], 170);
    }
    fprintf(stderr, "UNPACK32:\n");
    printVecs(ymm1);
    /* Store the result vectors */
    write256(&dst[0], ymm1[0]);
    write256(&dst[4], ymm1[2]);
    write256(&dst[8], ymm1[1]);
    write256(&dst[12], ymm1[3]);
    write256(&dst[16], ymm1[4]);
    write256(&dst[20], ymm1[6]);
    write256(&dst[24], ymm1[5]);
    write256(&dst[28], ymm1[7]);
}
#endif
