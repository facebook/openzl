// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/lz/encode_lz_kernel.h"

#include <string.h>

#include "openzl/codecs/common/copy.h"
#include "openzl/codecs/common/fast_table.h"
#include "openzl/codecs/common/row_table.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/utils.h"

// OpenZL uses uint16_t to emit literal lengths and match lengths so they cannot
// be longer than UINT16_MAX. In fuzzing build modes, instead limit to a shorter
// length so the fuzzer can find bugs related to overflowing the maximum lengths
// in small inputs.
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#    define ZL_LZ_MAX_LENGTH 1024
#else
#    define ZL_LZ_MAX_LENGTH UINT16_MAX
#endif

#define ZL_LZ_DEFAULT_TABLE_LOG 14

#define ZL_LZ_MATCH_OVER_LENGTH 16
#define ZL_LZ_SEARCH_STRENGTH 8

// clang-format off
static const ZL_LzParameters kDefaultParams[8] = {
    /* strategy,                 W, H1, H2, HL, A, S */
    { ZL_LzStrategy_fast,       19, 13,  0,  7, 1, 0 }, /* negative levels */
    { ZL_LzStrategy_fast,       19, 14,  0,  7, 1, 0 }, /* level 1 */
    { ZL_LzStrategy_fast,       19, 16,  0,  6, 1, 0 }, /* level 2 */
    { ZL_LzStrategy_doubleFast, 21, 17, 16,  5, 1, 0 }, /* level 3 */
    { ZL_LzStrategy_doubleFast, 21, 18, 18,  5, 1, 0 }, /* level 4 */
    { ZL_LzStrategy_greedy,     22, 18,  0,  5, 1, 2 }, /* level 5 */
    { ZL_LzStrategy_lazy,       22, 18,  0,  5, 1, 3 }, /* level 6 */
    { ZL_LzStrategy_lazy2,      22, 19,  0,  5, 1, 4 }, /* level 7 */
};
// clang-format on

/// @returns true if @p strategy searches multiple candidates in a ZS_RowTable.
static bool ZL_LzStrategy_usesRowTable(ZL_LzStrategy strategy)
{
    return strategy == ZL_LzStrategy_greedy || strategy == ZL_LzStrategy_lazy
            || strategy == ZL_LzStrategy_lazy2;
}

ZL_LzParameters ZL_LzParameters_default(int level, size_t srcSize)
{
    (void)srcSize; // Ignored for now

    const int row = level == 0 ? 1 : ZL_CLAMP(level, 0, 7);

    ZL_LzParameters params = kDefaultParams[row];
    if (level < 0) {
        params.acceleration = (uint32_t)-level;
    }
    return params;
}

