// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/mux_lengths/encode_mux_lengths_kernel.h"

#include "openzl/codecs/mux_lengths/common_mux_lengths_luts.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"

#if ZL_HAS_AVX2

static size_t ZL_muxLengthsEncode_2(
        uint8_t* muxedOut,
        uint16_t* longOut,
        const uint16_t* literalLengths,
        const uint16_t* matchLengths,
        size_t numElements,
        unsigned splitPoint,
        unsigned matchLengthBias)
{
    unsigned const llMask = (1u << splitPoint) - 1;
    unsigned const mlMask = (1u << (8 - splitPoint)) - 1;

    // Interleaved mask: [llMask, mlMask] repeated across 16 uint16_t lanes.
    __m256i const maskVec =
            _mm256_set1_epi32((int)((uint32_t)llMask | (mlMask << 16)));
    // Bias to subtract: [0, matchLengthBias] repeated.
    __m256i const biasVec =
            _mm256_set1_epi32((int)((uint32_t)matchLengthBias << 16));

    __m128i const shiftVec = _mm_cvtsi32_si128(16 - (int)splitPoint);

    // PSHUFB to extract token bytes from interleaved 16-bit tokens.
    // From [llTok0, mlTok0, llTok1, mlTok1, ...] (each uint16_t),
    // pick the low byte of each and pack into 8 consecutive bytes.
    // In each 128-bit lane: bytes 0,2,4,6,8,10,12,14 -> positions 0..7.
    __m256i const kTokenExtract = _mm256_setr_epi8(
            0,
            2,
            4,
            6,
            8,
            10,
            12,
            14,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            0,
            2,
            4,
            6,
            8,
            10,
            12,
            14,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80);

    // Extract the low byte of each 32-bit pair (the combined token).
    // Use pshufb to gather bytes 0,4,8,12 from each 128-bit lane.
    // But we have bytes at positions 0,4,8,12 in each lane.
    const __m256i kMuxGather = _mm256_setr_epi8(
            0,
            4,
            8,
            12,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            0,
            4,
            8,
            12,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80,
            (char)0x80);

    size_t count = 0;
    size_t i     = 0;

    size_t const vecEnd = numElements & ~(size_t)7;

    for (; i < vecEnd; i += 8) {
        // Load 8 literal lengths and 8 match lengths as 128-bit vectors.
        __m128i llRaw = _mm_loadu_si128((const __m128i_u*)(literalLengths + i));
        __m128i mlRaw = _mm_loadu_si128((const __m128i_u*)(matchLengths + i));

        // Interleave into one 256-bit vector:
        //   [ll0,ml0,ll1,ml1,ll2,ml2,ll3,ml3 | ll4,ml4,...,ll7,ml7]
        __m256i vals = _mm256_set_m128i(
                _mm_unpackhi_epi16(llRaw, mlRaw),
                _mm_unpacklo_epi16(llRaw, mlRaw));

        // Subtract matchLengthBias from ML (odd) positions.
        vals = _mm256_sub_epi16(vals, biasVec);

        // Compute token values: min(val, mask) per lane.
        // min(x, m) = x - subs(x, m) using saturating subtract.
        __m256i tokenVec =
                _mm256_sub_epi16(vals, _mm256_subs_epu16(vals, maskVec));

        {
            // Build muxed token bytes: llToken | (mlToken << splitPoint).
            // tokenVec has interleaved [llTok, mlTok, ...] as uint16_t.
            // In each 32-bit element: bits[15:0] = llTok, bits[31:16] = mlTok.
            // Shift RIGHT by (16 - splitPoint) to move mlTok down into
            // bits[splitPoint..splitPoint+7], while llTok (< 256 in bits[15:0])
            // vanishes entirely since 16 - splitPoint >= 8.
            const __m256i shifted = _mm256_srl_epi32(tokenVec, shiftVec);
            __m256i muxed         = _mm256_or_si256(tokenVec, shifted);
            muxed                 = _mm256_shuffle_epi8(muxed, kMuxGather);

            // Now lo lane has [tok0..tok3, 0...] and hi lane has [tok4..tok7,
            // 0...]. Extract lo 32 bits of each lane and combine.
            const uint32_t lo4 = (uint32_t)_mm256_extract_epi32(muxed, 0);
            const uint32_t hi4 = (uint32_t)_mm256_extract_epi32(muxed, 4);
            ZL_write32(muxedOut + i, lo4);
            ZL_write32(muxedOut + i + 4, hi4);
        }

        // Overflow values: val - mask (only meaningful where val >= mask).
        __m256i extraVec = _mm256_sub_epi16(vals, maskVec);

        // Detect which positions overflow (token == mask).
        __m256i eqMask = _mm256_cmpeq_epi16(tokenVec, maskVec);

        // Pack comparison results from 16-bit to 8-bit for movemask.
        // Use pshufb to extract one byte per 16-bit lane.
        __m256i eqPacked = _mm256_shuffle_epi8(eqMask, kTokenExtract);

        // movemask on the packed bytes: bits 0..7 from lo lane, 16..23 from
        // hi lane.
        uint32_t combinedMask = (uint32_t)_mm256_movemask_epi8(eqPacked);
        uint8_t loMask        = (uint8_t)(combinedMask & 0xFF);
        uint8_t hiMask        = (uint8_t)((combinedMask >> 16) & 0xFF);

        // Use LUT + pshufb to pack valid overflow values to the front.
        // Each LUT entry is a 128-bit gather control for one 128-bit lane.
        __m128i loShuf =
                _mm_load_si128((const __m128i*)ZL_kCompressU16LUT[loMask]);
        __m128i hiShuf =
                _mm_load_si128((const __m128i*)ZL_kCompressU16LUT[hiMask]);

        __m128i loExtra = _mm256_castsi256_si128(extraVec);
        __m128i hiExtra = _mm256_extracti128_si256(extraVec, 1);

        __m128i loPacked = _mm_shuffle_epi8(loExtra, loShuf);
        __m128i hiPacked = _mm_shuffle_epi8(hiExtra, hiShuf);

        size_t loCount = (size_t)ZL_popcount64((uint64_t)loMask);
        size_t hiCount = (size_t)ZL_popcount64((uint64_t)hiMask);

        _mm_storeu_si128((__m128i_u*)(longOut + count), loPacked);
        count += loCount;
        _mm_storeu_si128((__m128i_u*)(longOut + count), hiPacked);
        count += hiCount;
    }

    // Scalar tail
    for (; i < numElements; ++i) {
        uint16_t const ll = literalLengths[i];
        uint16_t const ml = matchLengths[i] - (uint16_t)matchLengthBias;

        uint8_t mux = 0;

        if (ll >= llMask) {
            mux |= (uint8_t)llMask;
            longOut[count] = (uint16_t)(ll - llMask);
            count++;
        } else {
            mux |= (uint8_t)ll;
        }

        if (ml >= mlMask) {
            mux |= (uint8_t)(mlMask << splitPoint);
            longOut[count] = (uint16_t)(ml - mlMask);
            count++;
        } else {
            mux |= (uint8_t)(ml << splitPoint);
        }

        muxedOut[i] = mux;
    }

    return count;
}
#endif /* ZL_HAS_AVX2 */

