// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/lz/encode_lz_kernel.h"

#include <string.h>

#include "openzl/codecs/common/copy.h"
#include "openzl/codecs/common/fast_table.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/simd_wrapper.h"

// OpenZL uses uint16_t to emit literal lengths and match lengths so they cannot
// be longer than UINT16_MAX. In fuzzing build modes, instead limit to a shorter
// length so the fuzzer can find bugs related to overflowing the maximum lengths
// in small inputs.
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#    define ZL_LZ_MAX_LENGTH 1024
#else
#    define ZL_LZ_MAX_LENGTH UINT16_MAX
#endif

#define ZL_LZ_HASH_LEN 7

#define ZL_LZ_MATCH_OVER_LENGTH 16
#define ZL_LZ_SEARCH_STRENGTH 8

static ptrdiff_t matchLength(
        uint8_t const* const in,
        ptrdiff_t inPos,
        ptrdiff_t matchPos,
        ptrdiff_t inEnd)
{
    {
        ZL_ASSERT_LE(inPos + 16, inEnd);
        const ZL_Vec128 matchVec = ZL_Vec128_read(in + matchPos);
        const ZL_Vec128 ipVec    = ZL_Vec128_read(in + inPos);
        const ZL_Vec128 maskVec  = ZL_Vec128_cmp8(matchVec, ipVec);
        const uint32_t mask      = ZL_Vec128_mask8(maskVec);
        const uint32_t len       = (uint32_t)ZL_ctz32(~mask);
        if (ZL_LIKELY(len < 16)) {
            return len;
        }
    }
    ptrdiff_t totalLength   = 16;
    const ptrdiff_t inLimit = inEnd - 16;
    while (inPos + totalLength < inLimit) {
        const ZL_Vec128 matchVec = ZL_Vec128_read(in + matchPos + totalLength);
        const ZL_Vec128 ipVec    = ZL_Vec128_read(in + inPos + totalLength);
        const ZL_Vec128 maskVec  = ZL_Vec128_cmp8(matchVec, ipVec);
        const uint32_t mask      = ZL_Vec128_mask8(maskVec);
        const uint32_t length    = (uint32_t)ZL_ctz32(~mask);
        if (length < 16) {
            return totalLength + length;
        }
        totalLength += 16;
        if (ZL_UNLIKELY(totalLength > ZL_LZ_MAX_LENGTH)) {
            return ZL_LZ_MAX_LENGTH;
        }
    }

    while (inPos + totalLength < inEnd
           && in[inPos + totalLength] == in[matchPos + totalLength]) {
        ++totalLength;
    }
    return totalLength;
}

size_t ZL_Lz_maxNumSequences(size_t srcSize)
{
    if (srcSize == 0) {
        return 0;
    }
    // Each real match sequence consumes at least MIN_MATCH bytes.
    // Each overflow no-op sequence consumes ZL_LZ_MAX_LENGTH literal bytes.
    // Add 2 for the trailing literal sequence and rounding.
    return srcSize / ZL_LZ_MIN_MATCH + srcSize / ZL_LZ_MAX_LENGTH + 2;
}

/**
 * Match finding algorithm that runs the equivalent of the ZSTD_fast strategy.
 *
 * NOTE: This kernel uses ptrdiff_t rather than pointers to avoid UB with
 * pointers, which are only valid within the buffer and one past the end.
 */
