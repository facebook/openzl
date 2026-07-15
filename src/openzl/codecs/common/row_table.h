// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_COMPRESS_MATCH_FINDER_ROW_TABLE_H
#define ZSTRONG_COMPRESS_MATCH_FINDER_ROW_TABLE_H

#include "openzl/codecs/common/count.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/hash.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/simd_wrapper.h"

// Row-based hash table for multiple candidates at the same position.
//
// Currently doesn't have a backup for machines that don't have SSE.
// TODO: Look at performance. It should be decent as-is, but I haven't
// looked, so there is likely a lot to gain from the little things.

#define ZS_RowTable_kRowLog 4
#define ZS_RowTable_kRowSize (1 << ZS_RowTable_kRowLog)
#define ZS_RowTable_kRowMask (ZS_RowTable_kRowSize - 1)

typedef struct {
    uint8_t tags[ZS_RowTable_kRowSize];
    uint32_t head;
    uint32_t pos[ZS_RowTable_kRowSize];
    uint32_t padding[11];
} ZS_RowTable_Row;

typedef struct {
    ZS_RowTable_Row* table;
    uint32_t tableLog;
    uint32_t fieldSize;
    uint32_t minMatch;
    uint32_t nextToFill;
} ZS_RowTable;

typedef struct {
    uint32_t matchIdx;
    size_t forwardLength;
    size_t backwardLength;
    size_t totalLength;
} ZS_RowTable_Match;

/**
 * @returns The table size in bytes for a table log.
 */
static inline size_t ZS_RowTable_tableSize(uint32_t tableLog);

/**
 * Initialize the row hash table.
 *
 * @p memory A pointer of `ZS_RowTable_tableSize(tableLog)` bytes that is at
 *           least 4-byte aligned.
 * @p tableLog The log2 of the numer of entries in the table. Must be at least
 *             ZS_RowTable_kRowLog.
 * @p fieldSize The size of each atomic unit of source in bytes.
 * @p minMatch The number of bytes of the source to hash for the key.
 */
static inline void ZS_RowTable_init(
        ZS_RowTable* table,
        void* memory,
        uint32_t tableLog,
        uint32_t fieldSize,
        uint32_t minMatch);

/// Add pos to the table
/// Templated by kMinMatch which must match table->minMatch.
ZL_FORCE_INLINE void ZS_RowTable_putT(
        ZS_RowTable* table,
        uint8_t const* base,
        uint32_t pos,
        uint32_t const kMinMatch);

/// Add all positions up to, but not including, end to the table
/// Templated by kFieldSize which must match table->fieldSize.
/// Templated by kMinMatch which must match table->minMatch.
ZL_FORCE_INLINE void ZS_RowTable_fillT(
        ZS_RowTable* table,
        uint8_t const* base,
        uint32_t end,
        uint32_t const kFieldSize,
        uint32_t const kMinMatch);

/// Get up to nbMatches at pos, and add pos the table
/// Templated by kFieldSize which must match table->fieldSize.
/// Templated by kMinMatch which must match table->minMatch.
ZL_FORCE_INLINE size_t ZS_RowTable_getAndUpdateT(
        ZS_RowTable* table,
        uint8_t const* base,
        uint32_t lowLimit,
        uint32_t pos,
        uint32_t* matches,
        size_t nbSearches,
        uint32_t const kFieldSize,
        uint32_t const kMinMatch);

/// Get the best match at pos and add pos to the table
/// The match MUST be at least minLength bytes from pos.
/// Templated by kFieldSize which must match table->fieldSize.
/// Templated by kMinMatch which must match table->minMatch.
ZL_FORCE_INLINE ZS_RowTable_Match ZS_RowTable_getBestMatchAndUpdateT(
        ZS_RowTable* table,
        uint8_t const* base,
        uint8_t const* anchor,
        uint32_t lowLimit,
        uint32_t pos,
        uint8_t const* end,
        size_t nbSearches,
        size_t const minLength,
        uint32_t const kFieldSize,
        uint32_t const kMinMatch);

// Implementation