ZL_FORCE_INLINE size_t ZL_muxLengthsEncode_impl(
        uint8_t* muxedOut,
        uint8_t* longPtr,
        const uint8_t* llPtr,
        const uint8_t* mlPtr,
        size_t numElements,
        size_t eltWidth,
        unsigned splitPoint,
        unsigned matchLengthBias)
{
    unsigned const llMask = (1u << splitPoint) - 1;
    unsigned const mlMask = (1u << (8 - splitPoint)) - 1;

    size_t count = 0;

    for (size_t i = 0; i < numElements; ++i) {
        uint64_t const ll = ZL_readN(llPtr + i * eltWidth, eltWidth);
        uint64_t const ml = ZL_readN(mlPtr + i * eltWidth, eltWidth)
                - (uint64_t)matchLengthBias;

        uint8_t mux = 0;

        // Literal length overflow goes first when both overflow.
        if (ll >= llMask) {
            mux |= (uint8_t)llMask;
            ZL_writeN(longPtr + count * eltWidth, ll - llMask, eltWidth);
            count++;
        } else {
            mux |= (uint8_t)ll;
        }

        if (ml >= mlMask) {
            mux |= (uint8_t)(mlMask << splitPoint);
            ZL_writeN(longPtr + count * eltWidth, ml - mlMask, eltWidth);
            count++;
        } else {
            mux |= (uint8_t)(ml << splitPoint);
        }

        muxedOut[i] = mux;
    }

    return count;
}

size_t ZL_muxLengthsEncode(
        uint8_t* muxedOut,
        void* longOut,
        const void* literalLengths,
        const void* matchLengths,
        size_t numElements,
        size_t eltWidth,
        unsigned splitPoint,
        unsigned matchLengthBias)
{
    ZL_ASSERT_LE(splitPoint, 8);
    ZL_ASSERT_LE(matchLengthBias, 15);

#if ZL_HAS_AVX2
    if (eltWidth == 2) {
        return ZL_muxLengthsEncode_2(
                muxedOut,
                (uint16_t*)longOut,
                (const uint16_t*)literalLengths,
                (const uint16_t*)matchLengths,
                numElements,
                splitPoint,
                matchLengthBias);
    }
#endif

    const uint8_t* ll = (const uint8_t*)literalLengths;
    const uint8_t* ml = (const uint8_t*)matchLengths;
    uint8_t* longPtr  = (uint8_t*)longOut;
    switch (eltWidth) {
        case 1:
            return ZL_muxLengthsEncode_impl(
                    muxedOut,
                    longPtr,
                    ll,
                    ml,
                    numElements,
                    1,
                    splitPoint,
                    matchLengthBias);
        case 2:
            return ZL_muxLengthsEncode_impl(
                    muxedOut,
                    longPtr,
                    ll,
                    ml,
                    numElements,
                    2,
                    splitPoint,
                    matchLengthBias);
        case 4:
            return ZL_muxLengthsEncode_impl(
                    muxedOut,
                    longPtr,
                    ll,
                    ml,
                    numElements,
                    4,
                    splitPoint,
                    matchLengthBias);
        case 8:
            return ZL_muxLengthsEncode_impl(
                    muxedOut,
                    longPtr,
                    ll,
                    ml,
                    numElements,
                    8,
                    splitPoint,
                    matchLengthBias);
        default:
            ZL_ASSERT_LE(eltWidth, 8);
            return 0;
    }
}

