// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_MATCH_FINDER_FAST_TABLE_H
#define ZSTRONG_COMPRESS_MATCH_FINDER_FAST_TABLE_H

#include "openzl/common/assertion.h"
#include "openzl/shared/hash.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/**
 * Fast hash table without any collision resolution.
 * Table is sized to 2^tableLog uint32_t.
 * Hash looks at the first minMatch bytes of the ptr.
 */
typedef struct {
    uint32_t* table;
    uint32_t tableLog;
    uint32_t minMatch;
} ZS_FastTable;

/**
 * @returns The table size in bytes for a table log.
 */
size_t ZS_FastTable_tableSize(size_t tableLog);

/**
 * Initializes the hash table.
 *
 * @p memory A pointer of `ZS_FastTable_tableSize(tableLog)` bytes that is at
 *           least 4-byte aligned.
 * @p tableLog The log2 of the numer of entries in the table.
 * @p minMatch The number of bytes of the source to hash for the key.
 */
void ZS_FastTable_init(
        ZS_FastTable* table,
        void* memory,
        uint32_t tableLog,
        uint32_t minMatch);

/// Get the value at ptr. Replace the value with pos.
/// Templated by minMatch.
ZL_FORCE_INLINE uint32_t ZS_FastTable_getAndUpdateT(
        ZS_FastTable* table,
        uint8_t const* ptr,
        uint32_t pos,
        uint32_t const kMinMatch)
{
    ZL_ASSERT_EQ(kMinMatch, table->minMatch);
    size_t const hash    = ZL_hashPtr(ptr, table->tableLog, kMinMatch);
    uint32_t const match = table->table[hash];
    table->table[hash]   = pos;
    return match;
}

/// Get the value at ptr. Replace the value with pos.
uint32_t ZS_FastTable_getAndUpdate(
        ZS_FastTable* table,
        uint8_t const* ptr,
        uint32_t pos);

/// Put pos at ptr.
ZL_FORCE_INLINE void ZS_FastTable_putT(
        ZS_FastTable* table,
        uint8_t const* ptr,
        uint32_t pos,
        uint32_t kMinMatch)
{
    ZL_ASSERT_EQ(kMinMatch, table->minMatch);
    size_t const hash  = ZL_hashPtr(ptr, table->tableLog, kMinMatch);
    table->table[hash] = pos;
}

/// Put pos at ptr.
/// Templated by minMatch.
void ZS_FastTable_put(ZS_FastTable* table, uint8_t const* ptr, uint32_t pos);

ZL_FORCE_INLINE void ZS_FastTable_conditionalPutT(
        ZS_FastTable* table,
        uint8_t const* ptr,
        uint32_t pos,
        uint32_t kMinMatch,
        bool condition)
{
    ZL_ASSERT_EQ(kMinMatch, table->minMatch);
    size_t const hash  = ZL_hashPtr(ptr, table->tableLog, kMinMatch);
    table->table[hash] = condition ? pos : table->table[hash];
}

/// Get the value at ptr.
/// Templated by minMatch.
ZL_FORCE_INLINE uint32_t
ZS_FastTable_getT(ZS_FastTable* table, uint8_t const* ptr, uint32_t kMinMatch)
{
    ZL_ASSERT_EQ(kMinMatch, table->minMatch);
    size_t const hash = ZL_hashPtr(ptr, table->tableLog, kMinMatch);
    return table->table[hash];
}

/// Get the value at ptr.
uint32_t ZS_FastTable_get(ZS_FastTable* table, uint8_t const* ptr);

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_MATCH_FINDER_FAST_TABLE_H
