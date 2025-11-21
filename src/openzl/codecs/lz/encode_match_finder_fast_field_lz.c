// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "openzl/codecs/common/fast_table.h"
#include "openzl/codecs/common/window.h"
#include "openzl/codecs/lz/encode_field_lz_sequences.h"
#include "openzl/codecs/lz/encode_match_finder.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/hash.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_portability.h"

#define kTableLog 14
#define kRepMinMatch 3
#define kLzMinMatch 7

#define kMaxOffset ((1u << 24) - 1)

#define kSearchStrength 6

#define kPrefetch true

typedef struct {
    ZS_matchFinderCtx base;
    ZS_FastTable smallTable;
    ZS_FastTable largeTable;
    ZS_MatchFinderParameters params;
} ZS_tokenLzCtx;

static uint32_t smallMatchLength(uint32_t fieldSize)
{
    (void)fieldSize;
    if (fieldSize == 4)
        return 8;
    return fieldSize < 4 ? 4 : fieldSize;
}

static uint32_t largeMatchLength(uint32_t fieldSize)
{
    (void)fieldSize;
    if (fieldSize == 4)
        return 12;
    return fieldSize < 4 ? 8 : 2 * fieldSize;
}

static ZL_UNUSED_ATTR size_t
ZS_tokenLzMatchFinderCtx_sizeNeeded(ZS_MatchFinderParameters const* params)
{
    // For lzLargeMatch we have two tables
    size_t const multiplier = params->lzLargeMatch ? 2 : 1;
    return ZS_FastTable_tableSize(params->lzTableLog) * multiplier;
}