static inline size_t ZS_RowTable_tableSize(uint32_t tableLog)
{
    ZL_ASSERT_GE(tableLog, ZS_RowTable_kRowLog);
    size_t const tableSize  = (size_t)1 << (tableLog - ZS_RowTable_kRowLog);
    size_t const tableBytes = sizeof(ZS_RowTable_Row) * tableSize;
    return tableBytes;
}

// We are forcing the compiler to not inline this function, so that it won't
// optimize the malloc + memset into a calloc.
ZL_FORCE_NOINLINE void ZS_RowTable_clear(
        ZS_RowTable* table,
        uint32_t tableLog,
        uint32_t fieldSize,
        uint32_t minMatch)
{
    table->tableLog   = tableLog - 4;
    table->fieldSize  = fieldSize;
    table->minMatch   = minMatch;
    table->nextToFill = 0;
    memset(table->table, 0, ZS_RowTable_tableSize(tableLog));
}

static inline void ZS_RowTable_init(
        ZS_RowTable* table,
        void* memory,
        uint32_t tableLog,
        uint32_t fieldSize,
        uint32_t minMatch)
{
    ZL_ASSERT_EQ(sizeof(ZS_RowTable_Row), 128);
    ZL_ASSERT_NULL(table->table);
    table->table = memory;
    ZS_RowTable_clear(table, tableLog, fieldSize, minMatch);
}

ZL_FORCE_INLINE uint32_t ZS_RowTable_nextIndex(ZS_RowTable_Row* row)
{
    uint32_t const next = (row->head - 1) & ZS_RowTable_kRowMask;
    row->head           = next;
    return next;
}

ZL_FORCE_INLINE void ZS_RowTable_putT(
        ZS_RowTable* table,
        uint8_t const* base,
        uint32_t pos,
        uint32_t const kMinMatch)
{
    ZL_ASSERT_EQ(table->minMatch, kMinMatch);
    size_t const hashPair =
            ZL_hashPtr(base + pos, table->tableLog + 8, kMinMatch);
    size_t const rowIdx  = hashPair >> 8;
    uint8_t const tag    = hashPair & 0xFF;
    ZS_RowTable_Row* row = &table->table[rowIdx];
    uint32_t const idx   = ZS_RowTable_nextIndex(row);
    row->tags[idx]       = tag;
    row->pos[idx]        = pos;
}

ZL_FORCE_INLINE void ZS_RowTable_fillT(
        ZS_RowTable* table,
        uint8_t const* base,
        uint32_t end,
        uint32_t const kFieldSize,
        uint32_t const kMinMatch)
{
    ZL_ASSERT_EQ(table->fieldSize, kFieldSize);
    ZL_ASSERT_EQ(table->minMatch, kMinMatch);
    ZL_ASSERT_LE(table->nextToFill, end);
    ZL_ASSERT_EQ((end - table->nextToFill) % kFieldSize, 0);
    for (uint32_t pos = table->nextToFill; pos < end; pos += kFieldSize) {
        ZS_RowTable_putT(table, base, pos, kMinMatch);
    }
    table->nextToFill = end;
}

ZL_FORCE_INLINE uint16_t
ZS_RowTable_rotateMaskRight(uint16_t const value, uint32_t count)
{
    ZL_ASSERT_LT(count, 16);
    count &= 0x0F; /* for fickle pattern recognition */
    return (uint16_t)((value >> count) | (value << ((0U - count) & 0x0F)));
}

ZL_FORCE_INLINE uint32_t
ZS_RowTable_matchMask(void const* tags, uint8_t const tag, uint32_t head)
{
    ZL_ASSERT_EQ(ZS_RowTable_kRowSize, 16);
    ZL_Vec128 const haystack = ZL_Vec128_read(tags);
    ZL_Vec128 const needle   = ZL_Vec128_set8(tag);
    ZL_Vec128 const eq       = ZL_Vec128_cmp8(needle, haystack);
    ZL_VecMask const mask    = ZL_Vec128_mask8(eq);
    ZL_ASSERT_LT(head, 16);
    return ZS_RowTable_rotateMaskRight((uint16_t)mask, head);
}

