// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/float_deconstruct/encode_float_deconstruct_kernel.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/utils.h"

#if ZL_HAS_AVX2
#    include <immintrin.h>

static size_t const DWORDS_PER_AVX2_VEC = 8;
static size_t const WORDS_PER_AVX2_VEC  = 16;
static size_t const BYTES_PER_QWORD     = 8;

// We need macros here because C11 lacks constexpr
#    define AVX2_FLOAT32_BATCH_SIZE 4
#    define AVX2_BFLOAT16_BATCH_SIZE 4
#endif

static void float32_deconstruct_encode_scalar(
        uint32_t const* __restrict const src32,
        uint8_t* __restrict const exponent,
        uint8_t* __restrict const signFrac,
        size_t const nbElts)
{
    if (nbElts > 0) {
        for (size_t i = 0; i < nbElts - 1; ++i) {
            uint32_t const f32 = src32[i];
            exponent[i]        = (uint8_t)(f32 >> 23) & 0xff;

            // Write 4 bytes with overlap of 1 rather than writing 3 bytes.
            // This helps the auto-vectorizer generate code which runs 50%
            // faster for small buffers.
            ZL_writeCE32(signFrac + 3 * i, ((f32 << 1) | (f32 >> 31)));
        }

        // Deconstruct the final element separately to prevent overflow
        size_t finalIdx    = nbElts - 1;
        uint32_t const f32 = src32[finalIdx];
        exponent[finalIdx] = (uint8_t)(f32 >> 23) & 0xff;
        ZL_writeCE24(signFrac + 3 * finalIdx, ((f32 << 1) | (f32 >> 31)));
    }
}

#if ZL_HAS_AVX2

// Outline the mask definition to prevent a bad compiler optimization
ZL_FORCE_NOINLINE __m256i getFloat32CrossLaneShuffleMask(void)
{
    return _mm256_setr_epi32(0, 1, 2, 4, 5, 6, 3, 7);
}

static void float32_deconstruct_encode_AVX2(
        uint32_t const* __restrict const src32,
        uint8_t* __restrict const exponent,
        uint8_t* __restrict const signFrac,
        size_t const nbElts)
{
    // Some of the steps in this function depend on endianness.
    // We can assume a little-endian environment since AVX2 is
    // an extension of x86, which is little-endian.
    ZL_ASSERT(ZL_isLittleEndian());

    size_t const nbSrcVecs         = nbElts / DWORDS_PER_AVX2_VEC;
    __m256i_u const* const srcVecs = (__m256i_u const*)src32;
    __m256i_u* const exponentVecs  = (__m256i_u*)exponent;
    size_t signFracPos             = 0;

    __m128i const inLaneShuffleMask128 =
            _mm_setr_epi8(0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, 3, 7, 11, 15);
    __m256i const inLaneShuffleMask =
            _mm256_setr_m128i(inLaneShuffleMask128, inLaneShuffleMask128);
    __m256i const crossLaneShuffleMask = getFloat32CrossLaneShuffleMask();

    // Encode 256-bit vectors in groups of 4. We stop at least one vector
    // early to avoid overflow on the write to signFrac. The real stopping
    // point is somewhere inside the last vector, but stopping there would
    // add some complexity for very little benefit.
    size_t currVecBatch;
    for (currVecBatch = 0;
         currVecBatch + 1 < (nbSrcVecs + 3) / AVX2_FLOAT32_BATCH_SIZE;
         currVecBatch++) {
        __m256i v[AVX2_FLOAT32_BATCH_SIZE];
        for (size_t currVecOffset = 0; currVecOffset < AVX2_FLOAT32_BATCH_SIZE;
             currVecOffset++) {
            // Load the next src32 vec
            size_t const currVecIdx =
                    (currVecBatch * AVX2_FLOAT32_BATCH_SIZE) + currVecOffset;
            __m256i const w1 = _mm256_loadu_si256(&srcVecs[currVecIdx]);

            // Move sign bit to LSB position
            __m256i const w2 = _mm256_or_si256(
                    _mm256_slli_epi32(w1, 1), _mm256_srli_epi32(w1, 31));

            // Shuffle such that low 3 qwords are signFrac and
            // high qword is exponent */
            __m256i const w3 = _mm256_shuffle_epi8(w2, inLaneShuffleMask);
            __m256i const w4 =
                    _mm256_permutevar8x32_epi32(w3, crossLaneShuffleMask);

            // Write out low 3 qwords, overlap high qword of previous write
            _mm256_storeu_si256((__m256i_u*)(signFrac + signFracPos), w4);
            signFracPos += 3 * BYTES_PER_QWORD;

            // Save intermediate value for exponent consolidation after loop
            v[currVecOffset] = w4;
        }

        // Combine exponent bytes from high qwords of v[0] through v[3]
        __m256i const z1 = _mm256_unpackhi_epi64(v[0], v[1]);
        __m256i const z2 = _mm256_unpackhi_epi64(v[2], v[3]);

        // Combine high dqwords of z1 and z2 into z3
        __m256i const z3 = _mm256_permute2x128_si256(z1, z2, 0x31);

        // Write out exponent bytes for v[0] through v[3]
        _mm256_storeu_si256(&exponentVecs[currVecBatch], z3);
    }

    // Encode the remaining elements. The vectorized loop above leaves
    // between 1 and 4 full source vectors unprocessed, corresponding to
    // between 8 and 32 elements, plus any remaining elements past the final
    // full source vector.
    size_t const nbEltsEncoded =
            DWORDS_PER_AVX2_VEC * currVecBatch * AVX2_FLOAT32_BATCH_SIZE;
    size_t const nbEltsRemaining = nbElts - nbEltsEncoded;

    float32_deconstruct_encode_scalar(
            src32 + nbEltsEncoded,
            exponent + nbEltsEncoded,
            signFrac + 3 * nbEltsEncoded,
            nbEltsRemaining);
}
#endif

