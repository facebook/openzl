// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZS_COMMON_ROLZ_H
#define ZS_COMMON_ROLZ_H

#include <stdlib.h>
#include <string.h>

#include "openzl/codecs/common/window.h"
#include "openzl/common/debug.h"
#include "openzl/shared/hash.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

#define MINMATCH 1

#define N2 0
#define N1 0
#define P0 1
#define P1 1
#define R1 0
#define kRolzHash 0
#define kRolzMatch 1
#define kRolzMTF 0
#define kRolzUpdate 0
#define kRolzMatchPredict 1
#define kRolzPutRep 1
#define kRolzPutRepP1 0
#define kRolzNumExpect 1
#define kRolzNumNegExpect 0
#define kRolzDynamicMin 0
#define kRolzInsertEveryPosition 1
#define kRolzMinMatchLengthInsert 6

#define kRolzInsertLits 1

#if kRolzHash && kRolzUpdate
#    error This doesnt work
#endif

// NOTE: Needs to support multiple update rules

// static const uint32_t kRolzNumContexts = 256;
// Keep position with longer match length?
// Stop "seeding" once rolz is full?
// Require a "mml" to admit? Keep only "good" positions
// Increase bucket size for popular contexts?
// We need to handle very full contexts probably
#if 0
static ZL_UNUSED_ATTR const uint32_t kRolzNumEntries = 1 << 14;  // 1024 * 1024;
#else
static ZL_UNUSED_ATTR const uint32_t kRolzNumEntries = 1024 * 1024;
#endif
static ZL_UNUSED_ATTR const uint32_t kRolzHashLog    = 10;
static ZL_UNUSED_ATTR const uint32_t kRolzHashLength = 4;
static ZL_UNUSED_ATTR const uint32_t kRolzCtxBits    = 8;
static ZL_UNUSED_ATTR const uint32_t kRolzHashBits   = 6;

ZL_FORCE_INLINE uint32_t getCtx(uint8_t const* ip, uint32_t kCtxBits)
{
    uint32_t const kCtxMask = (1u << kCtxBits) - 1;
    if (kCtxBits <= 8)
        return (uint32_t)ip[-1] & kCtxMask;
    if (kCtxBits <= 16)
        return ((uint32_t)ip[-1] | (uint32_t)(ip[-2] << 8)) & kCtxMask;
    ZL_ASSERT(kCtxBits <= 24);
    return ((uint32_t)ip[-1] | (uint32_t)(ip[-2] << 8)
            | (uint32_t)(ip[-3] << 16))
            & kCtxMask;
}

ZL_FORCE_INLINE uint32_t
ZS_rolz_hashContext(uint64_t bytes, uint32_t contextDepth, uint32_t contextLog)
{
    return (uint32_t)ZL_hash(bytes, contextLog, contextDepth);
}

ZL_FORCE_INLINE uint32_t ZS_rolz_getContext(
        uint8_t const* ip,
        uint32_t contextDepth,
        uint32_t contextLog)
{
    return (uint32_t)ZL_hashPtr(ip - contextDepth, contextLog, contextDepth);
}

typedef struct {
    uint32_t lzIndex;
    uint32_t rzIndex;
    uint8_t mlMin;
    uint8_t mlExpect;
} ZS_rzEntry;

ZL_FORCE_INLINE uint32_t
ZS_rolz_decodeMatchLength(uint32_t min, uint32_t expect, uint32_t ml)
{
    if (!kRolzMatchPredict)
        return ml;
    // return ml - 5;
    // ml -= MINMATCH;
    if (ml + min >= expect + kRolzNumExpect + kRolzNumNegExpect) {
        return ml + min - kRolzNumNegExpect;
    }
    if (ml >= kRolzNumExpect + kRolzNumNegExpect) {
        return (ml + min) - kRolzNumExpect - kRolzNumNegExpect;
    }
    return expect + ml - kRolzNumNegExpect;
}

ZL_FORCE_INLINE uint32_t
ZS_rolz_encodeMatchLength(uint32_t min, uint32_t expect, uint32_t ml)
{
    if (!kRolzMatchPredict)
        return ml;
    uint32_t const oml = ml;
    ZL_ASSERT_GE(ml, min);
    if (ml >= expect + kRolzNumExpect) {
        ml = ml - min + kRolzNumNegExpect;
    } else if (expect > kRolzNumNegExpect && ml < expect - kRolzNumNegExpect) {
        ml = ml - min + kRolzNumExpect + kRolzNumNegExpect;
    } else {
        ml = ml - expect + kRolzNumNegExpect;
    }
    // ml += MINMATCH;
    ZL_LOG_IF(
            oml != ZS_rolz_decodeMatchLength(min, expect, ml),
            V,
            "MIN=%u EXPECT=%u OML=%u ML=%u",
            min,
            expect,
            oml,
            ml);
    ZL_ASSERT_EQ(oml, ZS_rolz_decodeMatchLength(min, expect, ml));
    return ml;
}

