// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/mux_lengths/decode_mux_lengths_kernel.h"

#include "openzl/codecs/mux_lengths/common_mux_lengths_luts.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"

#if ZL_HAS_SSSE3
#    include <tmmintrin.h>

/**
 * Optimized decoder for 2-byte lengths that uses SSSE3 to decode 4 muxed
 * lengths at a time.
 */
static ZL_MuxLengthsError ZL_muxLengthsDecode_2(
        uint16_t* llOut,
        uint16_t* mlOut,
        const uint8_t* muxedLengths,
        size_t numMuxed,
        const uint16_t* longLengths,
        size_t numLong,
        unsigned splitPoint,
        unsigned matchLengthBias)
{
    unsigned const llMask = (1u << splitPoint) - 1;
    unsigned const mlMask = (1u << (8 - splitPoint)) - 1;
    unsigned const mlMax  = (unsigned)matchLengthBias + mlMask;

    // Interleave LL and ML bytes from the two 64-bit halves produced by
    // _mm_set_epi64x(mux4 >> splitPoint, mux4):
    //   in:  [mux0, mux1, mux2, mux3, 0,0,0,0, shifted0..3, 0,0,0,0]
    //   out: [LL0, ML0, LL1, ML1, LL2, ML2, LL3, ML3, 0...]
    __m128i const kInterleaveShuffle = _mm_setr_epi8(
            0x00,
            0x08,
            0x01,
            0x09,
            0x02,
            0x0A,
            0x03,
            0x0B,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80);

    // Zero-extend interleaved bytes to uint16_t
    __m128i const kSpreadShuffle = _mm_setr_epi8(
            0x00,
            (char)0x80,
            0x01,
            (char)0x80,
            0x02,
            (char)0x80,
            0x03,
            (char)0x80,
            0x04,
            (char)0x80,
            0x05,
            (char)0x80,
            0x06,
            (char)0x80,
            0x07,
            (char)0x80);

    // Per-byte mask: llMask for even bytes (LL), mlMask for odd bytes (ML)
    __m128i const kBitMask =
            _mm_set1_epi16((short)((uint16_t)llMask | ((uint16_t)mlMask << 8)));

    // Add matchLengthBias to ML (odd) uint16_t positions, 0 to LL (even)
    __m128i const kMLBiasOffset = _mm_setr_epi16(
            0,
            (short)matchLengthBias,
            0,
            (short)matchLengthBias,
            0,
            (short)matchLengthBias,
            0,
            (short)matchLengthBias);

    // Extract LL values (even uint16_t positions 0,2,4,6) to low 64 bits
    __m128i const kExtractLL = _mm_setr_epi8(
            0x00,
            0x01,
            0x04,
            0x05,
            0x08,
            0x09,
            0x0C,
            0x0D,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80);

    // Extract ML values (odd uint16_t positions 1,3,5,7) to low 64 bits
    __m128i const kExtractML = _mm_setr_epi8(
            0x02,
            0x03,
            0x06,
            0x07,
            0x0A,
            0x0B,
            0x0E,
            0x0F,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80);

    // Buffer for the tail of longLengths so we can always do a full
    // 128-bit load without reading past the allocation.  Populated
    // once, the first time longPos gets within 8 of the end.
    ZL_ALIGNED(16) uint16_t longTail[16] = { 0 };
    const uint16_t* longSrc              = longLengths;

    size_t longPos = 0;
    size_t i       = 0;

    for (; i + 4 <= numMuxed; i += 4) {
        uint32_t const mux4 = ZL_read32(muxedLengths + i);

        // Low 64 bits: raw muxed bytes; high 64 bits: shifted by splitPoint
        __m128i lengthsV = _mm_set_epi64x(
                (long long)(uint64_t)(mux4 >> splitPoint),
                (long long)(uint64_t)mux4);

        // Interleave to [LL0, ML0, LL1, ML1, LL2, ML2, LL3, ML3]
        lengthsV = _mm_shuffle_epi8(lengthsV, kInterleaveShuffle);
        lengthsV = _mm_and_si128(lengthsV, kBitMask);

        // Detect which positions overflow (value == max)
        __m128i const needsExtraV = _mm_cmpeq_epi8(lengthsV, kBitMask);
        unsigned const needsExtra =
                (unsigned)_mm_movemask_epi8(needsExtraV) & 0xFFu;
        int const numNeeded = (int)ZL_popcount64((uint64_t)needsExtra);

        if (ZL_UNLIKELY(longPos + (size_t)numNeeded > numLong)) {
            return ZL_MuxLengthsError_longLengthsExhausted;
        }

        // Zero-extend interleaved bytes to uint16_t
        lengthsV = _mm_shuffle_epi8(lengthsV, kSpreadShuffle);
        lengthsV = _mm_add_epi16(lengthsV, kMLBiasOffset);

        // Switch to the tail buffer once we can no longer do a full
        // 128-bit load from the original array.
        if (ZL_UNLIKELY(longPos + 8 > numLong) && longSrc == longLengths) {
            size_t const remaining = numLong - longPos;
            memcpy(longTail,
                   longLengths + longPos,
                   remaining * sizeof(uint16_t));
            longSrc = longTail;
            longPos = 0;
            numLong = remaining;
        }

        // Load long lengths and scatter into overflow positions via LUT.
        __m128i extraLengthsV =
                _mm_loadu_si128((const __m128i_u*)(longSrc + longPos));
        __m128i const scatterV =
                _mm_load_si128((const __m128i*)ZL_kExpandU16LUT[needsExtra]);
        extraLengthsV = _mm_shuffle_epi8(extraLengthsV, scatterV);
        lengthsV      = _mm_add_epi16(lengthsV, extraLengthsV);

        longPos += (size_t)numNeeded;

        // Deinterleave into separate LL and ML streams and store
        _mm_storel_epi64(
                (__m128i_u*)(llOut + i),
                _mm_shuffle_epi8(lengthsV, kExtractLL));
        _mm_storel_epi64(
                (__m128i_u*)(mlOut + i),
                _mm_shuffle_epi8(lengthsV, kExtractML));
    }

    // Scalar tail
    for (; i < numMuxed; ++i) {
        uint8_t const mux = muxedLengths[i];
        uint64_t ll       = mux & llMask;
        uint64_t ml       = matchLengthBias + (mux >> splitPoint);

        if (ll == llMask) {
            if (longPos >= numLong) {
                return ZL_MuxLengthsError_longLengthsExhausted;
            }
            ll += longSrc[longPos++];
        }

        if (ml == mlMax) {
            if (longPos >= numLong) {
                return ZL_MuxLengthsError_longLengthsExhausted;
            }
            ml += longSrc[longPos++];
        }

        llOut[i] = (uint16_t)ll;
        mlOut[i] = (uint16_t)ml;
    }

    if (longPos != numLong) {
        return ZL_MuxLengthsError_longLengthsNotConsumed;
    }

    return ZL_MuxLengthsError_ok;
}
#endif /* ZL_HAS_SSSE3 */

