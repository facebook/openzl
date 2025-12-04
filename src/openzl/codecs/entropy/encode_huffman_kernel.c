// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/entropy/encode_huffman_kernel.h"

#include <stddef.h> // size_t
#include <string.h> // memset

#include "openzl/codecs/entropy/common_huffman_kernel.h"
#include "openzl/codecs/entropy/deprecated/common_entropy.h"
#include "openzl/fse/bitstream.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"
#include "openzl/zl_errors.h"

#define ZS_kLargeHuffmanMaxTableLog 20
#define ZS_kLargeHuffmanMaxSymbolValue ((1u << 16) - 1)
#define ZS_kLargeHuffmanMaxRank \
    31 /* 2^32 - 2 is the maximum allowed histogram value */

typedef struct {
    // TODO: count & parent could share 1 element, they aren't used at the same
    // time.
    uint32_t count;
    uint32_t parent;
    uint16_t symbol;
    uint16_t nbBits;
} ZS_NodeElt;

typedef struct {
    int base;
    int curr;
} ZS_RankPos;

static int ZS_largeHuffmanSetMaxHeight(
        ZS_NodeElt* huffNode,
        int lastNonNull,
        int const maxNbBits)
{
    int const largestBits = huffNode[lastNonNull].nbBits;
    if (largestBits <= maxNbBits) {
        return (int)largestBits;
    }

    int totalCost      = 0;
    int const baseCost = 1 << (largestBits - maxNbBits);
    int node;
    for (node = lastNonNull; huffNode[node].nbBits > maxNbBits; --node) {
        totalCost += baseCost - (1 << (largestBits - huffNode[node].nbBits));
        huffNode[node].nbBits = (uint16_t)maxNbBits;
    }
    while (huffNode[node].nbBits == maxNbBits) {
        --node;
    }

    totalCost >>= (largestBits - maxNbBits);

    /* repay normalized cost */
    uint32_t const kNoSymbol = 0xF0F0F0F0;
    uint32_t rankLast[ZS_kLargeHuffmanMaxTableLog + 2];
    /* Get pos of last (smallest) symbol per rank */
    memset(rankLast, 0xF0, sizeof(rankLast));
    {
        int currentNbBits = maxNbBits;
        int pos;
        for (pos = node; pos >= 0; pos--) {
            if (huffNode[pos].nbBits >= currentNbBits) {
                continue;
            }
            currentNbBits = huffNode[pos].nbBits; /* < maxNbBits */
            rankLast[maxNbBits - currentNbBits] = (U32)pos;
        }
    }
    while (totalCost > 0) {
        int nBitsToDecrease = (int)ZL_highbit32((uint32_t)totalCost) + 1;
        while (nBitsToDecrease > 1 && rankLast[nBitsToDecrease] == kNoSymbol) {
            --nBitsToDecrease;
        }
        for (; nBitsToDecrease > 1; nBitsToDecrease--) {
            uint32_t const highPos = rankLast[nBitsToDecrease];
            uint32_t const lowPos  = rankLast[nBitsToDecrease - 1];
            assert(highPos != kNoSymbol);
            if (lowPos == kNoSymbol) {
                break;
            }
            uint32_t const highTotal = huffNode[highPos].count;
            uint32_t const lowTotal  = 2 * huffNode[lowPos].count;
            if (highTotal <= lowTotal) {
                break;
            }
        }
        /* only triggered when no more rank 1 symbol left => find closest one
         * (note : there is necessarily at least one !) */
        assert(rankLast[nBitsToDecrease] != kNoSymbol || nBitsToDecrease == 1);
        /* HUF_MAX_TABLELOG test just to please gcc 5+; but it should not be
         * necessary */
        while ((nBitsToDecrease <= ZS_kLargeHuffmanMaxTableLog)
               && (rankLast[nBitsToDecrease] == kNoSymbol)) {
            nBitsToDecrease++;
        }
        assert(rankLast[nBitsToDecrease] != kNoSymbol);
        totalCost -= 1 << (nBitsToDecrease - 1);
        // this rank is no longer empty
        if (rankLast[nBitsToDecrease - 1] == kNoSymbol) {
            rankLast[nBitsToDecrease - 1] = rankLast[nBitsToDecrease];
        }
        huffNode[rankLast[nBitsToDecrease]].nbBits++;
        // special case, reached largest symbol
        if (rankLast[nBitsToDecrease] == 0) {
            rankLast[nBitsToDecrease] = kNoSymbol;
        } else {
            rankLast[nBitsToDecrease]--;
            /* this rank is now empty */
            if (huffNode[rankLast[nBitsToDecrease]].nbBits
                != maxNbBits - nBitsToDecrease) {
                rankLast[nBitsToDecrease] = kNoSymbol;
            }
        }
    } /* while (totalCost > 0) */
    while (totalCost < 0) { /* Sometimes, cost correction overshoot */
        if (rankLast[1]
            == kNoSymbol) { /* special case : no rank 1 symbol (using
                              maxNbBits-1); let's create one from
                              largest rank 0 (using maxNbBits) */
            while (huffNode[node].nbBits == maxNbBits) {
                node--;
            }
            huffNode[node + 1].nbBits--;
            assert(node >= 0);
            rankLast[1] = (U32)(node + 1);
            totalCost++;
            continue;
        }
        huffNode[rankLast[1] + 1].nbBits--;
        rankLast[1]++;
        totalCost++;
    }
    return maxNbBits;
}

