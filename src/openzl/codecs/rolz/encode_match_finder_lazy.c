// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "openzl/codecs/common/count.h"
#include "openzl/codecs/common/window.h"
#include "openzl/codecs/rolz/common_rolz.h"
#include "openzl/codecs/rolz/encode_match_finder.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/hash.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/simd_wrapper.h"
#include "openzl/shared/utils.h"

#define kSearchStrength 6
#define kLzBackSearch 1

#define kCombined 0
#define kPrefetchLog 3
#define kPrefetchAdv (1u << kPrefetchLog)
#define kPrefetchMask (kPrefetchAdv - 1)

typedef struct {
    uint32_t rowSizeU32;
    uint32_t rowMask;
    uint32_t rowEntries;
    uint32_t predictedMatchLengthOffset;
    uint32_t hashOffset;
    uint32_t contextLog;
    uint32_t rowLog;
    uint32_t minLength;
    uint32_t nbSearches;
    bool predictMatchLength;
    uint32_t* table;
} ZS_rolz;

static void ZS_rolz_destroy(ZS_rolz* rolz)
{
    free(rolz->table);
}

#define kPredictedMatchLengthOffset (4 + 16 * 4)
#define kRolzHashOffset (1 + 16 + 4)
#define kRolzRowSizeU32 25
#define kRolzRowEntries 16
#define kRolzRowMask 15

static int ZS_rolz_init(
        ZS_rolz* rolz,
        uint32_t contextLog,
        uint32_t rowLog,
        uint32_t minLength,
        uint32_t searchLog,
        bool predictMatchLength)
{
    ZL_ASSERT_GE(rowLog, 2);
    ZL_ASSERT_LE(rowLog, 5);
    rolz->nbSearches = 1u << searchLog;
    rolz->contextLog = contextLog;
    rolz->rowLog     = rowLog;
    rolz->rowMask    = (1u << rowLog) - 1;
    rolz->rowEntries = 1u << rowLog;
    rolz->predictedMatchLengthOffset =
            (uint32_t)sizeof(uint32_t) + (uint32_t)(sizeof(uint32_t) << rowLog);
    rolz->hashOffset         = 1 + (1u << rowLog) + (1u << (rowLog - 2));
    rolz->minLength          = minLength;
    rolz->predictMatchLength = predictMatchLength;
    size_t const rowSize     = ((sizeof(uint32_t) + sizeof(uint8_t)) << rowLog)
            + (sizeof(ZL_Vec128) << (ZL_MAX(4, rowLog) - 4)) + sizeof(uint32_t);
    rolz->rowSizeU32       = (uint32_t)rowSize >> 2;
    size_t const tableSize = rowSize << contextLog;
    rolz->table            = (uint32_t*)calloc(1, tableSize);
    ZL_ASSERT_EQ(
            rolz->predictedMatchLengthOffset + (1u << rowLog),
            4 * rolz->hashOffset);
    ZL_ASSERT_EQ(rolz->predictedMatchLengthOffset, 4 * (1 + (1u << rowLog)));

    ZL_ASSERT_EQ(rolz->predictedMatchLengthOffset, kPredictedMatchLengthOffset);
    ZL_ASSERT_EQ(rolz->hashOffset, kRolzHashOffset);
    ZL_ASSERT_EQ(rolz->rowSizeU32, kRolzRowSizeU32);
    ZL_ASSERT_EQ(rolz->rowEntries, kRolzRowEntries);
    ZL_ASSERT_EQ(rolz->rowMask, kRolzRowMask);

    if (!rolz->table)
        return 1;

    return 0;
}

static void ZS_rolz_reset(ZS_rolz* rolz)
{
    size_t const rowSize   = sizeof(uint32_t) * rolz->rowSizeU32;
    size_t const tableSize = rowSize << rolz->contextLog;
    memset(rolz->table, 0, tableSize);
}

// 4 bytes - HEAD pointer
// 4 bytes * 2^rowLog - indices
// 1 byte * 2^rowLog >= 4 - predictedMatchLengths
// 16 bytes * 2^(max(rowLog,4)-4) - hashes

typedef struct {
    uint32_t matchIndex;
    uint32_t matchCode;
    uint32_t matchLength;
    uint32_t encodedMatchLength;
} ZS_RolzMatch;

ZL_FORCE_INLINE uint32_t ZS_rolz_nextIndex(uint32_t* head, uint32_t rowMask)
{
    uint32_t const next = (*head - 1) & rowMask;
    *head               = next;
    return next;
}

ZL_FORCE_INLINE void ZS_rolz_prefetch(
        ZS_rolz* rolz,
        void const* ptr,
        uint32_t contextDepth,
        uint32_t contextLog)
{
    uint32_t const context = ZS_rolz_getContext(ptr, contextDepth, contextLog);
    uint32_t const* const rowStart = rolz->table + context * kRolzRowSizeU32;
    ZL_PREFETCH_L1(rowStart);
    ZL_PREFETCH_L1(rowStart + 16);
}

ZL_FORCE_INLINE void ZS_rolz_insert2(
        ZS_rolz* rolz,
        uint32_t context,
        void const* ptr,
        uint32_t index,
        uint32_t matchLength,
        uint32_t rolzMinLength)
{
    uint8_t const hash       = (uint8_t)ZL_hashPtr(ptr, 8, rolzMinLength);
    uint32_t* const rowStart = rolz->table + context * kRolzRowSizeU32;
    uint32_t const pos       = ZS_rolz_nextIndex(rowStart, kRolzRowMask);
    ZL_ASSERT_LT(pos, rolz->rowEntries);
    rowStart[1 + pos] = index;
    ((uint8_t*)rowStart)[kPredictedMatchLengthOffset + pos] =
            (uint8_t)ZL_MIN(matchLength, 255);
    ((uint8_t*)(rowStart + kRolzHashOffset))[pos] = hash;
}

// ZS_rolz_insert appears to be unused legacy code, kept for potential future
// use
static ZL_UNUSED_ATTR void ZS_rolz_insert(
        ZS_rolz* rolz,
        uint32_t context,
        void const* ptr,
        uint32_t index,
        uint32_t matchLength)
{
    ZS_rolz_insert2(rolz, context, ptr, index, matchLength, rolz->minLength);
}

typedef struct {
    uint32_t matchIndex;
    uint8_t matchCode;
    uint8_t matchPos;
} RolzMatch;

ZL_FORCE_INLINE ZS_RolzMatch ZS_rolz_findBestMatch2(
        ZS_rolz* rolz,
        ZS_window const* window,
        uint32_t context,
        uint8_t const* ip,
        uint8_t const* iend,
        uint32_t rolzMinLength,
        bool predictMatchLength)
{
    uint8_t const hash        = (uint8_t)ZL_hashPtr(ip, 8, rolzMinLength);
    uint32_t* const rowStart  = rolz->table + context * kRolzRowSizeU32;
    uint8_t const* const base = window->base;
    RolzMatch matchBuffer[16];
    size_t nbMatches  = 0;
    size_t nbSearches = rolz->nbSearches;
    {
        ZL_VecMask matches;
        if ((1)) {
            if (rolz->rowLog <= 4) {
                ZL_Vec128 const hashes =
                        ZL_Vec128_read(rowStart + kRolzHashOffset);
                ZL_Vec128 const hash1 = ZL_Vec128_set8(hash);
                ZL_Vec128 const cmpeq = ZL_Vec128_cmp8(hashes, hash1);
                matches               = ZL_Vec128_mask8(cmpeq);
            } else {
                ZL_Vec256 const hashes =
                        ZL_Vec256_read(rowStart + kRolzHashOffset);
                ZL_Vec256 const hash1 = ZL_Vec256_set8(hash);
                ZL_Vec256 const cmpeq = ZL_Vec256_cmp8(hashes, hash1);
                matches               = ZL_Vec256_mask8(cmpeq);
            }
        } else {
            matches = (1u << rolz->rowEntries) - 1;
        }
        ZL_ASSERT_LT(matches, 1u << rolz->rowEntries);
        matches = ZL_VecMask_rotateRight(matches, rowStart[0], kRolzRowEntries);
        ZL_ASSERT_LT(matches, 1u << rolz->rowEntries);

        for (; matches != 0 && nbSearches != 0;
             matches &= matches - 1, --nbSearches) {
            uint32_t const matchCode = (uint32_t)ZL_VecMask_next(matches);
            uint32_t const matchPos  = (rowStart[0] + matchCode) & kRolzRowMask;
            ZL_ASSERT_LT(matchPos, rolz->rowEntries);
            ZL_ASSERT_LT(matchCode, rolz->rowEntries);
            uint32_t const matchIndex = rowStart[1 + matchPos];
            if (matchIndex < window->lowLimit)
                break;
            ZL_PREFETCH_L1(base + matchIndex);
            matchBuffer[nbMatches].matchIndex = matchIndex;
            matchBuffer[nbMatches].matchCode  = (uint8_t)matchCode;
            matchBuffer[nbMatches].matchPos   = (uint8_t)matchPos;
            ++nbMatches;
        }
    }

    ZS_RolzMatch best = { 0, 0, 0, 0 };
    best.matchLength  = 1;
    for (size_t i = 0; i < nbMatches; ++i) {
        RolzMatch const m          = matchBuffer[i];
        uint8_t const* const match = base + m.matchIndex;
        uint32_t matchLength       = 0;
        if (ip[best.matchLength - 1] == match[best.matchLength - 1])
            matchLength = (uint32_t)ZS_count(ip, match, iend);
        if (matchLength >= best.matchLength && matchLength >= rolzMinLength) {
            uint8_t const predictedMatchLength = ((uint8_t*)rowStart)
                    [kPredictedMatchLengthOffset + m.matchPos];
            uint32_t const encodedMatchLength = predictMatchLength
                    ? ZS_rolz_encodeMatchLength(
                              rolzMinLength, predictedMatchLength, matchLength)
                    : matchLength - rolzMinLength;
            ZL_ASSERT(best.matchLength == 1 || m.matchCode > best.matchCode);
            bool const better = matchLength > best.matchLength
                    || encodedMatchLength < best.encodedMatchLength;
            if (better) {
                best.matchIndex         = m.matchIndex;
                best.matchCode          = m.matchCode;
                best.matchLength        = matchLength;
                best.encodedMatchLength = encodedMatchLength;
            }
        }
    }
    if (best.matchLength < rolzMinLength)
        best.matchLength = 0;
    return best;
}