ZL_INLINE uint32_t ZS_rzEntry_matchLength(ZS_rzEntry const* entry, uint32_t ml)
{
    return ZS_rolz_encodeMatchLength(entry->mlMin, entry->mlExpect, ml);
}

typedef struct {
    uint32_t real;
    uint32_t firstUsageDistance;
    uint8_t min;
    uint8_t expect;
    uint8_t usages;
    uint8_t from;
} ZS_rzML;

ZL_INLINE uint32_t ZS_rzML_matchLength(ZS_rzML const* entry, uint32_t ml)
{
    return ZS_rolz_encodeMatchLength(entry->min, entry->expect, ml);
}

#define ZS_RolzTable_kMaxBucketSize 256
#define ZS_RolzTable_kChunkLog 4
#define ZS_RolzTable_kChunkSize (1u << ZS_RolzTable_kChunkLog)
#define ZS_RolzTable_kChunkMask (ZS_RolzTable_kChunkSize - 1)
#define ZS_RolzTable_kInlineSize 8
#define ZS_RolzTable_kMaxNbChunks 15
#define ZS_RolzTable_kInlineMask (ZS_RolzTable_kInlineSize - 1)

// 0 indirections

typedef struct {
    uint32_t indices[ZS_RolzTable_kInlineSize]; /* 4*8 = 32 */
    uint16_t size;                              /* 32 + 2 = 34 */
    uint16_t chunks[ZS_RolzTable_kMaxNbChunks]; /* 34 + 2*15 = 64 */
} ZS_RolzTable_Bucket;

ZL_STATIC_ASSERT(
        sizeof(ZS_RolzTable_Bucket)
                == (ZS_RolzTable_kInlineSize == 8 ? 64 : 96),
        "");

typedef struct {
    uint32_t indices[ZS_RolzTable_kChunkSize];
} ZS_RolzTable_Chunk;

// 0-1 indirections
typedef struct {
    ZS_RolzTable_Bucket* buckets;
    ZS_RolzTable_Chunk* chunks;
    uint32_t nbBuckets; //< 1 << contextLog
    uint32_t nextChunk;
    uint32_t chunkMask;
} ZS_RolzTable;

ZL_INLINE void ZS_RolzTable_destroy(ZS_RolzTable* table)
{
    free(table->buckets);
    free(table->chunks);
    memset(table, 0, sizeof(*table));
}

ZL_INLINE ZL_Report
ZS_RolzTable_init(ZS_RolzTable* table, size_t contextLog, size_t chunkLog)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT((ZL_OperationContext*)NULL);
    uint32_t const nbBuckets = 1u << contextLog;
    uint32_t const nbChunks  = 1u << chunkLog;
    ZL_REQUIRE_UINT_FITS(nbBuckets - 1, uint16_t);
    ZL_REQUIRE_UINT_FITS(nbChunks - 1, uint16_t);
    table->nbBuckets = nbBuckets;
    table->nextChunk = 0;
    table->chunkMask = nbChunks - 1;
    table->buckets   = (ZS_RolzTable_Bucket*)calloc(
            nbBuckets, sizeof(ZS_RolzTable_Bucket));
    table->chunks =
            (ZS_RolzTable_Chunk*)calloc(nbChunks, sizeof(ZS_RolzTable_Chunk));
    if (table->buckets == NULL || table->chunks == NULL) {
        ZS_RolzTable_destroy(table);
        ZL_ERR(GENERIC);
    }
    ZL_REQUIRE_EQ((uintptr_t)table->buckets & 64, 0);
    ZL_REQUIRE_EQ((uintptr_t)table->chunks & 64, 0);
    return ZL_returnSuccess();
}

ZL_INLINE void ZS_RolzTable_reset(ZS_RolzTable* table)
{
    uint32_t const nbBuckets = table->nbBuckets;
    table->nextChunk         = 0;
    for (size_t bucket = 0; bucket < nbBuckets; ++bucket) {
        table->buckets[bucket].size = 0;
    }
}