ZL_FORCE_INLINE ZL_MuxLengthsError ZL_muxLengthsDecode_impl(
        uint8_t* llOut,
        uint8_t* mlOut,
        const uint8_t* muxedLengths,
        size_t numMuxed,
        const uint8_t* longPtr,
        size_t numLong,
        size_t eltWidth,
        unsigned splitPoint,
        unsigned matchLengthBias)
{
    unsigned const llMask = (1u << splitPoint) - 1;
    unsigned const mlMax =
            (unsigned)matchLengthBias + (1u << (8 - splitPoint)) - 1;

    size_t longPos = 0;

    for (size_t i = 0; i < numMuxed; ++i) {
        uint8_t const mux = muxedLengths[i];
        uint64_t ll       = mux & llMask;
        uint64_t ml       = matchLengthBias + (mux >> splitPoint);

        // Literal length overflow comes first when both overflow.
        if (ll == llMask) {
            if (longPos >= numLong) {
                return ZL_MuxLengthsError_longLengthsExhausted;
            }
            ll += ZL_readN(longPtr + longPos * eltWidth, eltWidth);
            longPos++;
        }

        if (ml == mlMax) {
            if (longPos >= numLong) {
                return ZL_MuxLengthsError_longLengthsExhausted;
            }
            ml += ZL_readN(longPtr + longPos * eltWidth, eltWidth);
            longPos++;
        }

        ZL_writeN(llOut + i * eltWidth, ll, eltWidth);
        ZL_writeN(mlOut + i * eltWidth, ml, eltWidth);
    }

    if (longPos != numLong) {
        return ZL_MuxLengthsError_longLengthsNotConsumed;
    }

    return ZL_MuxLengthsError_ok;
}

ZL_MuxLengthsError ZL_muxLengthsDecode(
        void* literalLengthsOut,
        void* matchLengthsOut,
        const uint8_t* muxedLengths,
        size_t numMuxed,
        const void* longLengths,
        size_t numLong,
        size_t eltWidth,
        unsigned splitPoint,
        unsigned matchLengthBias)
{
    ZL_ASSERT_LE(splitPoint, 8);
    ZL_ASSERT_LE(matchLengthBias, 15);

#if ZL_HAS_SSSE3
    if (eltWidth == 2) {
        return ZL_muxLengthsDecode_2(
                (uint16_t*)literalLengthsOut,
                (uint16_t*)matchLengthsOut,
                muxedLengths,
                numMuxed,
                (const uint16_t*)longLengths,
                numLong,
                splitPoint,
                matchLengthBias);
    }
#endif

    uint8_t* llOut         = (uint8_t*)literalLengthsOut;
    uint8_t* mlOut         = (uint8_t*)matchLengthsOut;
    const uint8_t* longPtr = (const uint8_t*)longLengths;
    switch (eltWidth) {
        case 1:
            return ZL_muxLengthsDecode_impl(
                    llOut,
                    mlOut,
                    muxedLengths,
                    numMuxed,
                    longPtr,
                    numLong,
                    1,
                    splitPoint,
                    matchLengthBias);
        case 2:
            return ZL_muxLengthsDecode_impl(
                    llOut,
                    mlOut,
                    muxedLengths,
                    numMuxed,
                    longPtr,
                    numLong,
                    2,
                    splitPoint,
                    matchLengthBias);
        case 4:
            return ZL_muxLengthsDecode_impl(
                    llOut,
                    mlOut,
                    muxedLengths,
                    numMuxed,
                    longPtr,
                    numLong,
                    4,
                    splitPoint,
                    matchLengthBias);
        case 8:
            return ZL_muxLengthsDecode_impl(
                    llOut,
                    mlOut,
                    muxedLengths,
                    numMuxed,
                    longPtr,
                    numLong,
                    8,
                    splitPoint,
                    matchLengthBias);
        default:
            ZL_ASSERT_FAIL("Impossible");
            return ZL_MuxLengthsError_ok;
    }
}