// ZS_rolz_findBestMatch appears to be unused legacy code, kept for potential
// future use
static ZL_UNUSED_ATTR ZS_RolzMatch ZS_rolz_findBestMatch(
        ZS_rolz* rolz,
        ZS_window const* window,
        uint32_t context,
        uint8_t const* ip,
        uint8_t const* iend)
{
    return ZS_rolz_findBestMatch2(
            rolz,
            window,
            context,
            ip,
            iend,
            rolz->minLength,
            rolz->predictMatchLength);
}

ZL_FORCE_INLINE uint32_t ZS_rolz_prevIndex(uint32_t* head, uint32_t rowMask)
{
    uint32_t const prev = *head;
    *head               = (*head + 1) & rowMask;
    return prev;
}

ZL_FORCE_INLINE void ZS_rolz_rollback(
        ZS_rolz* rolz,
        ZS_window const* window,
        uint8_t const* ip,
        uint32_t back,
        uint32_t contextDepth,
        uint32_t contextLog)
{
    (void)window;
    for (uint32_t b = 1; b <= back; ++b) {
        uint32_t const context =
                ZS_rolz_getContext(ip - b, contextDepth, contextLog);
        uint32_t* const rowStart = rolz->table + context * rolz->rowSizeU32;
        uint32_t const pos       = ZS_rolz_prevIndex(rowStart, rolz->rowMask);
        ZL_ASSERT_LT(pos, rolz->rowEntries);
        ZL_ASSERT_EQ(window->base + rowStart[1 + pos], ip - b);
        rowStart[1 + pos] = 0;
    }
}

typedef struct {
    uint32_t minLength;
    uint32_t searchLog;
    uint32_t tableLog;
    uint32_t chainLog;
    uint32_t anchor;
    uint32_t* table;
    uint32_t* chain;
} ZS_lz;

static void ZS_lz_destroy(ZS_lz* lz)
{
    free(lz->table);
    free(lz->chain);
}

// ZS_lz_init appears to be unused legacy code, kept for potential future use
static ZL_UNUSED_ATTR int ZS_lz_init(
        ZS_lz* lz,
        ZS_window const* window,
        uint32_t tableLog,
        uint32_t minLength,
        uint32_t chainLog,
        uint32_t searchLog)
{
    lz->searchLog          = searchLog;
    lz->anchor             = window->dictLimit + 1;
    lz->tableLog           = tableLog;
    lz->chainLog           = chainLog;
    lz->minLength          = minLength;
    size_t const tableSize = (1u << tableLog) * sizeof(*lz->table);
    size_t const chainSize = (1u << chainLog) * sizeof(*lz->chain);
    lz->table              = (uint32_t*)malloc(tableSize);
    lz->chain              = (uint32_t*)malloc(chainSize);
    if (!lz->table || !lz->chain) {
        ZS_lz_destroy(lz);
        return 1;
    }
    memset(lz->table, 0, tableSize);
    memset(lz->chain, 0, chainSize);
    return 0;
}

// ZS_lz_reset appears to be unused legacy code, kept for potential future use
static ZL_UNUSED_ATTR void ZS_lz_reset(ZS_lz* lz, ZS_window const* window)
{
    lz->anchor = window->dictLimit;
}

/* *********************************
 *  Hash Chain
 ***********************************/
#define NEXT_IN_CHAIN(d, mask) chain[(d) & (mask)]

/* Update chains up to ip (excluded)
   Assumption : always within prefix (i.e. not within extDict) */
ZL_FORCE_INLINE uint32_t ZS_insertAndFindFirstIndex_internal(
        ZS_lz* lz,
        ZS_window const* window,
        const uint8_t* ip,
        uint32_t const mls)
{
    uint32_t* const table     = lz->table;
    const uint32_t tableLog   = lz->tableLog;
    uint32_t* const chain     = lz->chain;
    const uint32_t chainMask  = (1u << lz->chainLog) - 1;
    uint8_t const* const base = window->base;
    const uint32_t target     = (uint32_t)(ip - base);
    uint32_t idx              = lz->anchor;
    ZL_ASSERT_GE(idx, window->dictLimit);

    while (idx < target) { /* catch up */
        size_t const h                = ZL_hashPtr(base + idx, tableLog, mls);
        NEXT_IN_CHAIN(idx, chainMask) = table[h];
        table[h]                      = idx;
        idx++;
    }

    lz->anchor = target;
    return table[ZL_hashPtr(ip, tableLog, mls)];
}

typedef struct {
    uint32_t matchIndex;
    uint32_t matchLength;
} ZS_LzMatch;

/* inlining is important to hardwire a hot branch (template emulation) */
ZL_FORCE_INLINE ZS_LzMatch ZS_lz_findBestMatch2(
        ZS_lz* lz,
        ZS_window const* window,
        const uint8_t* const ip,
        const uint8_t* const iend,
        const unsigned lzMinLength)
{
    uint32_t const mls        = lzMinLength;
    uint32_t* const chain     = lz->chain;
    const uint32_t chainSize  = 1u << lz->chainLog;
    const uint32_t chainMask  = chainSize - 1;
    uint8_t const* const base = window->base;

    /* HC4 match finder */
    uint32_t matchIndex =
            ZS_insertAndFindFirstIndex_internal(lz, window, ip, mls);

    const uint32_t dictLimit = window->dictLimit;
    const uint32_t current   = (uint32_t)(ip - base);
    const uint32_t lowLimit  = ZS_window_getLowestMatchIndex(window, current);
    const uint32_t minChain  = current > chainSize ? current - chainSize : 0;
    uint32_t nbAttempts      = 1U << lz->searchLog;

    ZS_LzMatch best = { 0, 0 };

    for (; (matchIndex > lowLimit) & (nbAttempts > 0); nbAttempts--) {
        uint32_t currentMl = 0;
        ZL_ASSERT_GE(matchIndex, dictLimit);
        const uint8_t* const match = base + matchIndex;
        if (match[best.matchLength]
            == ip[best.matchLength]) /* potentially better */
            currentMl = (uint32_t)ZS_count(ip, match, iend);

        /* save best solution */
        if (currentMl > best.matchLength) {
            best.matchIndex  = matchIndex;
            best.matchLength = currentMl;
            if (ip + currentMl == iend)
                break; /* best possible, avoids read overflow on next attempt */
        }

        if (matchIndex <= minChain)
            break;
        matchIndex = NEXT_IN_CHAIN(matchIndex, chainMask);
    }

    return best;
}

// ZS_lz_findBestMatch appears to be unused legacy code, kept for potential
// future use
static ZL_UNUSED_ATTR ZS_LzMatch ZS_lz_findBestMatch(
        ZS_lz* lz,
        ZS_window const* window,
        const uint8_t* const ip,
        const uint8_t* const iend)
{
    return ZS_lz_findBestMatch2(lz, window, ip, iend, lz->minLength);
}

typedef struct {
    uint32_t row;
    uint32_t tag;
} ZS_HashPair;

typedef struct {
    uint32_t* table;
    uint32_t anchor;
    uint32_t tableLog;
    uint32_t rowLog;
    uint32_t nbSearches;
    uint32_t minLength;
    ZS_HashPair hashCache[kPrefetchAdv];
} ZS_lz2;

/* table layout for row size == N (N == 16 or 32):
 * U32 head
 * U8 hash[N]: bits 0-6 are hash - bit 8 = isRolz?
 * U32 lzIndex[N]
 * U32 rolzIndex[N]
 * U8 predictedMatchLength[N]
 *
 * Size of row == 4 + 10*N
 */