// Broken up for prefetching
ZL_INLINE ZS_RolzTable_Bucket* ZS_RolzTable_getBucket(
        ZS_RolzTable const* table,
        uint32_t context /* validated */)
{
    return &table->buckets[context];
}

// Broken up for prefetching
ZL_INLINE uint32_t const* ZS_RolzTable_getIndexPtr(
        ZS_RolzTable const* table,
        ZS_RolzTable_Bucket const* bucket,
        uint32_t rolzOffset /* validated - starts at 1 */)
{
    ZL_ASSERT_GE(rolzOffset, 1);
    ZL_ASSERT_LE(rolzOffset, bucket->size);
    // ZL_ASSERT_LE(bucket->size, ZS_RolzTable_kMaxBucketSize);
    uint32_t const inlineSize = bucket->size & ZS_RolzTable_kInlineMask;
    if (rolzOffset <= inlineSize) {
        return &bucket->indices[inlineSize - rolzOffset];
    }
    if (ZS_RolzTable_kInlineSize != ZS_RolzTable_kChunkSize)
        rolzOffset += bucket->size & ZS_RolzTable_kInlineSize;
    rolzOffset -= inlineSize + 1;
    uint32_t const idxOfChunk = rolzOffset >> ZS_RolzTable_kChunkLog;
    uint32_t const idxInChunk =
            ZS_RolzTable_kChunkSize - (rolzOffset & ZS_RolzTable_kChunkMask);
    // TODO: Could skip validation if we mask here
    uint32_t const* const indices = table->chunks[idxOfChunk].indices;
    return &indices[idxInChunk];
}

// Simplified function
ZL_INLINE uint32_t ZS_RolzTable_getIndex(
        ZS_RolzTable const* table,
        uint32_t context,
        uint32_t rolzOffset /* validated - starts at 1 */)
{
    return *ZS_RolzTable_getIndexPtr(
            table, ZS_RolzTable_getBucket(table, context), rolzOffset);
}

/**
 * Inserts `index` into `bucket` of `table`. `lowestValidIndex` tells us
 * the lowest valid indices. Any indices below this value can be thrown
 * out during eviction.
 *
 * Eviction:
 * * Evict the oldest chunk.
 */
ZL_INLINE void ZS_RolzTable_put(
        ZS_RolzTable* table,
        ZS_RolzTable_Bucket* bucket,
        uint32_t context,
        uint32_t index)
{
    ZL_ASSERT_EQ(&table->buckets[context], bucket);
    size_t const inlineSize     = bucket->size & ZS_RolzTable_kInlineMask;
    bucket->indices[inlineSize] = index;
    ++bucket->size;

    if (ZL_LIKELY(inlineSize < ZS_RolzTable_kInlineMask)) {
        //> n-1 / n of the time
        return;
    }

    if (ZS_RolzTable_kInlineSize == ZS_RolzTable_kChunkSize) {
        uint16_t const nextChunk = (uint16_t)table->nextChunk;
        ZL_ARRAY_SHIFT_RIGHT(bucket->chunks, 1);
        bucket->chunks[0] = nextChunk;
        table->nextChunk  = (uint32_t)(nextChunk + 1) & table->chunkMask;
        ZS_RolzTable_Chunk* const chunk = &table->chunks[nextChunk];
        memcpy(chunk->indices, bucket->indices, sizeof(bucket->indices));
    } else {
        ZL_ASSERT(2 * ZS_RolzTable_kInlineSize == ZS_RolzTable_kChunkSize);
        ZL_ASSERT(
                (bucket->size & ZS_RolzTable_kInlineSize)
                ^ (bucket->size & ZS_RolzTable_kChunkSize));

        size_t const idxInChunk = bucket->size & ZS_RolzTable_kInlineSize;

        if (idxInChunk == 0) {
            //> Need a new chunk
            ZL_ARRAY_SHIFT_RIGHT(bucket->chunks, 1);
            bucket->chunks[0] = (uint16_t)table->nextChunk;
            table->nextChunk  = (table->nextChunk + 1) & table->chunkMask;
        }
        ZS_RolzTable_Chunk* const chunk = &table->chunks[bucket->chunks[0]];
        memcpy(chunk->indices + idxInChunk,
               bucket->indices,
               sizeof(bucket->indices));
    }
}

ZL_END_C_DECLS

#endif // ZS_COMMON_ROLZ_H