// static void ZS_largeHuffmanSort_small(ZS_NodeElt* bucket, int size)
// {
//   // TODO: Try swapping smallest to first position to avoid bounds check
//   for (int i = 1; i < size; ++i) {
//     ZS_NodeElt const tmp = bucket[i];
//     int j;
//     for (j = i - 1; j >= 0 && tmp.count > bucket[j].count; --j) {
//       bucket[j + 1] = bucket[j];
//     }
//     bucket[j + 1] = tmp;
//   }
// }

static void ZS_largeHuffmanSort_impl(
        ZS_NodeElt* restrict bucket,
        int size,
        int highbit,
        ZS_NodeElt* restrict scratch,
        ZS_RankPos* rankPosition)
{
    uint32_t const mask = (1u << highbit) - 1;

    //> Compute size of each rank
    memset(rankPosition, 0, sizeof(*rankPosition) * (size_t)(highbit + 1));
    for (int i = 0; i < size; ++i) {
        uint32_t const count = bucket[i].count & mask;
        int const rank       = count == 0 ? 0 : 1 + ZL_highbit32(count);
        rankPosition[rank].curr++;
    }
    //>> Compute the base of each rank by adding up the offsets.
    for (int r = highbit, curr = 0; r >= 0; --r) {
        int const next       = curr + rankPosition[r].curr;
        rankPosition[r].base = rankPosition[r].curr = curr;
        curr                                        = next;
        ZL_ASSERT_LE(curr, size);
    }
    //> Partition by rank
    for (int i = 0; i < size; ++i) {
        uint32_t const count = bucket[i].count & mask;
        int const rank       = count == 0 ? 0 : 1 + ZL_highbit32(count);
        int const pos        = rankPosition[rank].curr++;
        scratch[pos]         = bucket[i];
    }
    ZL_ASSERT_LE(bucket + size, scratch);
    memcpy(bucket, scratch, (size_t)size * sizeof(*bucket));
    //> Recurse into each rank (skip first two ranks (already sorted))
    for (int r = 2; r <= highbit; ++r) {
        int const base = rankPosition[r].base;
        int const curr = rankPosition[r].curr;
        ZL_ASSERT_EQ(rankPosition[r - 1].base, curr);
        int const rankSize = (int)(curr - base);
        int const hb       = r - 1;
        ZL_ASSERT_LE(base, curr);
        if (rankSize > 0) {
            ZS_largeHuffmanSort_impl(
                    bucket + base, rankSize, hb, scratch, rankPosition);
        }
    }
}

/**
 * ZS_largeHuffmanSort()
 * Sorts the symbols into huffNode in order of decreasing counts.
 * Builds up rankPosition that tells where each rank starts.
 */