static void ZS_lz2_destroy(ZS_lz2* lz2)
{
    free(lz2->table);
    memset(lz2, 0, sizeof(*lz2));
}

static int ZS_lz2_init(
        ZS_lz2* lz2,
        ZS_window const* window,
        uint32_t tableLog,
        uint32_t rowLog,
        uint32_t searchLog,
        uint32_t minLength)
{
    lz2->anchor     = window->dictLimit + 1;
    lz2->nbSearches = 1u << searchLog;
    lz2->tableLog   = tableLog;
    lz2->rowLog     = rowLog;
    lz2->minLength  = minLength;

    size_t const rowSize   = 4u + ((1u + 4u) << rowLog);
    size_t const tableSize = rowSize << tableLog;
    lz2->table             = (uint32_t*)calloc(1, tableSize);

    if (!lz2->table) {
        ZS_lz2_destroy(lz2);
        return 1;
    }

    return 0;
}

static void ZS_lz2_reset(ZS_lz2* lz2)
{
    size_t const rowSize   = 4u + ((1u + 4u) << lz2->rowLog);
    size_t const tableSize = rowSize << lz2->tableLog;
    memset(lz2->table, 0, tableSize);
}

// 4 bytes - HEAD pointer
// 4 bytes * 2^rowLog - indices
// 1 byte * 2^rowLog >= 4 - predictedMatchLengths
// 16 bytes * 2^(max(rowLog,4)-4) - hashes

typedef struct {
    uint32_t matchCode;
    uint32_t matchLength;
} ZS_Lz2Match;

ZL_FORCE_INLINE uint32_t ZS_lz2_nextIndex(uint32_t* head, uint32_t rowMask)
{
    uint32_t const next = (*head - 1) & rowMask;
    *head               = next;
    return next;
}

ZL_FORCE_INLINE ZS_HashPair ZS_lz2_hash(
        uint8_t const* ip,
        uint32_t tableLog,
        uint32_t kRowLog,
        uint32_t kMinLength)
{
    uint32_t const kRowSize = 1u + ((1u + 4u) << (kRowLog - 2));
    size_t const hash       = ZL_hashPtr(ip, tableLog + 8, kMinLength);
    ZS_HashPair hp;
    hp.row = (uint32_t)(hash >> 8) * kRowSize;
    hp.tag = (uint32_t)(hash & 0xFF);
    return hp;
}

ZL_FORCE_INLINE void
ZS_lz2_prefetchRow(uint32_t const* table, uint32_t row, uint32_t rowLog)
{
    ZL_PREFETCH_L1(table + row);
    ZL_PREFETCH_L1(table + row + 16);
    if (rowLog == 5) {
        ZL_PREFETCH_L1(table + row + 32);
    }
}

ZL_FORCE_INLINE void ZS_lz2_fillHashCache(
        ZS_lz2* lz2,
        const uint8_t* istart,
        uint32_t const kMinMatch)
{
    uint32_t const* const table = lz2->table;
    uint32_t const tableLog     = lz2->tableLog;
    uint32_t const rowLog       = lz2->rowLog;
    size_t idx;
    for (idx = 0; idx < kPrefetchAdv; ++idx) {
        ZS_HashPair const hash =
                ZS_lz2_hash(istart + idx, tableLog, rowLog, kMinMatch);
        ZS_lz2_prefetchRow(table, hash.row, rowLog);
        lz2->hashCache[idx] = hash;
    }
}

ZL_FORCE_INLINE ZS_HashPair ZS_lz2_nextCachedHash(
        ZS_HashPair* cache,
        uint32_t const* table,
        uint8_t const* base,
        uint32_t idx,
        uint32_t tableLog,
        uint32_t kRowLog,
        uint32_t const kMinMatch)
{
    ZS_HashPair const newHash = ZS_lz2_hash(
            base + idx + kPrefetchAdv, tableLog, kRowLog, kMinMatch);
    ZS_HashPair const hash = cache[idx & kPrefetchMask];
    ZS_lz2_prefetchRow(table, newHash.row, kRowLog);
    cache[idx & kPrefetchMask] = newHash;
    return hash;
}

ZL_FORCE_INLINE void ZS_lz2_insert(
        uint32_t* table,
        ZS_HashPair hash,
        uint32_t index,
        uint32_t kRowLog)
{
    uint32_t* const row         = table + hash.row;
    uint32_t const kRowMask     = (1u << kRowLog) - 1;
    uint32_t const kHeadOffset  = 0;
    uint32_t const kHashOffset  = kHeadOffset + 1;
    uint32_t const kEntryOffset = kHashOffset + (1u << (kRowLog - 2));
    uint32_t const pos          = ZS_lz2_nextIndex(row + kHeadOffset, kRowMask);
    ZL_ASSERT_LE(pos, kRowMask);
    ((uint8_t*)(row + kHashOffset))[pos] = (uint8_t)hash.tag;
    row[kEntryOffset + pos]              = index;
}

ZL_FORCE_INLINE void ZS_lz2_update(
        ZS_lz2* lz2,
        ZS_window const* window,
        const uint8_t* ip,
        uint32_t kRowLog,
        uint32_t kMinMatch)
{
    uint32_t* const table     = lz2->table;
    uint32_t const tableLog   = lz2->tableLog;
    const uint8_t* const base = window->base;
    const uint32_t target     = (uint32_t)(ip - base);
    uint32_t idx              = lz2->anchor;

    for (; idx < target; ++idx) {
        ZS_HashPair const hash = ZS_lz2_nextCachedHash(
                lz2->hashCache, table, base, idx, tableLog, kRowLog, kMinMatch);
        ZS_lz2_insert(table, hash, idx, kRowLog);
    }

    lz2->anchor = target;
}

ZL_FORCE_INLINE
ZS_Lz2Match ZS_lz2_findBestMatch2(
        ZS_lz2* lz2,
        ZS_window const* window,
        uint8_t const* ip,
        uint8_t const* iend,
        uint32_t kRowLog,
        uint32_t kMinMatch)
{
    uint32_t const kRowEntries  = (1u << kRowLog);
    uint32_t const kRowMask     = kRowEntries - 1;
    uint32_t const kHeadOffset  = 0;
    uint32_t const kHashOffset  = kHeadOffset + 1;
    uint32_t const kEntryOffset = kHashOffset + (1u << (kRowLog - 2));

    uint32_t* const table     = lz2->table;
    const uint8_t* const base = window->base;
    const uint32_t lowLimit   = window->lowLimit;
    const uint32_t curr       = (uint32_t)(ip - base);
    uint32_t nbSearches       = lz2->nbSearches;

    // ZS_HashPair hash = ZS_row_hash(ip, hashLog, mls);
    // ZL_PREFETCH_L1(row);

    /* HC4 match finder */
    ZS_lz2_update(lz2, window, ip, kRowLog, kMinMatch);

    uint32_t matchBuffer[32];
    size_t numMatches = 0;
    {
        ZS_HashPair const hash = lz2->hashCache[curr & kPrefetchMask];
        uint32_t* const row    = table + hash.row;
        uint32_t const head    = row[kHeadOffset];
        ZL_VecMask matches;
        if (kRowLog == 4) {
            ZL_Vec128 hashes = ZL_Vec128_read(row + kHashOffset);
            ZL_Vec128 hash1  = ZL_Vec128_set8((uint8_t)hash.tag);
            ZL_Vec128 cmpeq  = ZL_Vec128_cmp8(hashes, hash1);
            matches          = ZL_Vec128_mask8(cmpeq);
        } else {
            ZL_ASSERT_EQ(kRowLog, 5);
            ZL_Vec256 hashes = ZL_Vec256_read(row + kHashOffset);
            ZL_Vec256 hash1  = ZL_Vec256_set8((uint8_t)hash.tag);
            ZL_Vec256 cmpeq  = ZL_Vec256_cmp8(hashes, hash1);
            matches          = ZL_Vec256_mask8(cmpeq);
        }
        ZL_ASSERT_LT(head, kRowEntries);
        ZL_ASSERT_LT((uint64_t)matches, (1ull << kRowEntries));
        matches = ZL_VecMask_rotateRight(matches, head, kRowEntries);
        ZL_ASSERT_LT((uint64_t)matches, (1ull << kRowEntries));
        for (; (matches > 0) && (nbSearches > 0);
             --nbSearches, matches &= (matches - 1)) {
            uint32_t const matchPos =
                    (head + ZL_VecMask_next(matches)) & kRowMask;
            uint32_t const matchIndex = row[kEntryOffset + matchPos];
            if (matchIndex < lowLimit)
                break;
            ZL_PREFETCH_L1(base + matchIndex);
            matchBuffer[numMatches++] = matchIndex;
        }
    }

    ZS_Lz2Match best = { 0, 0 };

    for (size_t m = 0; m < numMatches; ++m) {
        uint32_t const matchIndex = matchBuffer[m];
        ZL_ASSERT_LT(matchIndex, curr);
        ZL_ASSERT_GE(matchIndex, lowLimit);
        uint32_t matchLength       = 0;
        const uint8_t* const match = base + matchIndex;
        if (match[best.matchLength] == ip[best.matchLength])
            matchLength = (uint32_t)ZS_count(ip, match, iend);
        if (matchLength > best.matchLength) {
            best.matchCode   = curr - matchIndex;
            best.matchLength = matchLength;
            if (ip + matchLength == iend)
                break; // best possible, avoids read overflow on next attempt
        }
    }

    return best;
}

