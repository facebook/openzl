// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_MATCH_FINDER_FAST_TAG_TABLE_H
#define ZSTRONG_COMPRESS_MATCH_FINDER_FAST_TAG_TABLE_H

#include "openzl/common/assertion.h"
#include "openzl/shared/hash.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

#define ZS_FastTagTable_kTagSize 3

typedef struct {
    uint32_t data[ZS_FastTagTable_kTagSize];
    uint32_t pos;
} ZS_FastTagTable_Entry;

#define ZS_FastTagTable_kMaxMatchLen \
    (sizeof(uint32_t) * ZS_FastTagTable_kTagSize)

/**
 * Fast hash table without any collision resolution.
 * Table is sized to 2^tableLog uint32_t.
 * Hash looks at the first minMatch bytes of the ptr.
 */
typedef struct {
    ZS_FastTagTable_Entry* table;
    uint32_t tableLog;
    uint32_t minMatch;
} ZS_FastTagTable;

/**
 * @returns The table size in bytes for a table log.
 */
size_t ZS_FastTagTable_tableSize(size_t tableLog);

/**
 * Initializes the hash table.
 *
 * @p memory A pointer of `ZS_FastTagTable_tableSize(tableLog)` bytes that is at
 *           least 4-byte aligned.
 * @p tableLog The log2 of the numer of entries in the table.
 * @p minMatch The number of bytes of the source to hash for the key.
 */
void ZS_FastTagTable_init(
        ZS_FastTagTable* table,
        void* memory,
        uint32_t tableLog,
        uint32_t minMatch);

ZL_FORCE_INLINE ZS_FastTagTable_Entry
ZS_FastTagTable_entry(uint8_t const* ptr, uint32_t pos)
{
    ZS_FastTagTable_Entry entry;
    if (0 && ZS_FastTagTable_kTagSize == 3) {
        memcpy(&entry, ptr, sizeof(entry));
    } else {
        memcpy(&entry, ptr, ZS_FastTagTable_kMaxMatchLen);
    }
    entry.pos = pos;
    return entry;
}

/// Get the value at ptr. Replace the value with pos.
/// Templated by minMatch.
ZL_FORCE_INLINE ZS_FastTagTable_Entry ZS_FastTagTable_getAndUpdateT(
        ZS_FastTagTable* table,
        uint8_t const* ptr,
        uint32_t pos,
        uint32_t const kMinMatch)
{
    ZL_ASSERT_EQ(kMinMatch, table->minMatch);
    size_t const hash = ZL_hashPtr(ptr, table->tableLog, kMinMatch);
    ZS_FastTagTable_Entry const match = table->table[hash];
    table->table[hash]                = ZS_FastTagTable_entry(ptr, pos);
    return match;
}

/// Get the value at ptr. Replace the value with pos.
ZS_FastTagTable_Entry ZS_FastTagTable_getAndUpdate(
        ZS_FastTagTable* table,
        uint8_t const* ptr,
        uint32_t pos);

/// Put pos at ptr.
ZL_FORCE_INLINE void ZS_FastTagTable_putT(
        ZS_FastTagTable* table,
        uint8_t const* ptr,
        uint32_t pos,
        uint32_t kMinMatch)
{
    ZL_ASSERT_EQ(kMinMatch, table->minMatch);
    size_t const hash  = ZL_hashPtr(ptr, table->tableLog, kMinMatch);
    table->table[hash] = ZS_FastTagTable_entry(ptr, pos);
}

/// Put pos at ptr.
/// Templated by minMatch.
void ZS_FastTagTable_put(
        ZS_FastTagTable* table,
        uint8_t const* ptr,
        uint32_t pos);

ZL_FORCE_INLINE void ZS_FastTagTable_conditionalPutT(
        ZS_FastTagTable* table,
        uint8_t const* ptr,
        uint32_t pos,
        uint32_t kMinMatch,
        bool condition)
{
    ZL_ASSERT_EQ(kMinMatch, table->minMatch);
    size_t const hash = ZL_hashPtr(ptr, table->tableLog, kMinMatch);
    table->table[hash] =
            condition ? ZS_FastTagTable_entry(ptr, pos) : table->table[hash];
}

/// Get the value at ptr.
/// Templated by minMatch.
ZL_FORCE_INLINE ZS_FastTagTable_Entry ZS_FastTagTable_getT(
        ZS_FastTagTable* table,
        uint8_t const* ptr,
        uint32_t kMinMatch)
{
    ZL_ASSERT_EQ(kMinMatch, table->minMatch);
    size_t const hash = ZL_hashPtr(ptr, table->tableLog, kMinMatch);
    return table->table[hash];
}

/// Get the value at ptr.
ZS_FastTagTable_Entry ZS_FastTagTable_get(
        ZS_FastTagTable* table,
        uint8_t const* ptr);

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_MATCH_FINDER_FAST_TAG_TABLE_H