static void ZS_largeHuffmanSort(
        ZS_NodeElt* huffNode,
        uint32_t const* histogram,
        uint16_t maxSymbolValue,
        ZS_RankPos* rankPosition)
{
    int const maxSymbolValue1 = (int)maxSymbolValue + 1;
    //> Insert into huffNode table.
    for (int s = 0; s < maxSymbolValue1; ++s) {
        huffNode[s].count  = histogram[s];
        huffNode[s].symbol = (uint16_t)s;
    }
    //> Sort huffNode table.
    ZS_largeHuffmanSort_impl(
            huffNode,
            maxSymbolValue1,
            ZS_kLargeHuffmanMaxRank,
            huffNode + maxSymbolValue1,
            rankPosition);

    // for (int s = 0; s < (int)maxSymbolValue; ++s) {
    //   ZL_LOG(ALWAYS, "s=%d in [0, %d)", s, maxSymbolValue1);
    //   ZL_ASSERT_GE(huffNode[s].count, huffNode[s+1].count);
    // }
}

static int ZS_largeHuffmanBuildTree(
        ZS_NodeElt* huffNode,
        uint16_t maxSymbolValue)
{
    int nonNullRank = (int)maxSymbolValue;

    while (huffNode[nonNullRank].count == 0) {
        --nonNullRank;
    }

    int const startNode = maxSymbolValue + 1;
    int nodeNb          = startNode;
    // singleton node with the lowest count
    int lowSingleton = nonNullRank;
    // node that is a root of a sub-tree with the lowest count (among sub-trees)
    int lowNode = nodeNb;
    // This will be the root node of the tree.
    int const nodeRoot = nodeNb + lowSingleton - 1;

    // Merge the lowest two weights and form the first internal node.
    huffNode[nodeNb].count =
            huffNode[lowSingleton].count + huffNode[lowSingleton - 1].count;
    huffNode[lowSingleton].parent = huffNode[lowSingleton - 1].parent =
            (uint32_t)nodeNb;
    nodeNb++;
    lowSingleton -= 2;

    // Fake the count values.
    for (int n = nodeNb; n <= nodeRoot; ++n) {
        huffNode[n].count = 1u << 30;
    }
    // Set a sentinal value.
    // TODO: Could this be nodeNb - 1 instead of -1?
    huffNode[-1].count = 1u << 31;

    for (; nodeNb <= nodeRoot; ++nodeNb) {
        int const n1 = (huffNode[lowSingleton].count < huffNode[lowNode].count)
                ? lowSingleton--
                : lowNode++;
        int const n2 = (huffNode[lowSingleton].count < huffNode[lowNode].count)
                ? lowSingleton--
                : lowNode++;
        huffNode[nodeNb].count = huffNode[n1].count + huffNode[n2].count;
        huffNode[n1].parent = huffNode[n2].parent = (uint32_t)nodeNb;
    }

    // Distribute the weights
    // TODO: Why is 2 passes enough?
    huffNode[nodeRoot].nbBits = 0;
    for (int n = nodeRoot - 1; n >= startNode; --n) {
        huffNode[n].nbBits =
                (uint16_t)(huffNode[huffNode[n].parent].nbBits + 1);
    }
    for (int n = 0; n <= nonNullRank; ++n) {
        huffNode[n].nbBits =
                (uint16_t)(huffNode[huffNode[n].parent].nbBits + 1);
    }

    return nonNullRank;
}

static ZL_Report ZS_largeHuffmanBuildCTableFromTree(
        ZS_Huf16CElt* ctable,
        ZS_NodeElt const* huffNode,
        int nonNullRank,
        uint16_t maxSymbolValue,
        int maxNbBits)
{
    int const alphabetSize = (int)(maxSymbolValue + 1);
    uint16_t* nbPerRank    = calloc((size_t)1 << maxNbBits, sizeof(uint16_t));
    ZL_RET_R_IF_NULL(allocation, nbPerRank);
    uint16_t* valPerRank = calloc((size_t)1 << maxNbBits, sizeof(uint16_t));
    if (valPerRank == NULL) {
        free(nbPerRank);
        ZL_RET_R_ERR(allocation);
    }

    for (int n = 0; n <= nonNullRank; n++) {
        nbPerRank[huffNode[n].nbBits]++;
    }
    /* determine stating value per rank */
    {
        uint16_t min = 0;
        for (int n = (int)maxNbBits; n > 0; n--) {
            valPerRank[n] = min; /* get starting value within each rank */
            min           = (uint16_t)(min + nbPerRank[n]);
            min >>= 1;
        }
    }
    for (int n = 0; n < alphabetSize; n++) {
        ctable[huffNode[n].symbol].nbBits =
                huffNode[n].nbBits; /* push nbBits per symbol, symbol order */
    }
    for (int n = 0; n < alphabetSize; n++) {
        ctable[n].symbol = valPerRank[ctable[n].nbBits]++;
    } /* assign value within
                                                  rank, symbol order */

    free(nbPerRank);
    free(valPerRank);
    return ZL_returnSuccess();
}