static ZS_Lz2Match ZS_lz2_findBestMatch(
        ZS_lz2* lz2,
        ZS_window const* window,
        uint8_t const* ip,
        uint8_t const* iend)
{
    if (0) {
        return ZS_lz2_findBestMatch2(
                lz2, window, ip, iend, lz2->rowLog, lz2->minLength);
    }

    if (lz2->rowLog == 4) {
        switch (lz2->minLength) {
            case 5:
                return ZS_lz2_findBestMatch2(lz2, window, ip, iend, 4, 5);
            case 6:
                return ZS_lz2_findBestMatch2(lz2, window, ip, iend, 4, 6);
            case 7:
            default:
                return ZS_lz2_findBestMatch2(lz2, window, ip, iend, 4, 7);
        };
    } else {
        ZL_ASSERT_EQ(lz2->rowLog, 5);
        switch (lz2->minLength) {
            case 5:
                return ZS_lz2_findBestMatch2(lz2, window, ip, iend, 5, 5);
            case 6:
                return ZS_lz2_findBestMatch2(lz2, window, ip, iend, 5, 6);
            case 7:
            default:
                return ZS_lz2_findBestMatch2(lz2, window, ip, iend, 5, 7);
        };
    }
}

typedef struct {
    uint32_t* table;
    uint32_t* rolzIndex;
    uint32_t tableLog;
    uint32_t rowLog;
    uint32_t nbSearches;
    uint32_t minLength;
    uint32_t lzMinLength;
    uint32_t rolzMinLength;
    uint32_t rolzContextLog;
    uint32_t rolzContextDepth;
    uint32_t rolzContextSize;
    bool rolzPredictMatchLength;
} ZS_combined;

/* table layout for row size == N (N == 16 or 32):
 * U32 head
 * U8 hash[N]: bits 0-6 are hash - bit 8 = isRolz?
 * U32 lzIndex[N]
 * U32 rolzIndex[N]
 * U8 predictedMatchLength[N]
 *
 * Size of row == 4 + 10*N
 */

static void ZS_combined_destroy(ZS_combined* comb)
{
    free(comb->table);
    free(comb->rolzIndex);
    memset(comb, 0, sizeof(*comb));
}

// ZS_combined_init is only used in kCombined=1 builds, currently disabled
static ZL_UNUSED_ATTR int ZS_combined_init(
        ZS_combined* comb,
        uint32_t rolzContextLog,
        uint32_t rolzContextDepth,
        uint32_t rolzContextSize,
        uint32_t tableLog,
        uint32_t rowLog,
        uint32_t searchLog,
        uint32_t minLength,
        uint32_t lzMinLength,
        uint32_t rolzMinLength,
        bool rolzPredictMatchLength)
{
    ZL_ASSERT_GE(rowLog, 4);
    ZL_ASSERT_LE(rowLog, 5);
    comb->nbSearches             = 1u << searchLog;
    comb->tableLog               = tableLog;
    comb->rowLog                 = rowLog;
    comb->minLength              = minLength;
    comb->rolzMinLength          = rolzMinLength + rolzContextDepth;
    comb->lzMinLength            = lzMinLength;
    comb->rolzContextLog         = rolzContextLog;
    comb->rolzContextDepth       = rolzContextDepth;
    comb->rolzContextSize        = rolzContextSize;
    comb->rolzPredictMatchLength = rolzPredictMatchLength;

    size_t const rowSize   = 4u + ((1u + 4u + 4u + 1u) << rowLog);
    size_t const tableSize = rowSize << tableLog;
    comb->table            = (uint32_t*)calloc(1, tableSize);
    comb->rolzIndex        = (uint32_t*)calloc(4, 1u << rolzContextLog);

    if (!comb->table || !comb->rolzIndex) {
        ZS_combined_destroy(comb);
        return 1;
    }

    return 0;
}

// ZS_combined_reset is only used in kCombined=1 builds, currently disabled
static ZL_UNUSED_ATTR void ZS_combined_reset(ZS_combined* comb)
{
    size_t const rowSize   = 4u + ((1u + 4u + 4u + 1u) << comb->rowLog);
    size_t const tableSize = rowSize << comb->tableLog;
    memset(comb->table, 0, tableSize);
    memset(comb->rolzIndex, 0, sizeof(uint32_t) << comb->rolzContextLog);
}

// 4 bytes - HEAD pointer
// 4 bytes * 2^rowLog - indices
// 1 byte * 2^rowLog >= 4 - predictedMatchLengths
// 16 bytes * 2^(max(rowLog,4)-4) - hashes

typedef struct {
    uint32_t matchType;
    uint32_t matchIndex;
    uint32_t matchCode;
    uint32_t matchLength;
    uint32_t encodedMatchLength;
} ZS_CombinedMatch;

ZL_FORCE_INLINE uint32_t ZS_combined_nextIndex(uint32_t* head, uint32_t rowMask)
{
    uint32_t const next = (*head - 1) & rowMask;
    *head               = next;
    return next;
}

ZL_FORCE_INLINE uint32_t ZS_combined_nextRolzIndex(uint32_t* rolzIndex)
{
    uint32_t const next = *rolzIndex + 1;
    *rolzIndex          = next;
    return next;
}

// typedef struct {
//   uint32_t row;
//   uint32_t tag;
// } ZS_HashPair;

ZL_FORCE_INLINE ZS_HashPair ZS_combined_hash(
        uint8_t const* ip,
        uint32_t tableLog,
        uint32_t minLength,
        bool isRolz)
{
    size_t const hash = ZL_hashPtr(ip, tableLog + 7, minLength);
    ZS_HashPair hp;
    hp.row = (uint32_t)(hash >> 7);
    hp.tag = (uint32_t)((hash & 0x7F) | (isRolz ? 0x80 : 0x00));
    return hp;
}

ZL_FORCE_INLINE void ZS_combined_insert2(
        ZS_combined* comb,
        uint32_t context,
        void const* ptr,
        uint32_t index,
        uint32_t matchLength,
        bool isRolz,
        uint32_t const rolzContextDepth,
        uint32_t const minLength,
        uint32_t const tableLog,
        uint32_t const rowLog)
{
    ZL_DLOG(SEQ,
            "ROLZ PUT %u | rolz=%u | ctx=%u | ml=%u",
            index - 1,
            (unsigned)isRolz,
            context,
            ZL_MIN(matchLength, 255));
    ptr = (uint8_t const*)ptr - rolzContextDepth;
    index -= rolzContextDepth;
    size_t const headOffset                 = 0;
    size_t const hashOffset                 = 1;
    uint32_t const rowMask                  = (1u << rowLog) - 1;
    size_t const lzOffset                   = hashOffset + (1u << (rowLog - 2));
    size_t const rolzOffset                 = lzOffset + (1u << rowLog);
    size_t const predictedMatchLengthOffset = rolzOffset + (1u << rowLog);
    size_t const rowSizeU32                 = 1u + (10u << (rowLog - 2));
    ZS_HashPair const hash = ZS_combined_hash(ptr, tableLog, minLength, isRolz);
    uint32_t* const rowStart = comb->table + hash.row * rowSizeU32;
    uint32_t const pos = ZS_combined_nextIndex(rowStart + headOffset, rowMask);
    ZL_ASSERT_LE(pos, rowMask);

    // Set hash
    ((uint8_t*)(rowStart + hashOffset))[pos] = (uint8_t)hash.tag;

    // Set lz index
    (rowStart + lzOffset)[pos] = index;

    if (isRolz) {
        // Set rolz index
        (rowStart + rolzOffset)[pos] =
                ZS_combined_nextRolzIndex(comb->rolzIndex + context);

        // Set rolz predicted match length
        ((uint8_t*)(rowStart + predictedMatchLengthOffset))[pos] =
                (uint8_t)ZL_MIN(matchLength, 255);
    } else {
        (rowStart + rolzOffset)[pos] = 0;
    }
}

// ZS_combined_insert is only used in kCombined=1 builds, currently disabled
static ZL_UNUSED_ATTR void ZS_combined_insert(
        ZS_combined* comb,
        uint32_t context,
        void const* ptr,
        uint32_t index,
        uint32_t matchLength,
        bool isRolz)
{
    ZS_combined_insert2(
            comb,
            context,
            ptr,
            index,
            matchLength,
            isRolz,
            comb->rolzContextDepth,
            comb->minLength,
            comb->tableLog,
            comb->rowLog);
}