void ZL_LzParameters_adjust(ZL_LzParameters* params, size_t srcSize)
{
#define ZL_LZPARAM_CLAMP(field, macro)          \
    params->field = ZL_CLAMP(                   \
            (uint32_t)params->field,            \
            (uint32_t)ZL_LZPARAM_##macro##_MIN, \
            (uint32_t)ZL_LZPARAM_##macro##_MAX)

    ZL_LZPARAM_CLAMP(strategy, STRATEGY);
    ZL_LZPARAM_CLAMP(windowLog, WINDOWLOG);
    ZL_LZPARAM_CLAMP(hashLog1, HASHLOG1);
    ZL_LZPARAM_CLAMP(hashLog2, HASHLOG2);
    ZL_LZPARAM_CLAMP(hashLength, HASHLENGTH);
    ZL_LZPARAM_CLAMP(acceleration, ACCELERATION);
    ZL_LZPARAM_CLAMP(searchLog, SEARCHLOG);

    // Adjust window log down if necessary
    {
        const uint32_t srcLog = (uint32_t)ZL_nextPow2(srcSize);
        params->windowLog     = ZL_MIN(params->windowLog, srcLog);
    }

    // Adjust table sizes down if necessary
    params->hashLog1 = ZL_MIN(params->windowLog + 1, params->hashLog1);
    params->hashLog2 = ZL_MIN(params->windowLog + 1, params->hashLog2);

    if (params->strategy == ZL_LzStrategy_fast
        || ZL_LzStrategy_usesRowTable(params->strategy)) {
        // Clear hashLog2 for the strategies that only use one table
        params->hashLog2 = 0;
    }

    if (ZL_LzStrategy_usesRowTable(params->strategy)) {
        // A row table cannot be smaller than a single row.
        params->hashLog1 = ZL_MAX(params->hashLog1, ZS_RowTable_kRowLog);
    }
}

size_t ZL_Lz_scratchBytes(const ZL_LzParameters* params)
{
    if (ZL_LzStrategy_usesRowTable(params->strategy)) {
        return ZS_RowTable_tableSize(params->hashLog1);
    }
    return ZS_FastTable_tableSize(params->hashLog1)
            + ZS_FastTable_tableSize(params->hashLog2);
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

ZL_FORCE_INLINE size_t storeSequence(
        uint8_t* lits,
        uint16_t* litLens,
        uint16_t* matchLens,
        void* offsets,
        size_t seq,
        const uint8_t* litStart,
        ptrdiff_t ll,
        ptrdiff_t ml,
        ptrdiff_t off,
        size_t offsetWidth)
{
    memcpy(lits, litStart, 16);
    if (ZL_UNLIKELY(ll > 16)) {
        assert(ZL_LZ_LIT_OVER_LENGTH >= ZS_WILDCOPY_OVERLENGTH);
        ZS_wildcopy(lits, litStart, (ptrdiff_t)ll, ZS_wo_no_overlap);
    }

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
        storeOffset(offsets, offsetWidth, seq, off);
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
            storeOffset(offsets, offsetWidth, seq, off);
            ++seq;

            remainingMatchLength -= bounded;
            litLens[seq] = 0;
        }
    }
    return seq;
}

/**
 * Match finding algorithm that runs the equivalent of the ZSTD_fast strategy.
 *
 * NOTE: This kernel uses ptrdiff_t rather than pointers to avoid UB with
 * pointers, which are only valid within the buffer and one past the end.
 */
ZL_FORCE_INLINE void ZL_Lz_encodeFastImpl(
        ZL_Lz_OutSequences* dst,
        const uint8_t* const src,
        size_t srcSize,
        void* hashTableMem,
        const ZL_LzParameters* params,
        const uint32_t kHashLen)
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

    const uint32_t windowLog    = params->windowLog;
    const uint32_t acceleration = params->acceleration;

    const uint32_t tableLog = params->hashLog1;
    ZS_FastTable table      = { 0, 0, 0 };
    ZS_FastTable_init(&table, hashTableMem, tableLog, kHashLen);

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
                &table, inPtr, (uint32_t)inPos, kHashLen);
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

            ptrdiff_t ll = (inPos - inLitStart);
            assert(inPos + ZL_LZ_LIT_OVER_LENGTH <= (ptrdiff_t)srcSize);
            seq = storeSequence(
                    lits,
                    litLens,
                    matchLens,
                    offsets,
                    seq,
                    in + inLitStart,
                    ll,
                    ml,
                    distance,
                    offsetWidth);
            lits += ll;

            // Update the hash table with positions at the start and end of the
            // match.
            // NOTE: Taken from zstd_fast.c
            ZS_FastTable_putT(
                    &table, in + inPos + 2, (uint32_t)(inPos + 2), kHashLen);
            inPos += ml;
            if (inPos <= inLimit) {
                ZS_FastTable_putT(
                        &table,
                        in + inPos - 2,
                        (uint32_t)(inPos - 2),
                        kHashLen);
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

#define ZL_LZ_ENCODE_DEFINITION(variant, kHashLen)                  \
    ZL_FORCE_NOINLINE void ZL_Lz_encode##variant##kHashLen(         \
            ZL_Lz_OutSequences* dst,                                \
            const uint8_t* const src,                               \
            size_t srcSize,                                         \
            void* hashTableMem,                                     \
            const ZL_LzParameters* params)                          \
    {                                                               \
        ZL_Lz_encode##variant##Impl(                                \
                dst, src, srcSize, hashTableMem, params, kHashLen); \
    }

ZL_LZ_ENCODE_DEFINITION(Fast, 4)
ZL_LZ_ENCODE_DEFINITION(Fast, 5)
ZL_LZ_ENCODE_DEFINITION(Fast, 6)
ZL_LZ_ENCODE_DEFINITION(Fast, 7)