ZL_Report ZS_largeHuffmanBuildCTable(
        ZS_Huf16CElt* ctable,
        unsigned const* histogram,
        uint16_t maxSymbolValue,
        int maxNbBits)
{
    ZS_RankPos rankPosition[ZS_kLargeHuffmanMaxRank + 1];
    ZS_NodeElt* const huffNodeTable =
            calloc((size_t)(2 * maxSymbolValue + 3), sizeof(ZS_NodeElt));
    ZL_RET_R_IF_NULL(allocation, huffNodeTable);

    ZS_NodeElt* const huffNode = huffNodeTable + 1;

    if (maxNbBits == 0) {
        maxNbBits = ZS_kLargeHuffmanMaxTableLog;
    }

    // Reduce maxNbBits
    {
        int const alphabetSizeBound =
                ZL_nextPow2((uint64_t)(maxSymbolValue + 1));
        ZL_ASSERT_GT((size_t)1 << alphabetSizeBound, maxSymbolValue);
        maxNbBits = ZL_MIN(maxNbBits, alphabetSizeBound + 3);
    }

    ZS_largeHuffmanSort(huffNode, histogram, maxSymbolValue, rankPosition);
    int const nonNullRank = ZS_largeHuffmanBuildTree(huffNode, maxSymbolValue);

    maxNbBits = ZS_largeHuffmanSetMaxHeight(huffNode, nonNullRank, maxNbBits);
    if (maxNbBits > ZS_kLargeHuffmanMaxTableLog) {
        free(huffNodeTable);
        ZL_RET_R_ERR(GENERIC);
    }

    const ZL_Report report = ZS_largeHuffmanBuildCTableFromTree(
            ctable, huffNode, nonNullRank, maxSymbolValue, maxNbBits);
    free(huffNodeTable);
    ZL_RET_R_IF_ERR(report);

    return ZL_returnValue((size_t)maxNbBits);
}

ZL_Report ZS_largeHuffmanWriteCTable(
        ZL_WC* dst,
        ZS_Huf16CElt const* ctable,
        uint16_t maxSymbolValue,
        int maxNbBits)
{
    int const maxSymbolValue1 = (int)maxSymbolValue + 1;
    uint8_t* const weights    = malloc((size_t)(maxSymbolValue + 1));
    ZL_RET_R_IF_NULL(allocation, weights);

    for (int s = 0; s < maxSymbolValue1; ++s) {
        ZL_ASSERT_LE(ctable[s].nbBits, maxNbBits);
        if (ctable[s].nbBits == 0) {
            weights[s] = 0;
        } else {
            weights[s] = (uint8_t)(maxNbBits + 1 - ctable[s].nbBits);
        }
        ZL_ASSERT_LE(weights[s], maxNbBits);
    }

    if (ZL_WC_avail(dst) < 7) {
        free(weights);
        ZL_RET_R_ERR(GENERIC);
    }
    ZL_WC_push(dst, (uint8_t)maxNbBits);
    ZL_WC_pushCE16(dst, maxSymbolValue);
    if (ZL_isError(ZS_Entropy_encodeFse(
                dst, weights, (size_t)maxSymbolValue1, 1, 2))) {
        free(weights);
        ZL_RET_R_ERR(GENERIC);
    }

    free(weights);

    return ZL_returnSuccess();
}