ZL_FORCE_INLINE ZS_CombinedMatch ZS_combined_findBestMatch2(
        ZS_combined* comb,
        ZS_window const* window,
        uint32_t context,
        uint8_t const* ip,
        uint8_t const* iend,
        bool allowLz,
        uint32_t rolzContextDepth,
        uint32_t rowLog,
        uint32_t tableLog,
        uint32_t minLength,
        bool predictMatchLength)
{
    ip -= rolzContextDepth;
    uint32_t const headOffset = 0;
    uint32_t const hashOffset = 1;
    uint32_t const rowEntries = 1u << rowLog;
    uint32_t const rowMask    = rowEntries - 1;
    uint32_t const lzOffset   = hashOffset + (1u << (rowLog - 2));
    uint32_t const rolzOffset = lzOffset + (1u << rowLog);
    uint32_t const predictedMatchLengthOffset = rolzOffset + (1u << rowLog);
    uint32_t const rowSizeU32                 = 1u + (10u << (rowLog - 2));
    ZS_HashPair const hash = ZS_combined_hash(ip, tableLog, minLength, true);

    uint8_t const* const base      = window->base;
    uint32_t const lowLimit        = window->lowLimit;
    uint32_t const rolzContextSize = comb->rolzContextSize;

    uint32_t const rolzHead      = comb->rolzIndex[context];
    uint32_t* const rowStart     = comb->table + hash.row * rowSizeU32;
    uint32_t const rowHead       = rowStart[headOffset];
    uint32_t nbSearches          = comb->nbSearches;
    uint32_t const lzMinLength   = comb->lzMinLength;
    uint32_t const rolzMinLength = comb->rolzMinLength;

    uint32_t lzIndices[32];
    uint32_t rolzIndices[32];
    uint8_t predictedMatchLengthes[32];
    size_t nbRolzMatches = 0;
    size_t nbLzMatches   = 0;

    {
        ZL_VecMask rolzMatches;
        ZL_VecMask lzMatches;
        if (rowLog == 4) {
            ZL_Vec128 const mask = ZL_Vec128_set8(0x7F);
            ZL_Vec128 hashes     = ZL_Vec128_read(rowStart + hashOffset);
            ZL_Vec128 hash1      = ZL_Vec128_set8((uint8_t)hash.tag);
            ZL_Vec128 cmpeq      = ZL_Vec128_cmp8(hashes, hash1);
            rolzMatches          = ZL_Vec128_mask8(cmpeq);

            // hashes    = ZL_Vec128_and(hashes, mask);
            hash1     = ZL_Vec128_and(hash1, mask);
            cmpeq     = ZL_Vec128_cmp8(hashes, hash1);
            lzMatches = ZL_Vec128_mask8(cmpeq);

            rolzMatches = ZL_VecMask_rotateRight(rolzMatches, rowHead, 16);
            lzMatches   = ZL_VecMask_rotateRight(lzMatches, rowHead, 16);
        } else {
            ZL_Vec256 const mask = ZL_Vec256_set8(0x7F);
            ZL_Vec256 hashes     = ZL_Vec256_read(rowStart + hashOffset);
            ZL_Vec256 hash1      = ZL_Vec256_set8((uint8_t)hash.tag);
            ZL_Vec256 cmpeq      = ZL_Vec256_cmp8(hashes, hash1);
            rolzMatches          = ZL_Vec256_mask8(cmpeq);

            // hashes    = ZL_Vec256_and(hashes, mask);
            hash1     = ZL_Vec256_and(hash1, mask);
            cmpeq     = ZL_Vec256_cmp8(hashes, hash1);
            lzMatches = ZL_Vec256_mask8(cmpeq);

            rolzMatches = ZL_VecMask_rotateRight(rolzMatches, rowHead, 32);
            lzMatches   = ZL_VecMask_rotateRight(lzMatches, rowHead, 32);
        }
        ZL_ASSERT_EQ(lzMatches & rolzMatches, 0);
        //> Find ROLZ matches
        for (; rolzMatches != 0 && nbSearches != 0;
             rolzMatches &= rolzMatches - 1, --nbSearches) {
            uint32_t const matchPos =
                    (rowHead + (uint32_t)ZL_VecMask_next(rolzMatches))
                    & rowMask;
            uint32_t const lzIndex = rowStart[lzOffset + matchPos];
            uint32_t const rolzIndex =
                    rolzHead - rowStart[rolzOffset + matchPos];
            uint8_t const predictedMatchLength =
                    ((uint8_t const*)(rowStart
                                      + predictedMatchLengthOffset))[matchPos];

            if (lzIndex < lowLimit || rolzIndex >= rolzContextSize)
                break;

            lzIndices[nbRolzMatches]              = lzIndex;
            rolzIndices[nbRolzMatches]            = rolzIndex;
            predictedMatchLengthes[nbRolzMatches] = predictedMatchLength;
            ++nbRolzMatches;
        }

        //> Add unsearched ROLZ matches to lz searches
        lzMatches |= rolzMatches;

        //> Find LZ matches
        if (allowLz) {
            for (; lzMatches != 0 && nbSearches != 0;
                 lzMatches &= lzMatches - 1, --nbSearches) {
                uint32_t const matchPos =
                        (rowHead + (uint32_t)ZL_VecMask_next(lzMatches))
                        & rowMask;
                uint32_t const lzIndex = rowStart[lzOffset + matchPos];

                if (lzIndex < lowLimit)
                    break;

                lzIndices[nbRolzMatches + nbLzMatches] = lzIndex;
                ++nbLzMatches;
            }
        }
    }
    ZL_ASSERT_LE(nbRolzMatches + nbLzMatches, comb->nbSearches);

    ZS_CombinedMatch best = { 0 };

    //> ROLZ search
    for (size_t i = 0; i < nbRolzMatches; ++i) {
        uint8_t const* const match = base + lzIndices[i];
        uint32_t matchLength       = 0;
        ZL_ASSERT_LT(match, ip);
        // if (match[best.matchLength] == ip[best.matchLength])
        matchLength = (uint32_t)ZS_count(ip, match, iend);
        if (matchLength >= best.matchLength && matchLength >= rolzMinLength) {
            ZL_ASSERT_GT(matchLength, rolzContextDepth);
            ZL_ASSERT_GE(matchLength, rolzMinLength);
            uint8_t const predictedMatchLength = predictedMatchLengthes[i];
            uint32_t const encodedMatchLength  = predictMatchLength
                     ? ZS_rolz_encodeMatchLength(
                              rolzMinLength - rolzContextDepth,
                              predictedMatchLength,
                              matchLength - rolzContextDepth)
                     : matchLength - rolzMinLength;
            bool const better                  = matchLength > best.matchLength
                    || encodedMatchLength < best.encodedMatchLength;
            if (better) {
                ZL_DLOG(SEQ,
                        "ML = %u EML = %u PML = %u",
                        matchLength - rolzContextDepth,
                        encodedMatchLength,
                        predictedMatchLength);
                best.matchType          = ZS_mt_rolz;
                best.matchIndex         = lzIndices[i];
                best.matchCode          = rolzIndices[i];
                best.matchLength        = matchLength;
                best.encodedMatchLength = encodedMatchLength;
            }
        }
    }

    //> Favor ROLZ matches always
    // if (best.matchLength > 0)
    //   return best;

    //> LZ search
    uint32_t const curr = (uint32_t)(ip - base);
    for (size_t i = nbRolzMatches; i < nbRolzMatches + nbLzMatches; ++i) {
        uint8_t const* const match = base + lzIndices[i];
        uint32_t matchLength       = 0;

        ZL_ASSERT_LT(match, ip);
        if (match[best.matchLength] == ip[best.matchLength])
            matchLength = (uint32_t)ZS_count(ip, match, iend);
        if (matchLength > best.matchLength && matchLength >= lzMinLength) {
            best.matchType   = ZS_mt_lz;
            best.matchIndex  = lzIndices[i];
            best.matchCode   = curr - lzIndices[i];
            best.matchLength = matchLength;
        }
    }

    if (best.matchType == ZS_mt_rolz)
        best.matchLength -= rolzContextDepth;

    return best;
}

// ZS_combined_findBestMatch is only used in kCombined=1 builds, currently
// disabled
static ZL_UNUSED_ATTR ZS_CombinedMatch ZS_combined_findBestMatch(
        ZS_combined* comb,
        ZS_window const* window,
        uint32_t context,
        uint8_t const* ip,
        uint8_t const* iend,
        bool allowLz)
{
    return ZS_combined_findBestMatch2(
            comb,
            window,
            context,
            ip,
            iend,
            allowLz,
            comb->rolzContextDepth,
            comb->rowLog,
            comb->tableLog,
            comb->minLength,
            comb->rolzPredictMatchLength);
}