static void bfloat16_deconstruct_encode_scalar(
        uint16_t const* __restrict const src16,
        uint8_t* __restrict const exponent,
        uint8_t* __restrict const signFrac,
        size_t const nbElts)
{
    for (size_t i = 0; i < nbElts; ++i) {
        uint16_t bf  = src16[i];
        exponent[i]  = (uint8_t)(bf >> 7);
        uint8_t sign = (uint8_t)(bf >> 15);
        uint8_t frac = (uint8_t)(bf << 1);
        signFrac[i]  = sign | frac;
    }
}

#if ZL_HAS_AVX2
static void bfloat16_deconstruct_encode_AVX2(
        uint16_t const* __restrict const src16,
        uint8_t* __restrict const exponent,
        uint8_t* __restrict const signFrac,
        size_t const nbElts)
{
    // Some of the steps in this function depend on endianness.
    // We can assume a little-endian environment since AVX2 is
    // an extension of x86, which is little-endian.
    ZL_ASSERT(ZL_isLittleEndian());

    size_t const nbSrcVecs         = nbElts / WORDS_PER_AVX2_VEC;
    __m256i_u const* const srcVecs = (__m256i_u const*)src16;
    __m256i_u* const exponentVecs  = (__m256i_u*)exponent;
    __m256i_u* const signFracVecs  = (__m256i_u*)signFrac;

    __m128i const shuffle0Mask128 =
            _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15);
    __m128i const shuffle1Mask128 =
            _mm_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15, 0, 2, 4, 6, 8, 10, 12, 14);
    __m256i const shuffle0Mask =
            _mm256_set_m128i(shuffle0Mask128, shuffle0Mask128);
    __m256i const shuffle1Mask =
            _mm256_set_m128i(shuffle1Mask128, shuffle1Mask128);

    // Encode 256-bit vectors in groups of 4.
    size_t currVecBatch;
    for (currVecBatch = 0; currVecBatch < nbSrcVecs / AVX2_BFLOAT16_BATCH_SIZE;
         currVecBatch++) {
        __m256i v[AVX2_BFLOAT16_BATCH_SIZE];
        for (size_t i = 0; i < AVX2_BFLOAT16_BATCH_SIZE; i++) {
            // Load the next src16 vec
            size_t const currVecIdx =
                    (currVecBatch * AVX2_BFLOAT16_BATCH_SIZE) + i;
            __m256i const w1 = _mm256_loadu_si256(&srcVecs[currVecIdx]);

            // Rotate sign bit from MSB to LSB position
            __m256i const w2 = _mm256_slli_epi16(w1, 1);
            __m256i const w3 = _mm256_srli_epi16(w1, 15);
            __m256i const w4 = _mm256_or_si256(w2, w3);

            // Save intermediate value for consolidation across vectors
            v[i] = w4;
        }

        // Consolidate exponent and signFrac bytes across vectors
        for (int i = 0; i < AVX2_BFLOAT16_BATCH_SIZE; i += 2) {
            __m256i const x1 = _mm256_shuffle_epi8(v[i + 0], shuffle0Mask);
            __m256i const x2 = _mm256_shuffle_epi8(v[i + 1], shuffle1Mask);
            __m256i const x3 = _mm256_blend_epi32(x1, x2, 0xcc);
            __m256i const x4 = _mm256_blend_epi32(x1, x2, 0x33);
            v[i + 0]         = _mm256_permute4x64_epi64(x3, 0xD8);
            v[i + 1]         = _mm256_permute4x64_epi64(x4, 0x8D);
        }

        // Store signFrac and exponent vectors
        size_t const outVecIdx = currVecBatch * AVX2_BFLOAT16_BATCH_SIZE / 2;
        _mm256_storeu_si256(&signFracVecs[outVecIdx], v[0]);
        _mm256_storeu_si256(&exponentVecs[outVecIdx], v[1]);
        _mm256_storeu_si256(&signFracVecs[outVecIdx + 1], v[2]);
        _mm256_storeu_si256(&exponentVecs[outVecIdx + 1], v[3]);
    }

    // Encode the remaining elements. The vectorized loop above leaves
    // between 0 and 3 full source vectors unprocessed, corresponding to
    // between 0 and 48 elements, plus any remaining elements past the final
    // full source vector.
    size_t const nbEltsEncoded =
            WORDS_PER_AVX2_VEC * currVecBatch * AVX2_BFLOAT16_BATCH_SIZE;
    size_t const nbEltsRemaining = nbElts - nbEltsEncoded;

    bfloat16_deconstruct_encode_scalar(
            src16 + nbEltsEncoded,
            exponent + nbEltsEncoded,
            signFrac + nbEltsEncoded,
            nbEltsRemaining);
}
#endif