ZL_FORCE_INLINE void ZS_largeHuffmanEncodeUsingCTable_body(
        BIT_CStream_t* cstream,
        uint16_t const* src,
        size_t size,
        ZS_Huf16CElt const* ctable,
        size_t const kUnroll)
{
    size_t s = size - 1;
    if (size > kUnroll) {
        for (; s >= kUnroll; s -= kUnroll) {
            for (size_t u = 0; u < kUnroll; ++u) {
                ZS_Huf16CElt const elt = ctable[src[s - u]];
                BIT_addBits(cstream, elt.symbol, elt.nbBits);
            }
            BIT_flushBits(cstream);
        }
    }
    for (size_t r = 0; r <= s; ++r) {
        ZS_Huf16CElt const elt = ctable[src[s - r]];
        BIT_addBits(cstream, elt.symbol, elt.nbBits);
        BIT_flushBits(cstream);
    }
}

ZL_Report ZS_largeHuffmanEncodeUsingCTable(
        ZL_WC* dst,
        uint16_t const* src,
        size_t const size,
        ZS_Huf16CElt const* ctable,
        int maxNbBits)
{
    if (ZL_WC_avail(dst) < 8) {
        ZL_RET_R_ERR(GENERIC);
    }
    ZL_WC_pushCE32(dst, (uint32_t)size);
    uint8_t* const sizePtr = ZL_WC_ptr(dst);
    ZL_WC_advance(dst, sizeof(uint32_t));
    //> Compress
    BIT_CStream_t cstream;
    if (ERR_isError(
                BIT_initCStream(&cstream, ZL_WC_ptr(dst), ZL_WC_avail(dst)))) {
        ZL_RET_R_ERR(GENERIC);
    }

    if (ZL_64bits()) {
        if (maxNbBits <= 14) {
            ZS_largeHuffmanEncodeUsingCTable_body(
                    &cstream, src, size, ctable, 4);
        } else if (maxNbBits <= 18) {
            ZS_largeHuffmanEncodeUsingCTable_body(
                    &cstream, src, size, ctable, 3);
        } else {
            ZL_ASSERT_LE(maxNbBits, 28);
            ZS_largeHuffmanEncodeUsingCTable_body(
                    &cstream, src, size, ctable, 2);
        }
    } else {
        if (maxNbBits <= 14) {
            ZS_largeHuffmanEncodeUsingCTable_body(
                    &cstream, src, size, ctable, 2);
        } else {
            ZL_ASSERT_LE(maxNbBits, 28);
            ZS_largeHuffmanEncodeUsingCTable_body(
                    &cstream, src, size, ctable, 1);
        }
    }

    size_t const streamSize = BIT_closeCStream(&cstream);
    if (streamSize == 0) {
        ZL_RET_R_ERR(GENERIC);
    }
    ZL_WC_advance(dst, streamSize);

    ZL_writeCE32(sizePtr, (uint32_t)streamSize);
    return ZL_returnSuccess();
}

ZL_Report ZS_largeHuffmanEncodeUsingCTableX4(
        ZL_WC* dst,
        uint16_t const* src,
        size_t const size,
        ZS_Huf16CElt const* ctable,
        int maxNbBits)
{
    size_t off                = 0;
    size_t const maxChunkSize = size / 4 + 1;
    for (int i = 0; i < 4; ++i) {
        size_t const chunkSize = ZL_MIN(maxChunkSize, size - off);
        ZL_RET_R_IF_ERR(ZS_largeHuffmanEncodeUsingCTable(
                dst, src + off, chunkSize, ctable, maxNbBits));
        off += chunkSize;
    }
    ZL_ASSERT_EQ(off, size);
    return ZL_returnSuccess();
}

static ZL_Report
ZS_largeHuffmanUncompressed(ZL_WC* dst, uint16_t const* src, size_t size)
{
    if (ZL_WC_avail(dst) < 1 + ZL_varintSize((uint64_t)size) + 2 * size) {
        ZL_RET_R_ERR(GENERIC);
    }
    ZL_WC_push(dst, (uint8_t)ZS_HufTransformPrefix_lit);
    ZL_WC_pushVarint(dst, (uint64_t)size);
    for (size_t i = 0; i < size; ++i) {
        ZL_WC_pushCE16(dst, src[i]);
    }
    return ZL_returnSuccess();
}