// ZS_combined_prevIndex is only used in kCombined=1 builds, currently disabled
ZL_FORCE_INLINE ZL_UNUSED_ATTR uint32_t
ZS_combined_prevIndex(uint32_t* head, uint32_t rowMask)
{
    uint32_t const prev = *head;
    *head               = (*head + 1) & rowMask;
    return prev;
}

ZL_FORCE_INLINE uint32_t ZS_combined_rewindRolzIndex(uint32_t* rolzIndex)
{
    uint32_t const prev = *rolzIndex;
    *rolzIndex          = *rolzIndex - 1;
    return prev;
}

// ZS_combined_rollback is only used in kCombined=1 builds, currently disabled
ZL_FORCE_INLINE ZL_UNUSED_ATTR void ZS_combined_rollback(
        ZS_combined* comb,
        ZS_window const* window,
        uint8_t const* ip,
        uint32_t back,
        uint32_t contextDepth,
        uint32_t contextLog,
        uint32_t tableLog,
        uint32_t rowLog,
        uint32_t minLength)
{
    size_t const headOffset   = 0;
    size_t const hashOffset   = 1;
    uint32_t const rowEntries = 1u << rowLog;
    uint32_t const rowMask    = rowEntries - 1;
    size_t const lzOffset     = hashOffset + (1u << (rowLog - 2));
    size_t const rowSizeU32   = 1u + (10u << (rowLog - 2));
    (void)window;
    for (uint32_t b = 1; b <= back; ++b) {
        ZS_combined_rewindRolzIndex(
                comb->rolzIndex
                + ZS_rolz_getContext(ip - b, contextDepth, contextLog));
        ZS_HashPair const hash = ZS_combined_hash(
                ip - b - contextDepth, tableLog, minLength, true);
        uint32_t* const rowStart = comb->table + hash.row * rowSizeU32;
        uint32_t const pos = ZS_rolz_prevIndex(rowStart + headOffset, rowMask);
        ZL_ASSERT_LT(pos, rowEntries);
        ZL_ASSERT_EQ(
                window->base + rowStart[lzOffset + pos], ip - b - contextDepth);
        //> Don't need to rewrite it because we will fill it immediately.
        (void)pos;
        (void)lzOffset;
        rowStart[lzOffset + pos] = 0;
    }
}

typedef struct {
    ZS_matchFinderCtx base;
    ZS_MatchFinderParameters params;
#if kCombined
    ZS_combined comb;
#else
    ZS_lz2 lz2;
    ZS_rolz rolz;
#endif
} ZS_lazyCtx;

static void ZS_lazyMatchFinderCtx_releaseInternal(ZS_lazyCtx* ctx)
{
    if (!ctx)
        return;
#if kCombined
    ZS_combined_destroy(&ctx->comb);
#else
    ZS_rolz_destroy(&ctx->rolz);
    ZS_lz2_destroy(&ctx->lz2);
#endif
    free(ctx);
}

static void ZS_lazyMatchFinderCtx_release(ZS_matchFinderCtx* ctx)
{
    ZS_lazyMatchFinderCtx_releaseInternal(
            ZL_CONTAINER_OF(ctx, ZS_lazyCtx, base));
}

static ZS_matchFinderCtx* ZS_lazyMatchFinderCtx_create(
        ZS_window const* window,
        ZS_MatchFinderParameters const* params)
{
    ZS_lazyCtx* const ctx = (ZS_lazyCtx*)calloc(1, sizeof(ZS_lazyCtx));
    if (!ctx)
        goto err;
    ctx->params = *params;
#if kCombined
    if (ZS_combined_init(
                &ctx->comb,
                params->rolzContextLog,
                params->rolzContextDepth,
                1u << params->rolzRowLog,
                params->tableLog,
                params->rowLog,
                params->searchLog,
                params->minLength,
                params->lzMinLength,
                params->rolzMinLength,
                params->rolzPredictMatchLength))
        goto err;
#else
    if (ZS_rolz_init(
                &ctx->rolz,
                params->rolzContextLog,
                params->rolzRowLog,
                params->rolzMinLength,
                params->rolzSearchLog,
                params->rolzPredictMatchLength))
        goto err;

    if (ZS_lz2_init(
                &ctx->lz2,
                window,
                params->lzTableLog,
                params->lzRowLog,
                params->lzSearchLog,
                params->lzMinLength))
        goto err;
#endif
    ctx->base.window = window;

    return &ctx->base;
err:
    ZS_lazyMatchFinderCtx_releaseInternal(ctx);
    return NULL;
}

static void ZS_lazyMatchFinderCtx_reset(ZS_matchFinderCtx* baseCtx)
{
    ZS_lazyCtx* const ctx = ZL_CONTAINER_OF(baseCtx, ZS_lazyCtx, base);
#if kCombined
    ZS_combined_reset(&ctx->comb);
#else
    ZS_lz2_reset(&ctx->lz2);
    ZS_rolz_reset(&ctx->rolz);
#endif
}

static uint32_t ZS_repcode(uint32_t rep, int off)
{
    ZL_ASSERT_LT(rep, 3);
    ZL_ASSERT_GE(off, -4);
    ZL_ASSERT_LE(off, 4);
    return rep | ((uint32_t)(off + 4) << 2);
}

static char const* ZS_MatchType_name(ZS_matchType mt)
{
    switch (mt) {
        case ZS_mt_rep:
            return "REP";
        case ZS_mt_rep0:
            return "REP0";
        case ZS_mt_lz:
            return "LZ";
        case ZS_mt_rolz:
            return "ROLZ";
        case ZS_mt_lits:
            return "LITS";
        case ZS_mt_lzn:
            return "LZN";
        default:
            ZL_ASSERT_FAIL("Illegal match type!");
            return "";
    }
}