ZL_FORCE_INLINE ZL_MuxLengthsSplitResult ZL_muxLengthsComputeSplitPoint_impl(
        const uint8_t* literalLengths,
        const uint8_t* matchLengths,
        size_t numElements,
        size_t eltWidth,
        unsigned matchLengthBias)
{
    uint32_t llStats[256] = { 0 };
    uint32_t mlStats[256] = { 0 };

    for (size_t i = 0; i < numElements; ++i) {
        uint64_t const ll = ZL_readN(literalLengths + i * eltWidth, eltWidth);
        uint64_t const ml = ZL_readN(matchLengths + i * eltWidth, eltWidth)
                - (uint64_t)matchLengthBias;
        ++llStats[ll < 256 ? ll : 255];
        ++mlStats[ml < 256 ? ml : 255];
    }

    size_t bestLLBits    = 0;
    size_t bestExtraLens = 2 * numElements + 1;
    for (size_t llBits = 1; llBits < 8; ++llBits) {
        size_t const llMask = (1u << llBits) - 1;
        size_t const mlMask = (1u << (8 - llBits)) - 1;

        size_t numExtraLL = 0;
        for (size_t i = llMask; i < 256; ++i) {
            numExtraLL += llStats[i];
        }
        size_t numExtraML = 0;
        for (size_t i = mlMask; i < 256; ++i) {
            numExtraML += mlStats[i];
        }
        size_t const numExtraLens = numExtraLL + numExtraML;

        if (numExtraLens < bestExtraLens) {
            bestExtraLens = numExtraLens;
            bestLLBits    = llBits;
        }
    }

    return (ZL_MuxLengthsSplitResult){ .splitPoint = bestLLBits,
                                       .numLong    = bestExtraLens };
}

ZL_MuxLengthsSplitResult ZL_muxLengthsComputeSplitPoint(
        const void* literalLengths,
        const void* matchLengths,
        size_t numElements,
        size_t eltWidth,
        unsigned matchLengthBias)
{
    const uint8_t* ll = (const uint8_t*)literalLengths;
    const uint8_t* ml = (const uint8_t*)matchLengths;
    switch (eltWidth) {
        case 1:
            return ZL_muxLengthsComputeSplitPoint_impl(
                    ll, ml, numElements, 1, matchLengthBias);
        case 2:
            return ZL_muxLengthsComputeSplitPoint_impl(
                    ll, ml, numElements, 2, matchLengthBias);
        case 4:
            return ZL_muxLengthsComputeSplitPoint_impl(
                    ll, ml, numElements, 4, matchLengthBias);
        case 8:
            return ZL_muxLengthsComputeSplitPoint_impl(
                    ll, ml, numElements, 8, matchLengthBias);
        default:
            ZL_ASSERT_FAIL("Impossible");
            return (ZL_MuxLengthsSplitResult){ 0, 0 };
    }
}

ZL_FORCE_INLINE size_t ZL_muxLengthsCountLong_impl(
        const uint8_t* literalLengths,
        const uint8_t* matchLengths,
        size_t numElements,
        size_t eltWidth,
        unsigned splitPoint,
        unsigned matchLengthBias)
{
    unsigned const llMask = (1u << splitPoint) - 1;
    unsigned const mlMask = (1u << (8 - splitPoint)) - 1;

    size_t count = 0;
    for (size_t i = 0; i < numElements; ++i) {
        uint64_t const ll = ZL_readN(literalLengths + i * eltWidth, eltWidth);
        uint64_t const ml = ZL_readN(matchLengths + i * eltWidth, eltWidth)
                - (uint64_t)matchLengthBias;
        count += (ll >= llMask);
        count += (ml >= mlMask);
    }
    return count;
}

size_t ZL_muxLengthsCountLong(
        const void* literalLengths,
        const void* matchLengths,
        size_t numElements,
        size_t eltWidth,
        unsigned splitPoint,
        unsigned matchLengthBias)
{
    const uint8_t* ll = (const uint8_t*)literalLengths;
    const uint8_t* ml = (const uint8_t*)matchLengths;
    switch (eltWidth) {
        case 1:
            return ZL_muxLengthsCountLong_impl(
                    ll, ml, numElements, 1, splitPoint, matchLengthBias);
        case 2:
            return ZL_muxLengthsCountLong_impl(
                    ll, ml, numElements, 2, splitPoint, matchLengthBias);
        case 4:
            return ZL_muxLengthsCountLong_impl(
                    ll, ml, numElements, 4, splitPoint, matchLengthBias);
        case 8:
            return ZL_muxLengthsCountLong_impl(
                    ll, ml, numElements, 8, splitPoint, matchLengthBias);
        default:
            ZL_ASSERT_LE(eltWidth, 8);
            return 0;
    }
}
