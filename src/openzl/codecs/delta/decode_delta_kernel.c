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
    ZS_deltaDecode64_scalar(dst, first, deltas, nbElts);
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