ZL_FORCE_INLINE size_t ZS_RowTable_getAndUpdateT(
        ZS_RowTable* table,
        uint8_t const* base,
        uint32_t lowLimit,
        uint32_t pos,
        uint32_t* matches,
        size_t nbSearches,
        uint32_t const kFieldSize,
        uint32_t const kMinMatch)
{
    size_t nbMatches = 0;
    ZL_ASSERT_EQ(kFieldSize, table->fieldSize);
    ZL_ASSERT_EQ(kMinMatch, table->minMatch);
    ZL_ASSERT_EQ(pos, table->nextToFill);

    size_t const hashPair =
            ZL_hashPtr(base + pos, table->tableLog + 8, kMinMatch);
    size_t const rowIdx  = hashPair >> 8;
    uint8_t const tag    = hashPair & 0xFF;
    ZS_RowTable_Row* row = &table->table[rowIdx];

    // get matches
    uint32_t matchMask = ZS_RowTable_matchMask(row->tags, tag, row->head);
    for (; (matchMask > 0) && (nbSearches > 0);
         --nbSearches, matchMask &= matchMask - 1) {
        uint32_t const matchPos = (row->head + (uint32_t)ZL_ctz32(matchMask))
                & ZS_RowTable_kRowMask;
        ZL_ASSERT_EQ(row->tags[matchPos], tag);
        uint32_t const matchIndex = row->pos[matchPos];
        if (matchIndex < lowLimit)
            break;
        ZL_PREFETCH_L1(base + matchIndex);
        matches[nbMatches++] = matchIndex;
    }

    // put match
    uint32_t const idx = ZS_RowTable_nextIndex(row);
    row->tags[idx]     = tag;
    row->pos[idx]      = pos;
    table->nextToFill += kFieldSize;

    return nbMatches;
}

ZL_FORCE_INLINE ZS_RowTable_Match ZS_RowTable_getBestMatchAndUpdateT(
        ZS_RowTable* table,
        uint8_t const* base,
        uint8_t const* anchor,
        uint32_t lowLimit,
        uint32_t pos,
        uint8_t const* end,
        size_t nbSearches,
        size_t const minLength,
        uint32_t const kFieldSize,
        uint32_t const kMinMatch)
{
    ZL_ASSERT_EQ(kFieldSize, table->fieldSize);
    ZL_ASSERT_EQ(kMinMatch, table->minMatch);
    size_t const kFieldMask  = kFieldSize - 1;
    uint8_t const* const ptr = base + pos;
    uint32_t matches[ZS_RowTable_kRowSize];
    size_t const nbMatches = ZS_RowTable_getAndUpdateT(
            table,
            base,
            lowLimit,
            pos,
            matches,
            nbSearches,
            kFieldSize,
            kMinMatch);
    uint32_t bestMatch = 0;
    size_t bestLength  = minLength - 1;
    size_t backLength  = 0;

    if (base + pos + 4 * kFieldSize + kMinMatch < end) {
        size_t const hashPair = ZL_hashPtr(
                base + pos + 4 * kFieldSize, table->tableLog + 8, kMinMatch);
        size_t const rowIdx = hashPair >> 8;
        ZL_PREFETCH_L1(&table->table[rowIdx]);
        ZL_PREFETCH_L1(((uint8_t*)&table->table[rowIdx]) + 64);
    }

    assert(ptr + bestLength < end);

    // ZS_countBack is clamped to (ptr - anchor), so this bounds the backward
    // length of any candidate.
    size_t const maxBackLength = (size_t)(ptr - anchor);
    for (size_t m = 0; m < nbMatches; ++m) {
        uint8_t const* const match = base + matches[m];

        size_t const matchLength = ZS_count(ptr, match, end) & ~kFieldMask;
        if (matchLength < minLength
            || matchLength + maxBackLength <= backLength + bestLength) {
            continue;
        }
        size_t const bLen =
                ZS_countBack(ptr, match, anchor, base + lowLimit) & ~kFieldMask;
        if (bLen + matchLength > backLength + bestLength) {
            bestMatch  = matches[m];
            bestLength = matchLength;
            backLength = bLen;
            if (bestLength >= (size_t)(end - ptr))
                break;
        }
    }

    ZS_RowTable_Match match = {
        .matchIdx       = bestMatch,
        .forwardLength  = bestLength,
        .backwardLength = backLength,
        .totalLength    = backLength + bestLength,
    };
    return match;
}

#endif