static ZL_MAYBE_UNUSED_FUNCTION void float16_deconstruct_encode_scalar(
        uint16_t const* __restrict const src16,
        uint8_t* __restrict const exponent,
        uint8_t* __restrict const signFrac,
        size_t const nbElts)
{
    for (size_t i = 0; i < nbElts; ++i) {
        uint16_t f       = src16[i];
        uint16_t expFrac = (uint16_t)(f << 1);
        exponent[i]      = (uint8_t)(expFrac >> 11);
        uint16_t sign    = (uint16_t)(f >> 15);
        uint16_t frac    = expFrac & ((1 << 11) - 1);
        ZL_writeCE16(signFrac + 2 * i, frac | sign);
    }
}

#if ZL_HAS_AVX2
static void float16_deconstruct_encode_AVX2(
        uint16_t const* __restrict const src16,
        uint8_t* __restrict const exponent,
        uint8_t* __restrict const signFrac,
        size_t const nbElts)
{
// TODO(embg): support recent GCC
#    if defined(__clang__) && defined(NDEBUG)
// Adding this pragma helps clang generate optimal code when AVX2
// instructions are available. The generated code is 20% faster, thanks to
// better instructions for byte-packing.
#        pragma clang loop vectorize_width(64)
#    endif
    for (size_t i = 0; i < nbElts; ++i) {
        uint16_t f       = src16[i];
        uint16_t expFrac = (uint16_t)(f << 1);
        exponent[i]      = (uint8_t)(expFrac >> 11);
        uint16_t sign    = (uint16_t)(f >> 15);
        uint16_t frac    = expFrac & ((1 << 11) - 1);
        ZL_writeCE16(signFrac + 2 * i, frac | sign);
    }
}
#endif

void FLTDECON_float32_deconstruct_encode(
        uint32_t const* __restrict const src32,
        uint8_t* __restrict const exponent,
        uint8_t* __restrict const signFrac,
        size_t const nbElts)
{
#if ZL_HAS_AVX2
    float32_deconstruct_encode_AVX2(src32, exponent, signFrac, nbElts);
#else
    float32_deconstruct_encode_scalar(src32, exponent, signFrac, nbElts);
#endif
}

void FLTDECON_bfloat16_deconstruct_encode(
        uint16_t const* __restrict const src16,
        uint8_t* __restrict const exponent,
        uint8_t* __restrict const signFrac,
        size_t const nbElts)
{
#if ZL_HAS_AVX2
    bfloat16_deconstruct_encode_AVX2(src16, exponent, signFrac, nbElts);
#else
    bfloat16_deconstruct_encode_scalar(src16, exponent, signFrac, nbElts);
#endif
}

void FLTDECON_float16_deconstruct_encode(
        uint16_t const* __restrict const src16,
        uint8_t* __restrict const exponent,
        uint8_t* __restrict const signFrac,
        size_t const nbElts)
{
#if ZL_HAS_AVX2
    float16_deconstruct_encode_AVX2(src16, exponent, signFrac, nbElts);
#else
    float16_deconstruct_encode_scalar(src16, exponent, signFrac, nbElts);
#endif
}