#if kCombined
static void ZS_lazyMatchFinder_parse(
        ZS_matchFinderCtx* baseCtx,
        ZS_RolzSeqStore* seqs,
        uint8_t const* src,
        size_t size)
{
    ZS_lazyCtx* const ctx = ZL_CONTAINER_OF(baseCtx, ZS_lazyCtx, base);

    uint8_t const* ip           = src;
    uint8_t const* anchor       = ip;
    uint8_t const* const iend   = ip + size;
    uint8_t const* const ilimit = iend - 8; // TODO Hash read size

    ZS_window const* window = baseCtx->window;
    // ZS_lz* lz                         = &ctx->lz;
    // ZS_rolz* rolz                     = &ctx->rolz;
    ZS_combined* comb           = &ctx->comb;
    uint32_t const contextDepth = ctx->params.rolzContextDepth;
    uint32_t const contextLog   = ctx->params.rolzContextLog;
    // uint32_t const lzSearchDelay = ctx->params.lzSearchDelay;
    uint32_t const repMinLength = ctx->params.repMinLength;
    uint32_t const lzMinLength  = ctx->params.lzMinLength;
    // uint32_t const rolzMinLength      = ctx->params.rolzMinLength;
    // bool const rolzEnabled            = ctx->params.rolzEnabled;
    // bool const lzEnabled              = ctx->params.lzEnabled;
    bool const rolzPredictMatchLength = ctx->params.rolzPredictMatchLength;

    uint32_t const tableLog  = ctx->params.tableLog;
    uint32_t const rowLog    = ctx->params.rowLog;
    uint32_t const minLength = ctx->params.minLength;

    // Skip the first contextDepth positions
    ip += contextDepth + 1;
    uint32_t rep0 = 0;
    uint32_t rep1 = 0;

    // TODO: Repcode search
    while (ip < ilimit) {
        uint32_t curr = (uint32_t)(ip - window->base);
        ZS_sequence seq;
        uint32_t matchLength;
        ZL_ASSERT_GE(ip, anchor);

        //> Repcode match
        if (rep0 && ZL_read32(ip) == ZL_read32(ip - rep0)) {
            matchLength = (uint32_t)ZS_count(ip, ip - rep0, iend);
            ZL_ASSERT_GE(matchLength, repMinLength);
            seq.matchType   = ZS_mt_rep0;
            seq.matchCode   = 0;
            seq.matchLength = matchLength - repMinLength;
            goto storeSequence;
        }

        uint32_t const context =
                ZS_rolz_getContext(ip, contextDepth, contextLog);
        ZS_CombinedMatch const m = ZS_combined_findBestMatch2(
                comb,
                window,
                context,
                ip,
                iend,
                /* allowLz */ ip - contextDepth >= anchor,
                contextDepth,
                rowLog,
                tableLog,
                minLength,
                rolzPredictMatchLength);
        if (m.matchLength > 0) {
            seq.matchType = m.matchType;
            seq.matchCode = m.matchCode;
            matchLength   = m.matchLength;
            rep1          = rep0;
            rep0          = curr - m.matchIndex - contextDepth;
            if (m.matchType == ZS_mt_rolz) {
                ZL_ASSERT_GE(matchLength, minLength - contextDepth);
                ZL_DLOG(SEQ, "ctx=%u off=%u", context, curr - m.matchIndex);
                ZS_combined_insert2(
                        comb,
                        context,
                        ip,
                        (uint32_t)(ip - window->base),
                        m.matchLength,
                        true,
                        contextDepth,
                        minLength,
                        tableLog,
                        rowLog);
                seq.matchLength = m.encodedMatchLength;
            } else {
                ZL_ASSERT_GE(matchLength, lzMinLength);
                ZL_ASSERT_GE(ip - contextDepth, anchor);
                ip -= contextDepth;
                uint8_t const* match         = window->base + m.matchIndex;
                uint8_t const* lowMatchLimit = window->base + window->lowLimit;
                uint32_t back                = 0;
                if (ip + matchLength < iend)
                    ZL_ASSERT_NE(ip[matchLength], match[matchLength]);
                while (ip - back > anchor && match - back > lowMatchLimit
                       && *(ip - back - 1) == *(match - back - 1)) {
                    ++back;
                    ++matchLength;
                }
                seq.matchLength = matchLength - lzMinLength;
                ZS_combined_rollback(
                        comb,
                        window,
                        ip + contextDepth,
                        ZL_MIN(back + contextDepth,
                               (uint32_t)(ip - src - contextDepth)),
                        contextDepth,
                        contextLog,
                        tableLog,
                        rowLog,
                        minLength);
                ip -= back;
            }
            goto storeSequence;
        }

        ZS_combined_insert2(
                comb,
                kRolzInsertLits
                        ? ZS_rolz_getContext(ip, contextDepth, contextLog)
                        : 0,
                ip,
                (uint32_t)(ip - window->base),
                0,
                /* isRolz */ kRolzInsertLits,
                contextDepth,
                minLength,
                tableLog,
                rowLog);
        ++ip;
        continue;

    storeSequence:
        seq.literalLength = (uint32_t)(ip - anchor);
        ZL_DLOG(SEQ,
                "%s mpos=%u code=%u mlen=%u",
                ZS_MatchType_name(seq.matchType),
                (uint32_t)(ip - src),
                seq.matchCode,
                matchLength);
        ZS_RolzSeqStore_store(seqs, 0, anchor, iend, &seq);
        if (seq.matchType != ZS_mt_rolz && ip - src >= contextDepth) {
            ZS_combined_insert2(
                    comb,
                    ZS_rolz_getContext(ip, contextDepth, contextLog),
                    ip,
                    (uint32_t)(ip - window->base),
                    matchLength,
                    /* isRolz */ true,
                    contextDepth,
                    minLength,
                    tableLog,
                    rowLog);
            ZS_combined_insert2(
                    comb,
                    ZS_rolz_getContext(ip + 1, contextDepth, contextLog),
                    ip + 1,
                    (uint32_t)(ip + 1 - window->base),
                    matchLength - 1,
                    /* isRolz */ true,
                    contextDepth,
                    minLength,
                    tableLog,
                    rowLog);
            anchor = ip + 2;
        } else {
            anchor = ip + 1;
        }
        ip += matchLength;
        ZL_ASSERT_LE(anchor, ip);
        for (; anchor < ip; ++anchor) {
            ZS_combined_insert2(
                    comb,
                    0,
                    anchor,
                    (uint32_t)(anchor - window->base),
                    0,
                    /* isRolz */ false,
                    contextDepth,
                    minLength,
                    tableLog,
                    rowLog);
        }

        // Immediate rep1 search
        if (ip < ilimit && rep1 && ZL_read32(ip) == ZL_read32(ip - rep1)) {
            matchLength = (uint32_t)ZS_count(ip, ip - rep1, iend);
            if (matchLength >= repMinLength)
                seq.matchType = ZS_mt_rep;
            seq.matchLength = matchLength - repMinLength;
            seq.matchCode   = ZS_repcode(1, 0);
            {
                uint32_t tmp = rep0;
                rep0         = rep1;
                rep1         = tmp;
            }
            goto storeSequence;
        }
    }
    ZL_ASSERT_LE(anchor, iend);
    ZS_RolzSeqStore_storeLastLiterals(seqs, anchor, (size_t)(iend - anchor));
}
#else
ZL_FORCE_INLINE int32_t
gain(ZS_matchType type,
     uint32_t matchCode,
     uint32_t literalLength,
     uint32_t matchLength)
{
    int32_t const mlBits = 8 * (int32_t)matchLength;
    switch (type) {
        case ZS_mt_rep0:
            return mlBits - 1 - 5 * (int32_t)literalLength;
        case ZS_mt_rolz:
            ZL_ASSERT_LT(matchCode, 16);
            // Slightly cheaper literals because we expect rolz literals to be
            // more predictable
            return mlBits - 4 - 3 * (int32_t)literalLength;
        case ZS_mt_lz:
            return mlBits - 2 * (int32_t)ZL_highbit32(matchCode)
                    - 5 * (int32_t)literalLength - 8;
        case ZS_mt_lits:
        case ZS_mt_lzn:
        case ZS_mt_rep:
        default:
            ZL_ASSERT_FAIL("Not supported");
            return 0;
    }
}

ZL_FORCE_INLINE bool search(
        ZS_sequence* seq,
        uint32_t* offset,
        uint32_t* matchLength,
        ZS_window const* window,
        uint32_t const context,
        uint8_t const* anchor,
        uint8_t const* ip,
        uint8_t const* iend,
        uint32_t const repMinLength,
        uint32_t contextDepth,
        ZS_rolz* rolz,
        bool const rolzEnabled,
        uint32_t const rolzMinLength,
        bool const rolzPredictMatchLength,
        ZS_lz2* lz2,
        bool const lzEnabled,
        uint32_t const lzMinLength,
        uint32_t const lzSearchDelay,
        ZS_MatchFinderStrategy_e const strategy,
        uint32_t const* rep)
{
    bool found                   = false;
    uint32_t const literalLength = (uint32_t)(ip - anchor);
    int32_t gain1                = *matchLength == 0 ? INT32_MIN
                                                     : gain(seq->matchType,
                                             seq->matchCode,
                                             seq->literalLength,
                                             *matchLength);
    uint8_t const* const base    = window->base;
    uint32_t const curr          = (uint32_t)(ip - base);
    (void)strategy;
    //> Repcode match
    if (rep[0] && ZL_read32(ip) == ZL_read32(ip - rep[0])) {
        uint32_t const matchLength2 = (uint32_t)ZS_count(ip, ip - rep[0], iend);
        if (matchLength2 >= repMinLength) {
            int32_t const gain2 =
                    gain(ZS_mt_rep0, 0, literalLength, matchLength2);
            if (gain2 > gain1) {
                gain1 = gain2;
                found = true;
                ZL_ASSERT_GE(matchLength2, repMinLength);
                *offset            = rep[0];
                seq->matchType     = ZS_mt_rep0;
                seq->matchCode     = 0;
                seq->literalLength = literalLength;
                seq->matchLength   = matchLength2 - repMinLength;
                *matchLength       = matchLength2;
                // if (strategy == ZS_MatchFinderStrategy_greedy)
                //   return true;
            }
        }
    }

    //> Rolz match
    if (rolzEnabled && ((1) || ip - anchor < contextDepth)) {
        ZS_RolzMatch const m = ZS_rolz_findBestMatch2(
                rolz,
                window,
                context,
                ip,
                iend,
                rolzMinLength,
                rolzPredictMatchLength);
        if (m.matchLength >= rolzMinLength) {
            int32_t const gain2 =
                    gain(ZS_mt_rolz, m.matchCode, literalLength, m.matchLength);
            if (gain2 > gain1) {
                gain1 = gain2;
                ZL_DLOG(SEQ, "ctx=%u off=%u", context, curr - m.matchIndex);
                found              = true;
                seq->matchType     = ZS_mt_rolz;
                seq->matchCode     = m.matchCode;
                seq->literalLength = literalLength;
                seq->matchLength   = m.encodedMatchLength;
                *offset            = curr - m.matchIndex;
                *matchLength       = m.matchLength;
                // if (strategy == ZS_MatchFinderStrategy_greedy)
                //   return true;
            }
        }
    }

    //> Lz match
    if (lzEnabled && ip - anchor >= lzSearchDelay) {
        ZS_Lz2Match const m =
                ZS_lz2_findBestMatch(lz2, window, ip - lzSearchDelay, iend);
        ZL_ASSERT_GT(lzMinLength, 0);
        if (m.matchLength >= lzMinLength) {
            int32_t const gain2 =
                    gain(ZS_mt_lz,
                         m.matchCode,
                         literalLength - lzSearchDelay,
                         m.matchLength);
            if (gain2 > gain1) {
                gain1              = gain2;
                found              = true;
                *matchLength       = m.matchLength;
                seq->matchType     = ZS_mt_lz;
                seq->literalLength = literalLength;
                seq->matchCode     = m.matchCode;
                seq->matchLength   = m.matchLength - lzMinLength;
                *offset            = m.matchCode;
            }
        }
    }
    return found;
}

