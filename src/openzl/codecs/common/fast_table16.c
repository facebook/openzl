// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/common/fast_table16.h"
#include "openzl/shared/portability.h"

#include <stdlib.h>

size_t ZS_FastTable16_tableSize(size_t tableLog)
{
    return ((size_t)1 << tableLog) * sizeof(((ZS_FastTable16*)NULL)->table[0]);
}

// We are forcing the compiler to not inline this function, so that it won't
// optimize the malloc + memset into a calloc.
ZL_FORCE_NOINLINE void ZS_FastTable16_clear(
        ZS_FastTable16* table,
        uint32_t tableLog,
        uint32_t minMatch)
{
    size_t const tableSize = ZS_FastTable16_tableSize(tableLog);
    memset(table->table, 0, tableSize);
    table->tableLog = tableLog;
    table->minMatch = minMatch;
}

void ZS_FastTable16_init(
        ZS_FastTable16* table,
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
    ZS_FastTable16_clear(table, tableLog, minMatch);
}

uint32_t ZS_FastTable16_getAndUpdate(
        ZS_FastTable16* table,
        uint8_t const* ptr,
        uint16_t pos)
{
    return ZS_FastTable16_getAndUpdateT(table, ptr, pos, table->minMatch);
}

void ZS_FastTable16_put(ZS_FastTable16* table, uint8_t const* ptr, uint16_t pos)
{
    ZS_FastTable16_putT(table, ptr, pos, table->minMatch);
}

uint32_t ZS_FastTable16_get(ZS_FastTable16* table, uint8_t const* ptr)
{
    return ZS_FastTable16_getT(table, ptr, table->minMatch);
}