static ZS_matchFinderCtx* ZS_tokenLzMatchFinderCtx_create(
        ZS_window const* window,
        ZS_MatchFinderParameters const* params)
{
    uint32_t const kSmallMatch = smallMatchLength(params->fieldSize);
    uint32_t const kLargeMatch = largeMatchLength(params->fieldSize);
    ZS_tokenLzCtx* const ctx =
            params->alloc.alloc(params->alloc.opaque, sizeof(ZS_tokenLzCtx));
    if (!ctx) {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->params = *params;

    void* smallTableMem = params->alloc.alloc(
            params->alloc.opaque, ZS_FastTable_tableSize(params->lzTableLog));
    if (!smallTableMem) {
        return NULL;
    }
    ZS_FastTable_init(
            &ctx->smallTable, smallTableMem, params->lzTableLog, kSmallMatch);
    if (params->lzLargeMatch) {
        void* largeTableMem = params->alloc.alloc(
                params->alloc.opaque,
                ZS_FastTable_tableSize(params->lzTableLog));
        if (!largeTableMem) {
            return NULL;
        }
        ZS_FastTable_init(
                &ctx->largeTable,
                largeTableMem,
                params->lzTableLog,
                kLargeMatch);
    }

    ctx->base.window = window;

    return &ctx->base;
}

ZL_FORCE_INLINE bool
ZS_checkMatch(uint8_t const* value, uint8_t const* match, uint32_t kSmallMatch)
{
    switch (kSmallMatch) {
        case 2:
            return ZL_read16(value) == ZL_read16(match);
        case 3:
            return ZL_read24(value) == ZL_read24(match);
        case 4:
            return ZL_read32(value) == ZL_read32(match);
        default:
        case 8:
            return ZL_read64(value) == ZL_read64(match);
    }
}

ZL_FORCE_INLINE size_t ZS_countFields(
        uint8_t const* ip,
        uint8_t const* match,
        uint8_t const* iend,
        uint32_t const kFieldSize)
{
    uint8_t const* const start  = ip;
    uint8_t const* const ilimit = iend - kFieldSize + 1;
    while (ip < ilimit && ZS_checkMatch(ip, match, kFieldSize)) {
        ip += kFieldSize;
        match += kFieldSize;
    }
    return (size_t)(ip - start);
}

#define kHashSize 16
#define kHashMask (kHashSize - 1)

typedef struct {
    uint32_t small[kHashSize];
    uint32_t large[kHashSize];
    uint8_t const* next;
} ZS_HashCache;

#if kPrefetch
#    define PREFETCH(ptr) ZL_PREFETCH_L1(ptr)
#else
#    define PREFETCH(ptr) (void)(ptr)
#endif

ZL_FORCE_INLINE
void ZS_HashCache_init(
        ZS_HashCache* hc,
        ZS_FastTable const* smallTable,
        ZS_FastTable const* largeTable,
        uint8_t const* istart,
        uint8_t const* ip,
        uint8_t const* iend,
        uint32_t kFieldSize,
        bool kUseLarge)
{
    memset(hc, 0, sizeof(*hc));
    int const kFieldBits        = ZL_highbit32(kFieldSize);
    uint32_t const kSmallMatch  = smallMatchLength(kFieldSize);
    uint32_t const kLargeMatch  = largeMatchLength(kFieldSize);
    uint8_t const* const ilimit = iend - kLargeMatch + 1;
    size_t idx                  = (size_t)(ip - istart) >> kFieldBits;
    for (size_t i = 0; i < kHashSize && ip < ilimit; ++i, ip += kFieldSize) {
        size_t const smallHash =
                ZL_hashPtr(ip, smallTable->tableLog, kSmallMatch);
        hc->small[(idx + i) & kHashMask] = (uint32_t)smallHash;
        PREFETCH(smallTable->table + smallHash);
        if (kUseLarge) {
            size_t const largeHash =
                    ZL_hashPtr(ip, largeTable->tableLog, kLargeMatch);
            ;
            hc->large[(idx + i) & kHashMask] = (uint32_t)largeHash;
            PREFETCH(largeTable->table + largeHash);
        }
    }
    for (size_t i = 0; i < kHashSize / 2; ++i) {
        PREFETCH(istart + smallTable->table[hc->small[(idx + i) & kHashMask]]);
        if (kUseLarge)
            PREFETCH(
                    istart
                    + largeTable->table[hc->large[(idx + i) & kHashMask]]);
    }
    // ZL_DLOG(ERROR, "INIT");
}

typedef struct {
    uint32_t small;
    uint32_t large;
} ZS_Hashes;

ZL_FORCE_INLINE
ZS_Hashes ZS_HashCache_update(
        ZS_HashCache* hc,
        ZS_FastTable* smallTable,
        ZS_FastTable* largeTable,
        uint8_t const* istart,
        uint8_t const* ip,
        uint8_t const* iend,
        uint32_t kFieldSize,
        bool kUseLarge)
{
    int const kFieldBits       = ZL_highbit32(kFieldSize);
    uint32_t const kSmallMatch = smallMatchLength(kFieldSize);
    uint32_t const kLargeMatch = largeMatchLength(kFieldSize);
    size_t const idx = ((size_t)(ip - istart) >> kFieldBits) & kHashMask;
    uint8_t const* const ilimit = iend - kLargeMatch + 1;
    // ZL_DLOG(ERROR, "UPDATE %zu", idx);

    ZS_Hashes const hashes = {
        .small = hc->small[idx],
        .large = kUseLarge ? hc->large[idx] : 0,
    };

    ZL_ASSERT_LT(ip, ilimit);
    ZL_ASSERT(
            hashes.small == ZL_hashPtr(ip, smallTable->tableLog, kSmallMatch));
    if (kUseLarge)
        ZL_ASSERT(
                hashes.large
                == ZL_hashPtr(ip, largeTable->tableLog, kLargeMatch));

    uint8_t const* const np = ip + kFieldSize * kHashSize;
    if (np < ilimit) {
        size_t const smallHash =
                ZL_hashPtr(np, smallTable->tableLog, kSmallMatch);
        hc->small[idx] = (uint32_t)smallHash;
        PREFETCH(smallTable->table + smallHash);
        if (kUseLarge) {
            size_t const largeHash =
                    ZL_hashPtr(np, largeTable->tableLog, kLargeMatch);
            hc->large[idx] = (uint32_t)largeHash;
            PREFETCH(largeTable->table + largeHash);
        }
    }
    size_t const next = (idx + kHashSize / 2) & kHashMask;
    if (kUseLarge)
        PREFETCH(istart + largeTable->table[hc->large[next]]);
    PREFETCH(istart + smallTable->table[hc->small[next]]);

    hc->next = ip + kFieldSize;

    return hashes;
}

ZL_FORCE_INLINE
void ZS_HashCache_skip(
        ZS_HashCache* hc,
        ZS_FastTable* smallTable,
        ZS_FastTable* largeTable,
        uint8_t const* istart,
        uint8_t const* ip,
        uint8_t const* iend,
        uint32_t kFieldSize,
        bool kUseLarge)
{
    uint8_t const* prev     = hc->next;
    size_t const skipLength = (size_t)(ip - prev);
    // ZL_DLOG(ERROR, "SKIP %zu", skipLength / kFieldSize);
    if (skipLength >= kHashSize * kFieldSize) {
        ZS_HashCache_init(
                hc,
                smallTable,
                largeTable,
                istart,
                ip,
                iend,
                kFieldSize,
                kUseLarge);
        return;
    }
    for (; prev < ip; prev += kFieldSize) {
        ZS_HashCache_update(
                hc,
                smallTable,
                largeTable,
                istart,
                prev,
                iend,
                kFieldSize,
                kUseLarge);
    }
}

ZL_FORCE_INLINE
uint32_t
ZS_FastTable_getAndUpdateHC(ZS_FastTable* table, uint32_t hash, uint32_t value)
{
    uint32_t const ret = table->table[hash];
    table->table[hash] = value;
    return ret;
}

ZL_FORCE_INLINE
bool findMatchDFast(
        ZS_sequence* seq,
        ZS_Hashes hashes,
        uint8_t const** match,
        ZS_FastTable* smallTable,
        ZS_FastTable* largeTable,
        uint8_t const* base,
        uint8_t const* anchor,
        uint8_t const* ip,
        uint8_t const* iend,
        uint32_t lowLimit,
        uint32_t kFieldSize,
        bool kUseLarge)
{
    uint32_t const kFieldMask  = kFieldSize - 1;
    int const kFieldBits       = ZL_highbit32(kFieldSize);
    uint32_t const kSmallMatch = smallMatchLength(kFieldSize);
    uint32_t const kLargeMatch = largeMatchLength(kFieldSize);

    ZL_ASSERT_LE(ip + kSmallMatch, iend);
    ZL_ASSERT_LE(ip + kLargeMatch, iend);
    // Check the large match
    if (kUseLarge) {
        uint32_t const largeMatchPos = ZS_FastTable_getAndUpdateHC(
                largeTable, hashes.large, (uint32_t)(ip - base));
        *match                = base + largeMatchPos;
        uint32_t const offset = (uint32_t)(ip - *match);
        ZL_ASSERT_LT(*match, ip);
        if (largeMatchPos >= lowLimit && offset < kMaxOffset
            && ZS_checkMatch(ip, *match, kSmallMatch)) {
            ZL_ASSERT(!(offset & kFieldMask));
            seq->matchType     = ZS_mt_lz;
            seq->matchCode     = offset >> kFieldBits;
            seq->literalLength = (uint32_t)(ip - anchor);
            seq->matchLength =
                    (uint32_t)ZS_countFields(ip, *match, iend, kFieldSize);
            ZL_ASSERT_GE(seq->matchLength, kFieldSize);
            return true;
        }
    }
    // Check the small match
    {
        uint32_t const smallMatchPos = ZS_FastTable_getAndUpdateHC(
                smallTable, hashes.small, (uint32_t)(ip - base));
        *match = base + smallMatchPos;
        ZL_ASSERT_LT(*match, ip);
        uint32_t const offset = (uint32_t)(ip - *match);
        if (smallMatchPos >= lowLimit && offset < kMaxOffset
            && ZS_checkMatch(ip, *match, kSmallMatch)) {
            ZL_ASSERT(!(offset & kFieldMask));
            seq->matchType     = ZS_mt_lz;
            seq->matchCode     = offset >> kFieldBits;
            seq->literalLength = (uint32_t)(ip - anchor);
        } else {
            return false;
        }
    }

    // Check the next position long match
    // TODO: Test enabling this. It is disabled because we just
    // search the next position. If it is enabled, it may insert
    // into the hash table at the end of the match, which causes
    // offset=0 matches.
    if ((0)) {
        uint8_t const* const ip1     = ip + kFieldSize;
        uint32_t const largeMatchPos = ZS_FastTable_getAndUpdateT(
                largeTable, ip1, (uint32_t)(ip1 - base), kLargeMatch);
        uint8_t const* largeMatch = base + largeMatchPos;
        uint32_t const offset     = (uint32_t)(ip1 - largeMatch);
        if (largeMatchPos >= lowLimit && offset < kMaxOffset
            && ZS_checkMatch(ip1, largeMatch, kSmallMatch)) {
            ZL_ASSERT(!(offset & kFieldMask));
            *match             = largeMatch;
            seq->matchType     = ZS_mt_lz;
            seq->matchCode     = offset >> kFieldBits;
            seq->literalLength = (uint32_t)(ip1 - anchor);
            ip                 = ip1;
        }
    }
    seq->matchLength = (uint32_t)ZS_countFields(ip, *match, iend, kFieldSize);
    ZL_ASSERT_GE(seq->matchLength, kFieldSize);
    return true;
}

ZL_FORCE_INLINE void ZS_tokenLzMatchFinder_parseT(
        ZS_matchFinderCtx* baseCtx,
        ZS_seqStore* seqs,
        uint8_t const* src,
        size_t size,
        uint32_t const kFieldSize,
        bool kUseLarge)
{
    ZS_tokenLzCtx* const ctx = ZL_CONTAINER_OF(baseCtx, ZS_tokenLzCtx, base);

    uint8_t const* ip         = src;
    uint8_t const* anchor     = ip;
    uint8_t const* const iend = ip + size;
    uint8_t const* const ilimit =
            iend - ZL_MAX(kFieldSize, 16); // TODO Hash read size

    uint8_t const* const base      = baseCtx->window->base;
    uint32_t const lowLimit        = baseCtx->window->lowLimit;
    uint8_t const* const windowLow = base + lowLimit;
    ZS_FastTable smallTable        = ctx->smallTable;
    ZS_FastTable largeTable        = ctx->largeTable;

    uint32_t const kFieldMask  = kFieldSize - 1;
    uint32_t const kSmallMatch = smallMatchLength(kFieldSize);
    uint32_t const kLargeMatch = largeMatchLength(kFieldSize);
    // uint32_t const kStepSize    = kFieldSize <= 8 ? kFieldSize : 8;
    // uint32_t const kLargeOffset = kStepSize - kLargeMatch;
    // uint32_t const kSmallOffset = kStepSize - kSmallMatch;

    ZL_ASSERT(ZL_isPow2(kFieldSize));
    int const kFieldBits = ZL_highbit32(kFieldSize);

    ZL_ASSERT_LT(kFieldSize, 32);
    uint32_t fieldHistogram[32];
    memset(fieldHistogram, 0, sizeof(fieldHistogram));
    uint32_t log2Histogram[32];
    memset(log2Histogram, 0, sizeof(log2Histogram));

#define kNumRep 2
    uint32_t rep[kNumRep] = { kFieldSize, 2 * kFieldSize };

    ip += rep[kNumRep - 1];

    ZS_HashCache hc;
    ZS_HashCache_init(
            &hc,
            &smallTable,
            &largeTable,
            base,
            ip,
            iend,
            kFieldSize,
            kUseLarge);

    while (ip < ilimit) {
        ZS_sequence seq;
        uint8_t const* match = NULL;

        if (1) {
            if ((0)) {
                for (int r = 0; r < kNumRep; ++r) {
                    bool isRepMatch =
                            ZS_checkMatch(ip, ip - rep[r], kSmallMatch);
                    if (isRepMatch) {
                        match             = ip - rep[r];
                        seq.matchType     = ZS_mt_rep;
                        seq.matchCode     = (uint32_t)r;
                        seq.literalLength = (uint32_t)(ip - anchor);
                        seq.matchLength   = (uint32_t)ZS_countFields(
                                ip, ip - rep[r], iend, kFieldSize);
                        goto match_found;
                    }
                }
            }
            do {
                ZS_Hashes const hashes = ZS_HashCache_update(
                        &hc,
                        &smallTable,
                        &largeTable,
                        base,
                        ip,
                        iend,
                        kFieldSize,
                        kUseLarge);
                uint8_t const* const ip1 = ip + 0;
                for (int r = 0; r < kNumRep; ++r) {
                    bool isRepMatch =
                            ZS_checkMatch(ip1, ip1 - rep[r], kSmallMatch);
                    if (isRepMatch) {
                        match             = ip1 - rep[r];
                        seq.matchType     = ZS_mt_rep;
                        seq.matchCode     = (uint32_t)r;
                        seq.literalLength = (uint32_t)(ip1 - anchor);
                        seq.matchLength   = (uint32_t)ZS_countFields(
                                ip1, ip1 - rep[r], iend, kFieldSize);
                        goto match_found;
                    }
                }

                bool const matchFound = findMatchDFast(
                        &seq,
                        hashes,
                        &match,
                        &smallTable,
                        &largeTable,
                        base,
                        anchor,
                        ip,
                        iend,
                        lowLimit,
                        kFieldSize,
                        kUseLarge);
                if (matchFound)
                    goto match_found;

                ip += kFieldSize;
            } while (ip < ilimit);
        }

        continue;
    match_found: {
        ip = anchor + seq.literalLength;

        ZL_ASSERT_GE(seq.matchLength, kFieldSize);
        ZL_ASSERT_GE(seq.matchLength, 4);
        ZL_ASSERT_EQ(seq.literalLength & kFieldMask, 0);
        ZL_ASSERT_EQ(seq.matchLength & kFieldMask, 0);
        ZL_ASSERT_EQ((size_t)(ip - match) & kFieldMask, 0);
        ZL_ASSERT_LT(match, ip);

        ZL_ASSERT(ZS_checkMatch(ip, match, kSmallMatch));
        ZL_ASSERT_EQ((size_t)(ip - anchor) & kFieldMask, 0);
        if (ip + seq.matchLength < ilimit) {
            ZL_ASSERT(!ZS_checkMatch(
                    ip + seq.matchLength,
                    match + seq.matchLength,
                    kSmallMatch));
        }
        {
            uint8_t const* const windowLimit = windowLow + kFieldSize - 1;
            while (ip > anchor && match > windowLimit
                   && ZS_checkMatch(
                           ip - kFieldSize, match - kFieldSize, kFieldSize)) {
                ip -= kFieldSize;
                match -= kFieldSize;
                seq.matchLength += kFieldSize;
                seq.literalLength -= kFieldSize;
            }
        }

        ZL_ASSERT_EQ(seq.literalLength & kFieldMask, 0);
        ZL_ASSERT_EQ(seq.matchLength & kFieldMask, 0);
        ZL_ASSERT_EQ((size_t)(ip - match) & kFieldMask, 0);

        // fieldHistogram[(uint32_t)(ip - match) & kFieldMask]++;
        // log2Histogram[ZL_highbit32((uint32_t)(ip - match))]++;
        // ZL_LOG(WARN, "SEQ: TY=%d OF=%u LL=%u ML=%u", seq.matchType,
        // (uint32_t)(ip - match), seq.literalLength, seq.matchLength);

        if (seq.matchType == ZS_mt_lz) {
            memmove(&rep[1], &rep[0], (kNumRep - 1) * 4);
            rep[0] = seq.matchCode << kFieldBits;
        } else if (seq.matchType == ZS_mt_rep && seq.matchCode != 0) {
            uint32_t const offset = rep[seq.matchCode];
            memmove(&rep[1], &rep[0], seq.matchCode * 4);
            rep[0] = offset;

            // HACK
            seq.matchCode = ZL_MIN(seq.matchCode, 1);
        }
        ZS_seqStore_store(seqs, anchor, iend, &seq);

        if (ip + seq.matchLength <= ilimit) {
            uint8_t const* const matchStartPtr = ip;
            uint32_t const matchStartIdx     = (uint32_t)(matchStartPtr - base);
            uint8_t const* const matchEndPtr = ip + seq.matchLength;
            uint32_t const matchEndIdx       = (uint32_t)(matchEndPtr - base);

            if (seq.matchLength > kFieldSize) {
                ZS_FastTable_putT(
                        &smallTable,
                        matchStartPtr + kFieldSize,
                        matchStartIdx + kFieldSize,
                        kSmallMatch);
                ZS_FastTable_putT(
                        &smallTable,
                        matchEndPtr - kFieldSize,
                        matchEndIdx - kFieldSize,
                        kSmallMatch);
                if (kUseLarge) {
                    ZS_FastTable_putT(
                            &largeTable,
                            matchStartPtr + kFieldSize,
                            matchStartIdx + kFieldSize,
                            kLargeMatch);
                    ZS_FastTable_putT(
                            &largeTable,
                            matchEndPtr - 2 * kFieldSize,
                            matchEndIdx - 2 * kFieldSize,
                            kLargeMatch);
                }
                ZS_HashCache_skip(
                        &hc,
                        &smallTable,
                        &largeTable,
                        base,
                        ip + seq.matchLength,
                        iend,
                        kFieldSize,
                        kUseLarge);
            }
        }

        anchor = ip = ip + seq.matchLength;
    }
    }
    ZL_ASSERT_LE(anchor, iend);
    ZS_seqStore_storeLastLiterals(seqs, anchor, (size_t)(iend - anchor));

    // for (uint32_t i = 0; i < kFieldSize; ++i) {
    //     ZL_LOG(WARN, "field-off[%u] = %u", i, fieldHistogram[i]);
    // }
    // for (uint32_t i = 0; i < 32; ++i) {
    //     if (log2Histogram[i] > 0)
    //         ZL_LOG(WARN, "log2-off[%u] = %u", i, log2Histogram[i]);
    // }
}

static void ZS_tokenLzMatchFinder_parseAny(
        ZS_matchFinderCtx* baseCtx,
        ZS_seqStore* seqs,
        uint8_t const* src,
        size_t size,
        uint32_t const fieldSize,
        bool useLarge)
{
    ZS_tokenLzMatchFinder_parseT(baseCtx, seqs, src, size, fieldSize, useLarge);
}

#define ZS_TOKEN_LZ_MATCH_FINDER_PARSE(kFieldSize, kUseLarge)           \
    static void ZS_tokenLzMatchFinder_parse_##kFieldSize##_##kUseLarge( \
            ZS_matchFinderCtx* baseCtx,                                 \
            ZS_seqStore* seqs,                                          \
            uint8_t const* src,                                         \
            size_t size)                                                \
    {                                                                   \
        ZS_tokenLzMatchFinder_parseT(                                   \
                baseCtx, seqs, src, size, kFieldSize, kUseLarge);       \
    }