static void ZL_Lz_encodeFast(
        ZL_Lz_OutSequences* dst,
        const uint8_t* const src,
        size_t srcSize,
        void* hashTableMem,
        const ZL_LzParameters* params)
{
    const uint32_t hashLen = params->hashLength;
    if (hashLen <= 4) {
        ZL_Lz_encodeFast4(dst, src, srcSize, hashTableMem, params);
    } else if (hashLen == 5) {
        ZL_Lz_encodeFast5(dst, src, srcSize, hashTableMem, params);
    } else if (hashLen == 6) {
        ZL_Lz_encodeFast6(dst, src, srcSize, hashTableMem, params);
    } else {
        ZL_Lz_encodeFast7(dst, src, srcSize, hashTableMem, params);
    }
}

/**
 * Match finding algorithm that runs the equivalent of the ZSTD_dfast
 * strategy.
 *
 * NOTE: This kernel uses ptrdiff_t rather than pointers to avoid UB
 * with pointers, which are only valid within the buffer and one past
 * the end.
 */
ZL_FORCE_INLINE void ZL_Lz_encodeDoubleFastImpl(
        ZL_Lz_OutSequences* dst,
        const uint8_t* const src,
        size_t srcSize,
        void* hashTableMem,
        const ZL_LzParameters* params,
        const uint32_t kHashLen)
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

    const uint32_t windowLog    = params->windowLog;
    const uint32_t acceleration = params->acceleration;

    const uint32_t tableLogL = params->hashLog1;
    ZS_FastTable tableL      = { 0, 0, 0 };
    ZS_FastTable_init(&tableL, hashTableMem, tableLogL, 8);

    const uint32_t tableLogS = params->hashLog2;
    ZS_FastTable tableS      = { 0, 0, 0 };
    ZS_FastTable_init(
            &tableS,
            (char*)hashTableMem + ZS_FastTable_tableSize(tableLogL),
            tableLogS,
            kHashLen);

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

    for (;;) {
        ptrdiff_t distance;
        ptrdiff_t match;
        ptrdiff_t matchLen;
        ptrdiff_t inPos1 = inPos + step;
        while (inPos1 <= inLimit) {
            const uint8_t* const inPtr = in + inPos;
            const ptrdiff_t matchL     = ZS_FastTable_getAndUpdateT(
                    &tableL, inPtr, (uint32_t)inPos, 8);
            const ptrdiff_t matchS = ZS_FastTable_getAndUpdateT(
                    &tableS, inPtr, (uint32_t)inPos, kHashLen);
            const ptrdiff_t distanceL = inPos - matchL;
            const ptrdiff_t distanceS = inPos - matchS;
            if (ZL_read64(in + matchL) == ZL_read64(inPtr)
                && distanceL < maxOffset) {
                match    = matchL;
                matchLen = 8 + matchLength(in, inPos + 8, matchL + 8, inEnd);
                distance = distanceL;
                break;
            } else if (
                    ZL_read32(in + matchS) == ZL_read32(inPtr)
                    && distanceS < maxOffset) {
                match    = matchS;
                matchLen = 4 + matchLength(in, inPos + 4, matchS + 4, inEnd);
                distance = distanceS;

                // Check for longer match at inPos1
                const uint8_t* const inPtr1 = in + inPos1;
                // It is only safe to store this position when step < 4, as we
                // are guaranteed to skip to at least inPos+4 with matchS.
                const ptrdiff_t matchL1 = ZS_FastTable_getAndConditionalUpdateT(
                        &tableL, inPtr1, (uint32_t)inPos1, 8, step < 4);
                const ptrdiff_t distanceL1 = inPos1 - matchL1;
                if (ZL_read64(in + matchL1) == ZL_read64(inPtr1)
                    && distanceL1 < maxOffset) {
                    const ptrdiff_t matchLenL1 =
                            8 + matchLength(in, inPos1 + 8, matchL1 + 8, inEnd);
                    if (matchLenL1 > matchLen) {
                        // Use the long match instead
                        inPos    = inPos1;
                        match    = matchL1;
                        matchLen = matchLenL1;
                        distance = distanceL1;
                    }
                }
                break;
            } else {
                inPos += step;
                inPos1 = inPos + step;

                // This logic helps skip over incompressible data quickly by
                // progresssively speeding up every kStepIncr bytes and
                // resetting when a match is found.
                if (inPos >= nextStep) {
                    ++step;
                    nextStep += kStepIncr;
                }
            }
        }

        // The search loop only exits without a match when inPos1 passes
        // inLimit, in which case match, matchLen, & distance are unset.
        if (inPos1 > inLimit) {
            break;
        }

        // Walk the match backwards
        while (match > 0 && inPos > inLitStart
               && in[match - 1] == in[inPos - 1]) {
            --match;
            --inPos;
            ++matchLen;
        }

        ptrdiff_t ll = (inPos - inLitStart);
        assert(inPos + ZL_LZ_LIT_OVER_LENGTH <= (ptrdiff_t)srcSize);
        seq = storeSequence(
                lits,
                litLens,
                matchLens,
                offsets,
                seq,
                in + inLitStart,
                ll,
                matchLen,
                distance,
                offsetWidth);
        lits += ll;

        // Update the hash table with positions at the start and end of
        // the match. NOTE: Taken from zstd_double_fast.c
        ZS_FastTable_putT(&tableL, in + inPos + 2, (uint32_t)(inPos + 2), 8);
        ZS_FastTable_putT(
                &tableS, in + inPos + 2, (uint32_t)(inPos + 2), kHashLen);
        inPos += matchLen;
        if (inPos <= inLimit) {
            ZS_FastTable_putT(
                    &tableL, in + inPos - 2, (uint32_t)(inPos - 2), 8);
            ZS_FastTable_putT(
                    &tableS, in + inPos - 2, (uint32_t)(inPos - 2), kHashLen);
        }
        inLitStart = inPos;
        step       = firstStep;
        nextStep   = inPos + kStepIncr;
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

ZL_LZ_ENCODE_DEFINITION(DoubleFast, 4)
ZL_LZ_ENCODE_DEFINITION(DoubleFast, 5)
ZL_LZ_ENCODE_DEFINITION(DoubleFast, 6)
ZL_LZ_ENCODE_DEFINITION(DoubleFast, 7)

static void ZL_Lz_encodeDoubleFast(
        ZL_Lz_OutSequences* dst,
        const uint8_t* const src,
        size_t srcSize,
        void* hashTableMem,
        const ZL_LzParameters* params)
{
    const uint32_t hashLen = params->hashLength;
    if (hashLen <= 4) {
        ZL_Lz_encodeDoubleFast4(dst, src, srcSize, hashTableMem, params);
    } else if (hashLen == 5) {
        ZL_Lz_encodeDoubleFast5(dst, src, srcSize, hashTableMem, params);
    } else if (hashLen == 6) {
        ZL_Lz_encodeDoubleFast6(dst, src, srcSize, hashTableMem, params);
    } else {
        ZL_Lz_encodeDoubleFast7(dst, src, srcSize, hashTableMem, params);
    }
}

/**
 * Searches the row table for the best match at @p pos, first filling in every
 * position the parser has skipped since the last search, which ZS_RowTable
 * requires.
 */
ZL_FORCE_INLINE ZS_RowTable_Match rowSearch(
        ZS_RowTable* table,
        const uint8_t* const in,
        ptrdiff_t anchor,
        ptrdiff_t pos,
        ptrdiff_t inEnd,
        ptrdiff_t maxOffset,
        size_t nbSearches,
        const uint32_t kHashLen)
{
    // Bounding the search by the window also rejects the never-written table
    // entries, which read back as position 0.
    const uint32_t lowLimit = (uint32_t)ZL_MAX(1, pos - maxOffset + 1);
    ZS_RowTable_fillT(table, in, (uint32_t)pos, 1, kHashLen);
    return ZS_RowTable_getBestMatchAndUpdateT(
            table,
            in,
            in + anchor,
            lowLimit,
            (uint32_t)pos,
            in + inEnd,
            nbSearches,
            ZL_LZ_MIN_MATCH,
            1,
            kHashLen);
}

/**
 * Match finding algorithm that runs the equivalent of the ZSTD_greedy
 * (@p kDepth 0), ZSTD_lazy (1) and ZSTD_lazy2 (2) strategies, on top of the
 * row hash table. Unlike zstd, there are no repcodes in the LZ format, so only
 * the match searches contribute candidates.
 *
 * NOTE: This kernel uses ptrdiff_t rather than pointers to avoid UB with
 * pointers, which are only valid within the buffer and one past the end.
 */
ZL_FORCE_INLINE void ZL_Lz_encodeLazyImpl(
        ZL_Lz_OutSequences* dst,
        const uint8_t* const src,
        size_t srcSize,
        void* hashTableMem,
        const ZL_LzParameters* params,
        const uint32_t kHashLen,
        const uint32_t kDepth)
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

    const uint32_t windowLog    = params->windowLog;
    const uint32_t acceleration = ZL_MAX(params->acceleration, 1);
    const size_t nbSearches     = (size_t)1 << params->searchLog;

    ZS_RowTable table = { NULL, 0, 0, 0, 0 };
    ZS_RowTable_init(&table, hashTableMem, params->hashLog1, 1, kHashLen);

    const ptrdiff_t kSrcOverLength =
            ZL_MAX(ZL_LZ_LIT_OVER_LENGTH, ZL_LZ_MATCH_OVER_LENGTH);

    const uint8_t* const in = src;
    const ptrdiff_t inEnd   = (ptrdiff_t)srcSize;
    const ptrdiff_t inLimit = (ptrdiff_t)srcSize - kSrcOverLength;

    // Position 0 is indistinguishable from an empty table entry, so never
    // insert it.
    ptrdiff_t inLitStart = 0;
    ptrdiff_t inPos      = 1;
    table.nextToFill     = 1;

    // Cache output pointers locally to avoid reloading through dst
    uint8_t* lits             = dst->literals;
    uint16_t* const litLens   = dst->literalLengths;
    uint16_t* const matchLens = dst->matchLengths;
    void* const offsets       = dst->offsets;
    const size_t offsetWidth  = dst->offsetWidth;
    const ptrdiff_t maxOffset = getMaxOffset(offsetWidth, windowLog);

    size_t seq = 0;

    while (inPos < inLimit) {
        const ZS_RowTable_Match found = rowSearch(
                &table,
                in,
                inLitStart,
                inPos,
                inEnd,
                maxOffset,
                nbSearches,
                kHashLen);
        if (found.totalLength < ZL_LZ_MIN_MATCH) {
            // This logic helps skip over incompressible data quickly by
            // progressively speeding up the further we get from the last match.
            inPos += ((inPos - inLitStart) >> ZL_LZ_SEARCH_STRENGTH)
                    + (ptrdiff_t)acceleration;
            continue;
        }

        // The row table has already extended the match backwards, no further
        // than inLitStart, so `start` is where the sequence's match begins.
        ptrdiff_t start    = inPos - (ptrdiff_t)found.backwardLength;
        ptrdiff_t ml       = (ptrdiff_t)found.totalLength;
        ptrdiff_t distance = inPos - (ptrdiff_t)found.matchIdx;

        // Try to find a better match one (and for lazy2, two) bytes later,
        // which costs an extra literal but may pay for itself.
        for (uint32_t depth = 0; depth < kDepth && inPos + 1 < inLimit;
             ++depth) {
            ++inPos;
            const ZS_RowTable_Match next = rowSearch(
                    &table,
                    in,
                    inLitStart,
                    inPos,
                    inEnd,
                    maxOffset,
                    nbSearches,
                    kHashLen);
            if (next.totalLength < ZL_LZ_MIN_MATCH) {
                break;
            }
            const ptrdiff_t nextDistance = inPos - (ptrdiff_t)next.matchIdx;
            // Gain heuristic taken from zstd_lazy.c: a match is worth its
            // length minus the cost of its offset, and the incumbent gets a
            // bonus for every literal the replacement would add.
            const int gainNext = (int)(next.totalLength * 4)
                    - ZL_highbit32((uint32_t)nextDistance);
            const int gainCur = (int)(ml * 4) - ZL_highbit32((uint32_t)distance)
                    + (int)(4 * (depth + 1) - 1);
            if (gainNext <= gainCur) {
                break;
            }
            start    = inPos - (ptrdiff_t)next.backwardLength;
            ml       = (ptrdiff_t)next.totalLength;
            distance = nextDistance;
        }

        const ptrdiff_t ll = start - inLitStart;
        assert(inLitStart + ZL_LZ_LIT_OVER_LENGTH <= (ptrdiff_t)srcSize);
        seq = storeSequence(
                lits,
                litLens,
                matchLens,
                offsets,
                seq,
                in + inLitStart,
                ll,
                ml,
                distance,
                offsetWidth);
        lits += ll;

        // The match always ends at least ZL_LZ_MIN_MATCH past the position it
        // was found at, so the parser never moves backwards.
        inLitStart = inPos = start + ml;
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

#define ZL_LZ_ENCODE_LAZY_DEFINITION(variant, kDepth, kHashLen)             \
    ZL_FORCE_NOINLINE void ZL_Lz_encode##variant##kHashLen(                 \
            ZL_Lz_OutSequences* dst,                                        \
            const uint8_t* const src,                                       \
            size_t srcSize,                                                 \
            void* hashTableMem,                                             \
            const ZL_LzParameters* params)                                  \
    {                                                                       \
        ZL_Lz_encodeLazyImpl(                                               \
                dst, src, srcSize, hashTableMem, params, kHashLen, kDepth); \
    }

#define ZL_LZ_ENCODE_LAZY_DISPATCH(variant, kDepth)                            \
    ZL_LZ_ENCODE_LAZY_DEFINITION(variant, kDepth, 4)                           \
    ZL_LZ_ENCODE_LAZY_DEFINITION(variant, kDepth, 5)                           \
    ZL_LZ_ENCODE_LAZY_DEFINITION(variant, kDepth, 6)                           \
    ZL_LZ_ENCODE_LAZY_DEFINITION(variant, kDepth, 7)                           \
                                                                               \
    static void ZL_Lz_encode##variant(                                         \
            ZL_Lz_OutSequences* dst,                                           \
            const uint8_t* const src,                                          \
            size_t srcSize,                                                    \
            void* hashTableMem,                                                \
            const ZL_LzParameters* params)                                     \
    {                                                                          \
        const uint32_t hashLen = params->hashLength;                           \
        if (hashLen <= 4) {                                                    \
            ZL_Lz_encode##variant##4(dst, src, srcSize, hashTableMem, params); \
        } else if (hashLen == 5) {                                             \
            ZL_Lz_encode##variant##5(dst, src, srcSize, hashTableMem, params); \
        } else if (hashLen == 6) {                                             \
            ZL_Lz_encode##variant##6(dst, src, srcSize, hashTableMem, params); \
        } else {                                                               \
            ZL_Lz_encode##variant##7(dst, src, srcSize, hashTableMem, params); \
        }                                                                      \
    }