void ZL_Lz_encode(
        ZL_Lz_OutSequences* dst,
        const uint8_t* const src,
        size_t srcSize,
        void* hashTableMem)
{
    if (srcSize == 0) {
        dst->numLiterals  = 0;
        dst->numSequences = 0;
        return;
    }

    assert(dst->literalsCapacity >= srcSize + ZL_LZ_LIT_OVER_LENGTH);
    assert(dst->sequencesCapacity >= ZL_Lz_maxNumSequences(srcSize));

    ZS_FastTable table = { 0, 0, 0 };
    ZS_FastTable_init(&table, hashTableMem, ZL_LZ_TABLE_LOG, ZL_LZ_HASH_LEN);

    const ptrdiff_t kSrcOverLength =
            ZL_MAX(ZL_LZ_LIT_OVER_LENGTH, ZL_LZ_MATCH_OVER_LENGTH);

    const uint8_t* const in = src;
    const ptrdiff_t inEnd   = (ptrdiff_t)srcSize;
    ptrdiff_t inLitStart    = 0;
    ptrdiff_t inPos         = 1;
    ptrdiff_t inLimit       = (ptrdiff_t)srcSize - kSrcOverLength;

    // Cache output pointers locally to avoid reloading through dst
    uint8_t* lits             = dst->literals;
    uint16_t* const litLens   = dst->literalLengths;
    uint16_t* const matchLens = dst->matchLengths;
    uint16_t* const offsets   = dst->offsets;

    size_t seq = 0;

    const ptrdiff_t kStepIncr = 1 << ZL_LZ_SEARCH_STRENGTH;
    ptrdiff_t step            = 1;
    ptrdiff_t nextStep        = inPos + kStepIncr;

    while (inPos <= inLimit) {
        const uint8_t* const inPtr = in + inPos;
        ptrdiff_t match            = ZS_FastTable_getAndUpdateT(
                &table, inPtr, (uint32_t)inPos, ZL_LZ_HASH_LEN);
        const ptrdiff_t distance = inPos - match;
        if (ZL_read32(in + match) == ZL_read32(inPtr)
            && distance <= ZL_LZ_MAX_OFFSET) {
            ptrdiff_t ml = 4 + matchLength(in, inPos + 4, match + 4, inEnd);

            // Walk the match backwards
            while (match > 0 && inPos > inLitStart
                   && in[match - 1] == in[inPos - 1]) {
                --match;
                --inPos;
                ++ml;
            }

            // Truncate match to fit in a uint16_t
            if (ZL_UNLIKELY(ml > ZL_LZ_MAX_LENGTH)) {
                ml = ZL_LZ_MAX_LENGTH;
            }

            // Copy literals
            size_t ll = (size_t)(inPos - inLitStart);
            assert(inPos + ZL_LZ_LIT_OVER_LENGTH <= (ptrdiff_t)srcSize);
            memcpy(lits, in + inLitStart, 16);
            if (ZL_UNLIKELY(ll > 16)) {
                assert(ZL_LZ_LIT_OVER_LENGTH >= ZS_WILDCOPY_OVERLENGTH);
                ZS_wildcopy(
                        lits, in + inLitStart, (ptrdiff_t)ll, ZS_wo_no_overlap);
            }
            lits += ll;

            // Store the sequence
            if (ZL_LIKELY(ll <= ZL_LZ_MAX_LENGTH)) {
                litLens[seq] = (uint16_t)ll;
            } else {
                // If the literal length is too large, split it into multiple
                // sequences with match length 0 and offset 1.
                while (ll > ZL_LZ_MAX_LENGTH) {
                    litLens[seq]   = ZL_LZ_MAX_LENGTH;
                    matchLens[seq] = 0;
                    offsets[seq]   = 1;
                    ++seq;
                    ll -= ZL_LZ_MAX_LENGTH;
                }
                litLens[seq] = (uint16_t)ll;
            }
            matchLens[seq] = (uint16_t)ml;
            offsets[seq]   = (uint16_t)distance;
            ++seq;

            // Update the hash table with positions at the start and end of the
            // match.
            // NOTE: Taken from zstd_fast.c
            ZS_FastTable_putT(
                    &table,
                    in + inPos + 2,
                    (uint32_t)(inPos + 2),
                    ZL_LZ_HASH_LEN);
            inPos += ml;
            if (inPos <= inLimit) {
                ZS_FastTable_putT(
                        &table,
                        in + inPos - 2,
                        (uint32_t)(inPos - 2),
                        ZL_LZ_HASH_LEN);
            }
            inLitStart = inPos;
            step       = 1;
            nextStep   = inPos + kStepIncr;
        } else {
            inPos += step;

            // This logic helps skip over incompressible data quickly by
            // progresssively speeding up every kStepIncr bytes and resetting
            // when a match is found.
            if (inPos >= nextStep) {
                ++step;
                nextStep += kStepIncr;
            }
        }
    }

    // Handle trailing literals
    const size_t lastLits = srcSize - (size_t)inLitStart;
    memcpy(lits, in + inLitStart, lastLits);
    lits += lastLits;

    dst->numLiterals  = (size_t)(lits - dst->literals);
    dst->numSequences = seq;

    assert(dst->numLiterals + ZL_LZ_LIT_OVER_LENGTH <= dst->literalsCapacity);
    assert(dst->numSequences <= dst->sequencesCapacity);
}
