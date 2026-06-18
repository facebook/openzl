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
#define ZL_LZ_DEFAULT_TABLE_LOG 14

#define ZL_LZ_MATCH_OVER_LENGTH 16
#define ZL_LZ_SEARCH_STRENGTH 8

uint32_t ZL_Lz_tableLog(uint32_t windowLog)
{
    return ZL_MIN(windowLog + 1, ZL_LZ_DEFAULT_TABLE_LOG);
}

// Returns the number of leading equal bytes (0..16) between the two 16-byte
// windows starting at `ip` and `match`. A result of 16 means all 16 bytes
// matched and the caller should keep scanning.
//
// NOTE: For performance optimization, this scalar loop is likely *faster* than
// any vectorized implementation. Indeed, this current loop replaces a
// vectorized implementation that was neutral to slower. More details are in
// D109051308.
ZL_FORCE_INLINE uint32_t
matchLength16(uint8_t const* const ip, uint8_t const* const match)
{
    const uint64_t match0 = ZL_readLE64(match);
    const uint64_t ip0    = ZL_readLE64(ip);
    const uint64_t mask0  = match0 ^ ip0;
    if (mask0 != 0) {
        return (uint32_t)ZL_ctz64(mask0) >> 3;
    }
    const uint64_t match1 = ZL_readLE64(match + 8);
    const uint64_t ip1    = ZL_readLE64(ip + 8);
    const uint64_t mask1  = match1 ^ ip1;
    if (mask1 != 0) {
        return 8 + ((uint32_t)ZL_ctz64(mask1) >> 3);
    }
    return 16;
}
static ptrdiff_t matchLength(
        uint8_t const* const in,
        ptrdiff_t inPos,
        ptrdiff_t matchPos,
        ptrdiff_t inEnd)
{
    {
        ZL_ASSERT_LE(inPos + 16, inEnd);
        const uint32_t len = matchLength16(in + inPos, in + matchPos);
        if (ZL_LIKELY(len < 16)) {
            return len;
        }
    }
    ptrdiff_t totalLength   = 16;
    const ptrdiff_t inLimit = inEnd - 16;
    while (inPos + totalLength < inLimit) {
        const uint32_t length = matchLength16(
                in + inPos + totalLength, in + matchPos + totalLength);
        if (length < 16) {
            return totalLength + length;
        }
        totalLength += 16;
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
    // Each real match sequence consumes at least MIN_MATCH bytes on average.
    // Overflow matches may emit shorter sequences, but average to >=
    // UINT16_MAX/2. Each overflow no-op sequence consumes ZL_LZ_MAX_LENGTH
    // literal bytes. Add 2 for the trailing literal sequence and rounding.
    return srcSize / ZL_LZ_MIN_MATCH + srcSize / ZL_LZ_MAX_LENGTH + 2;
}

static void
storeOffset(void* offsets, size_t offsetWidth, size_t seq, ptrdiff_t offset)
{
    ZL_ASSERT_GT(offset, 0);
    if (offsetWidth == sizeof(uint16_t)) {
        ZL_ASSERT_LE(offset, UINT16_MAX);
        ((uint16_t*)offsets)[seq] = (uint16_t)offset;
    } else {
        ZL_ASSERT_EQ(offsetWidth, sizeof(uint32_t));
        ZL_ASSERT_LE(offset, UINT32_MAX);
        ((uint32_t*)offsets)[seq] = (uint32_t)offset;
    }
}

static ptrdiff_t getMaxOffset(size_t offsetWidth, uint32_t windowLog)
{
    ptrdiff_t maxOffset = offsetWidth == sizeof(uint32_t)
            ? (ptrdiff_t)ZL_LZ_MAX_OFFSET_U32
            : (ptrdiff_t)ZL_LZ_MAX_OFFSET_U16;
    return ZL_MIN(maxOffset, (ptrdiff_t)(1u << windowLog));
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
        void* hashTableMem,
        uint32_t windowLog,
        int acceleration)
{
    if (srcSize == 0) {
        dst->numLiterals  = 0;
        dst->numSequences = 0;
        return;
    }

    assert(dst->literalsCapacity >= srcSize + ZL_LZ_LIT_OVER_LENGTH);
    assert(dst->sequencesCapacity >= ZL_Lz_maxNumSequences(srcSize));
    assert(dst->offsetWidth == sizeof(uint16_t)
           || dst->offsetWidth == sizeof(uint32_t));

    const uint32_t tableLog = ZL_Lz_tableLog(windowLog);
    ZS_FastTable table      = { 0, 0, 0 };
    ZS_FastTable_init(&table, hashTableMem, tableLog, ZL_LZ_HASH_LEN);

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
    void* const offsets       = dst->offsets;
    const size_t offsetWidth  = dst->offsetWidth;
    const ptrdiff_t maxOffset = getMaxOffset(offsetWidth, windowLog);

    size_t seq = 0;

    const ptrdiff_t kStepIncr = 1 << ZL_LZ_SEARCH_STRENGTH;
    const ptrdiff_t firstStep = ZL_MAX(acceleration, 1);
    ptrdiff_t step            = firstStep;
    ptrdiff_t nextStep        = inPos + kStepIncr;

    while (inPos <= inLimit) {
        const uint8_t* const inPtr = in + inPos;
        ptrdiff_t match            = ZS_FastTable_getAndUpdateT(
                &table, inPtr, (uint32_t)inPos, ZL_LZ_HASH_LEN);
        const ptrdiff_t distance = inPos - match;
        if (ZL_read32(in + match) == ZL_read32(inPtr) && distance < maxOffset) {
            ptrdiff_t ml = 4 + matchLength(in, inPos + 4, match + 4, inEnd);

            // Walk the match backwards
            while (match > 0 && inPos > inLitStart
                   && in[match - 1] == in[inPos - 1]) {
                --match;
                --inPos;
                ++ml;
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
                    storeOffset(offsets, offsetWidth, seq, 1);
                    ++seq;
                    ll -= ZL_LZ_MAX_LENGTH;
                }
                litLens[seq] = (uint16_t)ll;
            }
            if (ZL_LIKELY(ml <= ZL_LZ_MAX_LENGTH)) {
                matchLens[seq] = (uint16_t)ml;
                storeOffset(offsets, offsetWidth, seq, distance);
                ++seq;
            } else {
                // If the match length is too large, split it into multiple
                // sequences. The final match length may be < ZL_LZ_MIN_MATCH
                // but that is okay.
                ptrdiff_t remainingMatchLength = ml;
                while (remainingMatchLength > 0) {
                    ptrdiff_t const bounded =
                            ZL_MIN(remainingMatchLength, ZL_LZ_MAX_LENGTH);
                    // litlens[seq] is already set
                    matchLens[seq] = (uint16_t)bounded;
                    storeOffset(offsets, offsetWidth, seq, distance);
                    ++seq;

                    remainingMatchLength -= bounded;
                    litLens[seq] = 0;
                }
            }

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
            step       = firstStep;
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
