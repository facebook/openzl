// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/common/fast_tag_table.h"
#include "openzl/shared/portability.h"

#include <stdlib.h>

size_t ZS_FastTagTable_tableSize(size_t tableLog)
{
    return ((size_t)1 << tableLog) * sizeof(((ZS_FastTagTable*)NULL)->table[0]);
}

// We are forcing the compiler to not inline this function, so that it won't
// optimize the malloc + memset into a calloc.
ZL_FORCE_NOINLINE void ZS_FastTagTable_clear(
        ZS_FastTagTable* table,
        uint32_t tableLog,
        uint32_t minMatch)
{
    size_t const tableSize = ZS_FastTagTable_tableSize(tableLog);
    memset(table->table, 0, tableSize);
    table->tableLog = tableLog;
    table->minMatch = minMatch;
}

void ZS_FastTagTable_init(
        ZS_FastTagTable* table,
        void* memory,
        uint32_t tableLog,
        uint32_t minMatch)
{
    // malloc + memset is faster than calloc because we have a random access
    // pattern. If we don't memset the table, the pages will be filled into the
    // page table one at a time, whenever we first write to that page. That is
    // a lot less efficient than linearly loading the entire table into the page
    // table, as we memset it.
    ZL_ASSERT_NULL(table->table);
    table->table = memory;
    ZS_FastTagTable_clear(table, tableLog, minMatch);
}

ZS_FastTagTable_Entry ZS_FastTagTable_getAndUpdate(
        ZS_FastTagTable* table,
        uint8_t const* ptr,
        uint32_t pos)
{
    return ZS_FastTagTable_getAndUpdateT(table, ptr, pos, table->minMatch);
}

void ZS_FastTagTable_put(
        ZS_FastTagTable* table,
        uint8_t const* ptr,
        uint32_t pos)
{
    ZS_FastTagTable_putT(table, ptr, pos, table->minMatch);
}

ZS_FastTagTable_Entry ZS_FastTagTable_get(
        ZS_FastTagTable* table,
        uint8_t const* ptr)
{
    return ZS_FastTagTable_getT(table, ptr, table->minMatch);
}