ZL_Report ZS_largeHuffmanEncode(
        ZL_WC* dst,
        uint16_t const* src,
        size_t const size,
        uint16_t maxSymbolValue,
        int maxTableLog)
{
    if (maxTableLog > ZS_kLargeHuffmanMaxTableLog || maxTableLog == 0) {
        maxTableLog = ZS_kLargeHuffmanMaxTableLog;
    }
    if (size == 0) {
        return ZS_largeHuffmanUncompressed(dst, src, size);
    }

    ZL_WC cpy = *dst;
    if (ZL_WC_avail(dst) < 1) {
        ZL_RET_R_ERR(GENERIC);
    }

    uint8_t* const header = ZL_WC_ptr(dst);
    ZL_WC_advance(dst, 1);
    //> Histogram
    uint32_t* const histogram =
            calloc((size_t)(maxSymbolValue + 1), sizeof(uint32_t));
    ZL_RET_R_IF_NULL(allocation, histogram);
    {
        int maxSymbolValueUpperBound = 0;
        for (size_t i = 0; i < size; ++i) {
            uint16_t const s = ZL_read16(&src[i]);
            maxSymbolValueUpperBound |= s;
            ++histogram[s];
        }
        //> Start from an upper bound on MSV, in the common case wehre msv <<
        // 2^16
        maxSymbolValue =
                ZL_MIN((uint16_t)maxSymbolValueUpperBound, maxSymbolValue);
        while (histogram[maxSymbolValue] == 0) {
            --maxSymbolValue;
        }
    }
    ZL_ASSERT_NE(histogram[maxSymbolValue], 0);
    //> Handle edge cases
    if (histogram[maxSymbolValue] == size) {
        free(histogram);
        *header = (uint8_t)ZS_HufTransformPrefix_constant;
        if (ZL_WC_avail(dst)
            < ZL_varintSize((uint64_t)size) + sizeof(uint16_t)) {
            ZL_RET_R_ERR(GENERIC);
        }
        ZL_WC_pushVarint(dst, (uint64_t)size);
        ZL_WC_pushCE16(dst, maxSymbolValue);
        return ZL_returnSuccess();
    }
    //> Build CTable
    *header = (uint8_t)ZS_HufTransformPrefix_huf;
    ZS_Huf16CElt* const ctable =
            calloc((size_t)(maxSymbolValue + 1), sizeof(ZS_Huf16CElt));
    if (ctable == NULL) {
        free(histogram);
        ZL_RET_R_ERR(allocation);
    }
    ZL_Report const maxNbBitsReport = ZS_largeHuffmanBuildCTable(
            ctable, histogram, maxSymbolValue, maxTableLog);
    if (ZL_isError(maxNbBitsReport)) {
        free(histogram);
        free(ctable);
        ZL_RET_R(maxNbBitsReport);
    }
    int const maxNbBits = (int)ZL_validResult(maxNbBitsReport);

    if (ZL_WC_avail(dst) < 2 * sizeof(uint32_t)) {
        goto uncompressed;
    }
    //> Encode CTable
    if (ZL_isError(ZS_largeHuffmanWriteCTable(
                dst, ctable, maxSymbolValue, maxNbBits))) {
        goto uncompressed;
    }
    //> Compress
    if (ZL_WC_avail(dst) < 1) {
        goto uncompressed;
    }
    if (size > 1024) {
        ZL_WC_push(dst, 1);
        if (ZL_isError(ZS_largeHuffmanEncodeUsingCTableX4(
                    dst, src, size, ctable, maxNbBits))) {
            goto uncompressed;
        }
    } else {
        ZL_WC_push(dst, 0);
        if (ZL_isError(ZS_largeHuffmanEncodeUsingCTable(
                    dst, src, size, ctable, maxNbBits))) {
            goto uncompressed;
        }
    }

    if ((size_t)(ZL_WC_ptr(dst) - header) >= size * 2) {
        goto uncompressed;
    }

    ZL_Report ret = ZL_returnSuccess();
    goto out;

uncompressed:
    *dst = cpy;
    ret  = ZS_largeHuffmanUncompressed(dst, src, size);

out:
    free(histogram);
    free(ctable);

    return ret;
}