ZS_TOKEN_LZ_MATCH_FINDER_PARSE(1, true)
ZS_TOKEN_LZ_MATCH_FINDER_PARSE(2, true)
ZS_TOKEN_LZ_MATCH_FINDER_PARSE(4, true)
ZS_TOKEN_LZ_MATCH_FINDER_PARSE(8, true)
ZS_TOKEN_LZ_MATCH_FINDER_PARSE(1, false)
ZS_TOKEN_LZ_MATCH_FINDER_PARSE(2, false)
ZS_TOKEN_LZ_MATCH_FINDER_PARSE(4, false)
ZS_TOKEN_LZ_MATCH_FINDER_PARSE(8, false)

static void ZS_tokenLzMatchFinder_parse(
        ZS_matchFinderCtx* baseCtx,
        ZS_seqStore* seqs,
        uint8_t const* src,
        size_t size)
{
    ZS_tokenLzCtx* const ctx = ZL_CONTAINER_OF(baseCtx, ZS_tokenLzCtx, base);
    ZL_ASSERT_NN(ctx);
    uint32_t const fieldSize = ctx->params.fieldSize;
    if (ctx->params.lzLargeMatch) {
        switch (fieldSize) {
            default:
                ZS_tokenLzMatchFinder_parseAny(
                        baseCtx, seqs, src, size, fieldSize, true);
                break;
            case 1:
                ZS_tokenLzMatchFinder_parse_1_true(baseCtx, seqs, src, size);
                break;
            case 2:
                ZS_tokenLzMatchFinder_parse_2_true(baseCtx, seqs, src, size);
                break;
            case 4:
                ZS_tokenLzMatchFinder_parse_4_true(baseCtx, seqs, src, size);
                break;
            case 8:
                ZS_tokenLzMatchFinder_parse_8_true(baseCtx, seqs, src, size);
                break;
        }
    } else {
        switch (fieldSize) {
            default:
                ZS_tokenLzMatchFinder_parseAny(
                        baseCtx, seqs, src, size, fieldSize, false);
                break;
            case 1:
                ZS_tokenLzMatchFinder_parse_1_false(baseCtx, seqs, src, size);
                break;
            case 2:
                ZS_tokenLzMatchFinder_parse_2_false(baseCtx, seqs, src, size);
                break;
            case 4:
                ZS_tokenLzMatchFinder_parse_4_false(baseCtx, seqs, src, size);
                break;
            case 8:
                ZS_tokenLzMatchFinder_parse_8_false(baseCtx, seqs, src, size);
                break;
        }
    }
}

const ZS_matchFinder ZS_tokenLzMatchFinder = {
    .name       = "tokenLz",
    .ctx_create = ZS_tokenLzMatchFinderCtx_create,
    .parse      = ZS_tokenLzMatchFinder_parse,
};