ZL_LZ_ENCODE_LAZY_DISPATCH(Greedy, 0)
ZL_LZ_ENCODE_LAZY_DISPATCH(Lazy, 1)
ZL_LZ_ENCODE_LAZY_DISPATCH(Lazy2, 2)

void ZL_Lz_encode(
        ZL_Lz_OutSequences* dst,
        const uint8_t* const src,
        size_t srcSize,
        void* scratch,
        const ZL_LzParameters* params)
{
    switch (params->strategy) {
        case ZL_LzStrategy_greedy:
            ZL_Lz_encodeGreedy(dst, src, srcSize, scratch, params);
            break;
        case ZL_LzStrategy_lazy:
            ZL_Lz_encodeLazy(dst, src, srcSize, scratch, params);
            break;
        case ZL_LzStrategy_lazy2:
            ZL_Lz_encodeLazy2(dst, src, srcSize, scratch, params);
            break;
        case ZL_LzStrategy_doubleFast:
            ZL_Lz_encodeDoubleFast(dst, src, srcSize, scratch, params);
            break;
        case ZL_LzStrategy_fast:
        default:
            assert(params->strategy == ZL_LzStrategy_fast);
            ZL_Lz_encodeFast(dst, src, srcSize, scratch, params);
            break;
    }
}