ZL_FORCE_INLINE void ZS_lazyMatchFinder_parse_internal(
        ZS_matchFinderCtx* baseCtx,
        ZS_RolzSeqStore* seqs,
        uint8_t const* src,
        size_t size,
        ZS_MatchFinderStrategy_e const strategy)
{
    ZS_lazyCtx* const ctx = ZL_CONTAINER_OF(baseCtx, ZS_lazyCtx, base);

    uint8_t const* ip           = src;
    uint8_t const* anchor       = ip;
    uint8_t const* const iend   = ip + size;
    uint8_t const* const ilimit = iend - 16; // TODO Hash read size

    ZS_window const* window      = baseCtx->window;
    ZS_lz2* lz2                  = &ctx->lz2;
    ZS_rolz* rolz                = &ctx->rolz;
    uint32_t const contextDepth  = 2;  // ctx->params.rolzContextDepth;
    uint32_t const contextLog    = 12; // ctx->params.rolzContextLog;
    uint32_t const lzSearchDelay = 1;  // ctx->params.lzSearchDelay;
    uint32_t const repMinLength  = 3;  // ctx->params.repMinLength;
    uint32_t const lzMinLength   = 7;  // ctx->params.lzMinLength;
    uint32_t const rolzMinLength = 3;  // ctx->params.rolzMinLength;
    bool const rolzEnabled       = 1;  // ctx->params.rolzEnabled;
    bool const lzEnabled         = 1;  // ctx->params.lzEnabled;
    bool const rolzPredictMatchLength =
            1; // ctx->params.rolzPredictMatchLength;

    // Skip the first contextDepth positions
    ip += contextDepth;
    uint32_t rep[2] = { 0, 0 };

    if (ip < ilimit)
        ZS_lz2_fillHashCache(lz2, src, lzMinLength);

    // TODO: Repcode search
    while (ip < ilimit) {
        uint8_t const* start = ip;
        ZS_sequence seq;
        uint32_t matchLength = 0;
        uint32_t offset      = 0;
        ZL_ASSERT_GE(ip, anchor);

        ZS_rolz_prefetch(rolz, ip + 1, contextDepth, contextLog);

        uint32_t context = ZS_rolz_getContext(ip, contextDepth, contextLog);
        bool foundMatch =
                search(&seq,
                       &offset,
                       &matchLength,
                       window,
                       context,
                       anchor,
                       ip,
                       iend,
                       repMinLength,
                       contextDepth,
                       rolz,
                       rolzEnabled,
                       rolzMinLength,
                       rolzPredictMatchLength,
                       lz2,
                       lzEnabled,
                       lzMinLength,
                       lzSearchDelay,
                       strategy,
                       rep);
        if (!foundMatch) {
            if (kRolzInsertLits && rolzEnabled) {
                ZS_rolz_insert2(
                        rolz,
                        context,
                        ip,
                        (uint32_t)(ip - window->base),
                        0,
                        rolzMinLength);
            }
            ++ip;
            continue;
        }

        if (strategy != ZS_MatchFinderStrategy_greedy) {
            while (ip < ilimit) {
                ZS_rolz_insert2(
                        rolz,
                        context,
                        ip,
                        (uint32_t)(ip - window->base),
                        0,
                        rolzMinLength);
                ++ip;
                ZS_rolz_prefetch(rolz, ip + 1, contextDepth, contextLog);
                context = ZS_rolz_getContext(ip, contextDepth, contextLog);

                ZS_sequence seq2 = seq;
                uint32_t offset2;
                uint32_t matchLength2 = matchLength;
                foundMatch =
                        search(&seq2,
                               &offset2,
                               &matchLength2,
                               window,
                               context,
                               anchor,
                               ip,
                               iend,
                               repMinLength,
                               contextDepth,
                               rolz,
                               rolzEnabled,
                               rolzMinLength,
                               rolzPredictMatchLength,
                               lz2,
                               lzEnabled,
                               lzMinLength,
                               lzSearchDelay,
                               strategy,
                               rep);
                if (foundMatch) {
                    seq         = seq2;
                    start       = ip;
                    matchLength = matchLength2;
                    offset      = offset2;
                    continue;
                }
                break;
            }
        }

        if (seq.matchType != ZS_mt_rep0) {
            rep[1] = rep[0];
            rep[0] = offset;
        }
        if (seq.matchType == ZS_mt_lz) {
            start -= lzSearchDelay;
            ZL_ASSERT_GE(start, anchor);
            uint8_t const* match = start - seq.matchCode;
            uint8_t const* const lowMatchLimit =
                    window->base + window->dictLimit;
            while (start > anchor && match > lowMatchLimit
                   && start[-1] == match[-1]) {
                --start;
                --match;
                ++matchLength;
            }
            seq.matchLength = matchLength - lzMinLength;
        }
        ZL_ASSERT_LE(start, ip);
        if (ip > start) {
            if (kRolzInsertLits && rolzEnabled)
                ZS_rolz_rollback(
                        rolz,
                        window,
                        ip,
                        (uint32_t)(ip - ZL_MAX(start, src + contextDepth)),
                        contextDepth,
                        contextLog);
            ip = start;
        }
    repStoreSequence:
        ZL_ASSERT_EQ(ip, start);
        if (rolzEnabled && ip >= src + contextDepth) {
            ZS_rolz_insert2(
                    rolz,
                    ZS_rolz_getContext(ip, contextDepth, contextLog),
                    ip,
                    (uint32_t)(ip - window->base),
                    matchLength,
                    rolzMinLength);
            if (P1 && seq.matchType != ZS_mt_rolz)
                ZS_rolz_insert2(
                        rolz,
                        ZS_rolz_getContext(ip + 1, contextDepth, contextLog),
                        ip + 1,
                        (uint32_t)(ip + 1 - window->base),
                        matchLength - 1,
                        rolzMinLength);
        }
        ZS_rolz_prefetch(rolz, ip + matchLength, contextDepth, contextLog);
        if (LITS_ARE_SEQ && anchor < ip) {
            ZS_sequence lits = {
                .matchType     = ZS_mt_lits,
                .literalLength = (uint32_t)(ip - anchor),
                .matchCode     = 0,
                .matchLength   = 0,
            };
            ZL_DLOG(SEQ,
                    "LITS lpos=%u llen=%u",
                    (uint32_t)(anchor - src),
                    (uint32_t)(ip - anchor));
            ZS_RolzSeqStore_store(seqs, 0, anchor, iend, &lits);
            seq.literalLength = 0;
        } else {
            seq.literalLength = (uint32_t)(ip - anchor);
        }
        ZL_DLOG(SEQ,
                "%s mpos=%u code=%u mlen=%u",
                ZS_MatchType_name(seq.matchType),
                (uint32_t)(ip - src),
                seq.matchCode,
                matchLength);
        ZS_RolzSeqStore_store(seqs, 0, anchor, iend, &seq);
        ip += matchLength;
        anchor = ip;

        // Immediate rep1 search
        if (ip < ilimit && rep[1] && ZL_read32(ip) == ZL_read32(ip - rep[1])) {
            matchLength = (uint32_t)ZS_count(ip, ip - rep[1], iend);
            if (matchLength >= repMinLength)
                seq.matchType = ZS_mt_rep;
            seq.matchLength = matchLength - repMinLength;
            seq.matchCode   = ZS_repcode(1, 0);
            {
                uint32_t tmp = rep[0];
                rep[0]       = rep[1];
                rep[1]       = tmp;
            }
            start = ip;
            goto repStoreSequence;
        }
    }
    ZL_ASSERT_LE(anchor, iend);
    ZS_RolzSeqStore_storeLastLiterals(seqs, anchor, (size_t)(iend - anchor));
}
static void ZS_lazyMatchFinder_parse(
        ZS_matchFinderCtx* baseCtx,
        ZS_RolzSeqStore* seqs,
        uint8_t const* src,
        size_t size)
{
    ZS_lazyCtx* const ctx = ZL_CONTAINER_OF(baseCtx, ZS_lazyCtx, base);

    ZS_MatchFinderStrategy_e const strategy = ctx->params.strategy;
    if (strategy == ZS_MatchFinderStrategy_greedy)
        ZS_lazyMatchFinder_parse_internal(
                baseCtx, seqs, src, size, ZS_MatchFinderStrategy_greedy);
    else
        ZS_lazyMatchFinder_parse_internal(
                baseCtx, seqs, src, size, ZS_MatchFinderStrategy_lazy);
}
#endif

const ZS_matchFinder ZS_lazyMatchFinder = {
    .name        = "lazy",
    .ctx_create  = ZS_lazyMatchFinderCtx_create,
    .ctx_reset   = ZS_lazyMatchFinderCtx_reset,
    .ctx_release = ZS_lazyMatchFinderCtx_release,
    .parse       = ZS_lazyMatchFinder_parse,
};
