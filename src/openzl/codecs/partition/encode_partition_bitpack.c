// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/partition/encode_partition_bitpack.h"

#include <stdint.h>
#include <string.h>

#include "openzl/zl_graph_api.h"

#include "openzl/codecs/partition/common_partition.h"
#include "openzl/codecs/zl_bitpack.h"
#include "openzl/codecs/zl_partition.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_localParams.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Add a small fixed cost per partition
#define PB_OVERHEAD_BITS 24
/// The maximum possible number of partitions
#define PB_MAX_PARTITIONS 256
/// Don't partition if the gain is less than this and just fallback to store
#define PB_MIN_GAIN_PCT 1

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Number of histogram buckets needed for values of the given bit width.
/// For u16 (valueBits=16): all buckets are linear, so 2^(16-4) = 4096.
/// For u32 (valueBits=32): 4096 linear buckets + 16 log-scale buckets = 4112.
static uint32_t PB_histCapacity(size_t valueBits)
{
    if (valueBits <= PB_LINEAR_THRESHOLD_LOG) {
        return (uint32_t)1 << (valueBits - PB_PRECISION_LOSS);
    }
    return PB_LINEAR_BUCKETS + (uint32_t)valueBits - PB_LINEAR_THRESHOLD_LOG;
}

/// Map a raw value to its histogram bucket index.
/// Below the threshold: uniform buckets of width 2^PB_PRECISION_LOSS.
/// Above: one bucket per power-of-two range [2^k, 2^(k+1)).
static uint32_t PB_idx2bucket(uint32_t idx)
{
    if (idx < PB_LINEAR_THRESHOLD) {
        return idx >> PB_PRECISION_LOSS;
    }
    return PB_LINEAR_BUCKETS + (uint32_t)ZL_highbit32(idx)
            - PB_LINEAR_THRESHOLD_LOG;
}

static uint32_t PB_readValue(const void* data, size_t i, size_t eltWidth)
{
    if (eltWidth == 2) {
        return ((const uint16_t*)data)[i];
    }
    ZL_ASSERT_EQ(eltWidth, 4);
    return ((const uint32_t*)data)[i];
}

/// Build cumulative value-space boundaries for each histogram bucket.
/// These map bucket indices back to actual values, so the DP optimizer can
/// compute partition sizes in value-space (not bucket-space). For linear
/// buckets this is uniform; for log buckets each spans a power-of-two range.
static void PB_buildCumBases(uint64_t* cumBases, size_t valueBits)
{
    const uint32_t histCapacity = PB_histCapacity(valueBits);
    const uint32_t linearBuckets =
            ZL_MIN(histCapacity, (uint32_t)PB_LINEAR_BUCKETS);
    for (size_t i = 0; i <= linearBuckets; ++i) {
        cumBases[i] = i << PB_PRECISION_LOSS;
    }
    for (size_t i = 1; linearBuckets + i <= histCapacity; ++i) {
        cumBases[linearBuckets + i] = 1ULL << (PB_LINEAR_THRESHOLD_LOG + i);
    }
}

static uint64_t PB_bucketSize(const uint64_t* cumBases, uint32_t b, uint32_t e)
{
    return cumBases[e] - cumBases[b];
}

typedef struct {
    /// Size: histSize + 1
    /// Last value is the total count from [0, histSize)
    const uint32_t* cumHist;
    /// Size: histSize + 1
    /// Cumulative value-space bucket boundaries.
    const uint64_t* cumBases;
    uint32_t histSize;
    uint64_t maxBucketSize;
} PB_CumHist;

// ---------------------------------------------------------------------------
// Cost function
// ---------------------------------------------------------------------------

/// @returns true iff the given partition is legal (i.e. the bucket size is
/// within the max allowed).
static bool PB_isLegalPartition(
        const uint64_t* cumBases,
        uint64_t maxBucketSize,
        uint32_t begin,
        uint32_t end)
{
    return begin < end && (cumBases[end] - cumBases[begin]) <= maxBucketSize;
}

/// Compute total bit cost for a set of partitions over a cumulative histogram.
/// It consists of the cost of the fixed bitpacked partition IDs, which take
/// ceil(log2(numPartitions)), plus the cost of the variable partition offsets,
/// each of which takes ceil(log2(partitionSizes[i])) for a value that falls
/// into partition i.
///
/// @param cumHist Cumulative histogram
/// @param partitions Array of partition start indices
/// @param numPartitions Number of partitions
/// @param totalPartitions Total partition count for bucket bits calculation
static uint64_t PB_fixedBucketCost(
        PB_CumHist cumHist,
        const uint32_t* partitions,
        size_t numPartitions,
        size_t totalPartitions)
{
    const uint32_t totalCount = cumHist.cumHist[cumHist.histSize];
    const uint32_t bucketBits = (uint32_t)ZL_nextPow2(totalPartitions);
    uint64_t cost             = (uint64_t)totalCount * bucketBits;
    for (size_t i = 0; i < numPartitions; ++i) {
        const uint32_t b = partitions[i];
        const uint32_t e =
                (i + 1 == numPartitions) ? cumHist.histSize : partitions[i + 1];
        ZL_ASSERT_LT(b, e);
        if (!PB_isLegalPartition(
                    cumHist.cumBases, cumHist.maxBucketSize, b, e)) {
            return UINT64_MAX;
        }
        const uint32_t count = cumHist.cumHist[e] - cumHist.cumHist[b];
        const uint32_t offBits =
                (uint32_t)ZL_nextPow2(PB_bucketSize(cumHist.cumBases, b, e));
        cost += PB_OVERHEAD_BITS + (uint64_t)count * offBits;
    }
    return cost;
}

// ---------------------------------------------------------------------------
// DP Partition (ported from utils/Partition.hpp 3rd overload)
// ---------------------------------------------------------------------------

/// Cost function context for the DP partition algorithm.
typedef struct {
    PB_CumHist ch;
    uint32_t fixedCost; // fixed cost for bucket bits
} PB_DPCostCtx;

/// Cost of a single bucket spanning [b, e) in the cumulative histogram.
static uint64_t PB_dpBucketCost(const PB_DPCostCtx* ctx, uint32_t b, uint32_t e)
{
    ZL_ASSERT_LT(b, e);
    ZL_ASSERT_LE(e, ctx->ch.histSize);
    if (!PB_isLegalPartition(ctx->ch.cumBases, ctx->ch.maxBucketSize, b, e)) {
        return UINT64_MAX;
    }
    const uint32_t count      = ctx->ch.cumHist[e] - ctx->ch.cumHist[b];
    const uint64_t bucketSize = PB_bucketSize(ctx->ch.cumBases, b, e);
    return (uint64_t)count
            * (ctx->fixedCost + (unsigned)ZL_nextPow2(bucketSize));
}

/// Compute the optimal partitions using dyanmic programming. Limits all but the
/// last partition sizes to be powers of two to reduce the runtime from N^2 * B
/// to N*log2(N) * B. This approximation has very little impact on the
/// optimallity, because optimal partitions on "well structured" inputs where
/// P[i+1] >= P[i] respect this condition.
///
/// @param numBuckets Maximum number of buckets
/// @param ctx Cost function context
/// @param outPartitions Output array (must hold numBuckets entries)
/// @param graph Graph context for scratch space allocation
/// @returns Actual number of partitions found
static size_t PB_dpPartition(
        size_t numBuckets,
        const PB_DPCostCtx* ctx,
        uint32_t* outPartitions,
        ZL_Graph* graph)
{
    const uint32_t N  = ctx->ch.histSize;
    const uint32_t B  = ZL_MIN((uint32_t)numBuckets, N);
    const uint32_t B1 = B + 1;
    const uint32_t N1 = N + 1;

    // Allocate DP tables from graph scratch space (arena, no free needed)
    const size_t optBytes   = (size_t)N1 * B1 * sizeof(uint64_t);
    const size_t beginBytes = (size_t)N1 * B1 * sizeof(uint32_t);
    uint64_t* opt   = (uint64_t*)ZL_Graph_getScratchSpace(graph, optBytes);
    uint32_t* begin = (uint32_t*)ZL_Graph_getScratchSpace(graph, beginBytes);
    if (!opt || !begin) {
        return 0;
    }

    // Initialize
    memset(opt, -1, optBytes);
    memset(begin, -1, beginBytes);

    opt[N1 * 0 + 0]   = 0;
    begin[N1 * 0 + 0] = 0;

    for (uint32_t e = 1; e < N1; ++e) {
        const size_t maxPossibleBuckets = ZL_MIN(e, B);
        for (uint32_t k = 1; k <= maxPossibleBuckets; ++k) {
            const uint32_t maxSz = e - (k - 1);
            for (uint32_t size = 1; size <= maxSz;
                 size          = (e == N) ? size + 1 : size * 2) {
                const uint32_t b = e - size;
                if (ctx->ch.cumBases[e] - ctx->ch.cumBases[b]
                    > ctx->ch.maxBucketSize) {
                    break;
                }
                const uint64_t prevCost = opt[N1 * (k - 1) + b];
                if (prevCost == UINT64_MAX) {
                    continue;
                }
                const uint64_t oldCost = opt[N1 * k + e];
                const uint64_t endCost = PB_dpBucketCost(ctx, b, e);
                if (endCost == UINT64_MAX) {
                    continue;
                }
                const uint64_t newCost = prevCost + endCost;
                if (newCost < oldCost) {
                    opt[N1 * k + e]   = newCost;
                    begin[N1 * k + e] = b;
                }
            }
        }
    }

    // Backtrack
    for (size_t e = N, pos = B; e > 0;) {
        uint32_t b = begin[N1 * pos-- + e];
        if (b == UINT32_MAX) {
            return 0;
        }
        outPartitions[pos] = b;
        e                  = b;
    }

    return B;
}

// ---------------------------------------------------------------------------
// Greedy Optimizer
// ---------------------------------------------------------------------------

typedef struct {
    PB_CumHist ch;
    /// number of prefix partitions
    size_t prefixSize;
    uint32_t* partitions; // working array
    size_t numPartitions;
    size_t targetPartitions;
    // Scratch buffer for grow/shrink
    uint32_t* scratch;
} PB_GreedyOpt;

/// @returns The cost of a single partition [b, e) excluding the global
/// bucketBits term (which is independent of the partition sizes).
static uint64_t
PB_singlePartitionCost(const PB_CumHist* ch, uint32_t b, uint32_t e)
{
    const uint32_t count = ch->cumHist[e] - ch->cumHist[b];
    const uint32_t offBits =
            (uint32_t)ZL_nextPow2(PB_bucketSize(ch->cumBases, b, e));
    return PB_OVERHEAD_BITS + (uint64_t)count * offBits;
}

/// @returns the end of partition @p i.
static uint32_t PB_partitionEnd(
        const uint32_t* partitions,
        size_t numPartitions,
        uint32_t histSize,
        size_t i)
{
    return (i + 1 == numPartitions) ? histSize : partitions[i + 1];
}

/// Grows partition @p idx by doubling its size, and rounding all following
/// partition sizes up to powers of two in @p out until a partition size is
/// unchanged.
/// @returns cascadeEnd: the first index NOT modified.
static size_t PB_growPartitions(
        const uint32_t* partitions,
        size_t idx,
        uint32_t* out,
        size_t n)
{
    if (idx + 1 == n) {
        return idx;
    }
    uint32_t begin   = partitions[idx];
    uint32_t end     = partitions[idx + 1];
    uint32_t sz      = end - begin;
    uint32_t newSize = 1u << ((unsigned)ZL_nextPow2(sz) + 1);
    out[idx + 1]     = begin + newSize;

    for (size_t j = idx + 1; j + 1 < n; ++j) {
        begin           = out[j];
        end             = partitions[j + 1];
        sz              = end > begin ? end - begin : 1;
        uint32_t alt    = 1u << (unsigned)ZL_nextPow2(sz);
        uint32_t newEnd = begin + ZL_MAX(newSize, alt);
        if (newEnd == end) {
            return j + 1;
        }
        out[j + 1] = newEnd;
    }
    return n;
}

/// Shrinks partition @p idx by halving its size, and rounding all following
/// partition sizes up to powers of two in @p out until a partition size is
/// unchanged.
/// @returns cascadeEnd: the first index NOT modified.
static size_t PB_shrinkPartitions(
        const uint32_t* partitions,
        size_t idx,
        uint32_t* out,
        size_t n)
{
    if (idx + 1 == n) {
        return idx;
    }
    uint32_t begin = partitions[idx];
    uint32_t end   = partitions[idx + 1];
    uint32_t sz    = end - begin;
    if (sz <= 1) {
        return idx;
    }
    uint32_t newSize = 1u << ((unsigned)ZL_nextPow2(sz) - 1);
    out[idx + 1]     = begin + newSize;

    for (size_t j = idx + 1; j + 1 < n; ++j) {
        begin = out[j];
        end   = partitions[j + 1];
        sz    = end - begin;
        if (!ZL_isPow2(sz)) {
            uint32_t newEnd = begin + (1u << ((unsigned)ZL_nextPow2(sz) - 1));
            if (newEnd == end) {
                return j + 1;
            }
            out[j + 1] = newEnd;
        } else {
            return j + 1;
        }
    }
    return n;
}

/// Compute cost of modified partitions [from, to), where out contains the
/// modified boundaries and partitions has the original values (used for the
/// end of partition to-1 if to < n, and partition from's begin is unchanged).
static uint64_t PB_modifiedRangeCost(
        const PB_CumHist* ch,
        const uint32_t* partitions,
        const uint32_t* out,
        size_t n,
        size_t from,
        size_t to)
{
    uint64_t cost = 0;
    for (size_t i = from; i < to; ++i) {
        const uint32_t b = (i == from) ? partitions[i] : out[i];
        uint32_t e;
        if (i + 1 == n) {
            e = ch->histSize;
        } else if (i + 1 < to) {
            e = out[i + 1];
        } else {
            e = partitions[i + 1];
        }
        if (e > ch->histSize
            || !PB_isLegalPartition(ch->cumBases, ch->maxBucketSize, b, e)) {
            return UINT64_MAX;
        }
        cost += PB_singlePartitionCost(ch, b, e);
    }
    return cost;
}

/// Try applying a cascaded mutation from scratch and accept if it reduces cost.
/// Returns true if the mutation was accepted.
static bool PB_tryMutation(
        PB_GreedyOpt* opt,
        uint64_t* partCost,
        uint64_t* currentCost,
        size_t n,
        size_t idx,
        size_t cascadeEnd)
{
    uint64_t newRangeCost = PB_modifiedRangeCost(
            &opt->ch, opt->partitions, opt->scratch, n, idx, cascadeEnd);
    if (newRangeCost == UINT64_MAX) {
        return false;
    }
    uint64_t oldRangeCost = 0;
    for (size_t j = idx; j < cascadeEnd; ++j) {
        oldRangeCost += partCost[j];
    }
    if (newRangeCost >= oldRangeCost) {
        return false;
    }
    for (size_t j = idx + 1; j < cascadeEnd && j < n; ++j) {
        opt->partitions[j] = opt->scratch[j];
    }
    *currentCost = *currentCost - oldRangeCost + newRangeCost;
    for (size_t j = idx; j < cascadeEnd; ++j) {
        const uint32_t b = opt->partitions[j];
        const uint32_t e =
                PB_partitionEnd(opt->partitions, n, opt->ch.histSize, j);
        partCost[j] = PB_singlePartitionCost(&opt->ch, b, e);
    }
    return true;
}

/**
 * Try to improve each partition boundry by either growing each partition to the
 * next power of two, or shrinking to the previous power of two. Repeat until a
 * local-minima is reached.
 *
 * See the "Iterative Improvement" section of
 * https://en.wikipedia.org/wiki/V-optimal_histograms
 */
static void PB_iterativeImprovement(PB_GreedyOpt* opt)
{
    const size_t n = opt->numPartitions;
    if (n < 2) {
        return;
    }

    uint64_t partCost[PB_MAX_PARTITIONS];
    uint64_t sumCost = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t b = opt->partitions[i];
        const uint32_t e =
                PB_partitionEnd(opt->partitions, n, opt->ch.histSize, i);
        if (!PB_isLegalPartition(
                    opt->ch.cumBases, opt->ch.maxBucketSize, b, e)) {
            return;
        }
        partCost[i] = PB_singlePartitionCost(&opt->ch, b, e);
        sumCost += partCost[i];
    }

    const uint32_t totalCount = opt->ch.cumHist[opt->ch.histSize];
    const uint32_t bucketBits = (uint32_t)ZL_nextPow2(opt->targetPartitions);
    const uint64_t baseCost   = (uint64_t)totalCount * bucketBits;
    uint64_t currentCost      = baseCost + sumCost;

    for (;;) {
        const uint64_t startCost = currentCost;
        for (size_t idx = 0; idx + 1 < n; ++idx) {
            size_t cascadeEnd =
                    PB_growPartitions(opt->partitions, idx, opt->scratch, n);
            if (PB_tryMutation(
                        opt, partCost, &currentCost, n, idx, cascadeEnd)) {
                continue;
            }
            cascadeEnd =
                    PB_shrinkPartitions(opt->partitions, idx, opt->scratch, n);
            PB_tryMutation(opt, partCost, &currentCost, n, idx, cascadeEnd);
        }
        if (currentCost == startCost) {
            break;
        }
    }
}

/**
 * Divides the @p i'th partition in two. The first sub-partition is the largest
 * power of two less than the current size, and the second is the remainder.
 */
static void
PB_dividePartitionAt(const PB_GreedyOpt* opt, uint32_t* out, size_t i)
{
    const uint32_t begin = opt->partitions[i];
    const uint32_t end   = PB_partitionEnd(
            opt->partitions, opt->numPartitions, opt->ch.histSize, i);
    const uint32_t sz      = end - begin;
    const uint32_t newSize = 1u << ((unsigned)ZL_nextPow2(sz) - 1);

    // Insert new partition at i+1
    if (out != opt->partitions) {
        memcpy(out, opt->partitions, opt->numPartitions * sizeof(uint32_t));
    }
    for (size_t j = opt->numPartitions; j > i + 1; --j) {
        out[j] = out[j - 1];
    }
    out[i + 1] = begin + newSize;
}

/// @returns the gain from splitting partition [begin, end) into two where the
/// first partition is the largest power of two smaller than the current size.
static int64_t PB_splitGain(const PB_CumHist* ch, uint32_t begin, uint32_t end)
{
    assert(end - begin > 1);
    const uint64_t oldCost   = PB_singlePartitionCost(ch, begin, end);
    const uint32_t sz        = end - begin;
    const uint32_t newSize   = 1u << ((unsigned)ZL_nextPow2(sz) - 1);
    const uint32_t mid       = begin + newSize;
    const uint64_t leftCost  = PB_singlePartitionCost(ch, begin, mid);
    const uint64_t rightCost = PB_singlePartitionCost(ch, mid, end);
    return (int64_t)oldCost - (int64_t)(leftCost + rightCost);
}

static int64_t
PB_partitionSplitGain(const PB_GreedyOpt* opt, uint32_t begin, uint32_t end)
{
    if (!PB_isLegalPartition(
                opt->ch.cumBases, opt->ch.maxBucketSize, begin, end)) {
        return INT64_MAX;
    }
    if (end - begin <= 1) {
        return INT64_MIN;
    }
    return PB_splitGain(&opt->ch, begin, end);
}

static bool PB_partitionsAreLegal(
        const uint32_t* partitions,
        size_t numPartitions,
        const PB_CumHist* ch)
{
    for (size_t i = 0; i < numPartitions; ++i) {
        const uint32_t b = partitions[i];
        const uint32_t e =
                PB_partitionEnd(partitions, numPartitions, ch->histSize, i);
        if (!PB_isLegalPartition(ch->cumBases, ch->maxBucketSize, b, e)) {
            return false;
        }
    }
    return true;
}

/**
 * Greedily divide the partition that is either illegal or provides the biggest
 * gain from division until we hit @p targetPartitions.
 * Run in O(targetPartitions^2) time.
 */
static void PB_dividePartitions(PB_GreedyOpt* opt, size_t targetPartitions)
{
    // Cache split gains to avoid O(N^2) rescanning.
    // gains[i] = gain from splitting partition i; INT32_MIN = unsplittable.
    int64_t gains[PB_MAX_PARTITIONS];
    {
        const size_t n     = opt->numPartitions;
        const size_t start = opt->prefixSize > 0 ? opt->prefixSize - 1 : 0;
        for (size_t i = 0; i < start; ++i) {
            gains[i] = INT64_MIN;
        }
        for (size_t i = start; i < n; ++i) {
            const uint32_t b = opt->partitions[i];
            const uint32_t e =
                    PB_partitionEnd(opt->partitions, n, opt->ch.histSize, i);
            gains[i] = PB_partitionSplitGain(opt, b, e);
        }
    }

    while (opt->numPartitions < targetPartitions) {
        const size_t n   = opt->numPartitions;
        int64_t bestGain = INT64_MIN;
        size_t bestIdx   = (size_t)-1;
        for (size_t i = 0; i < n; ++i) {
            if (gains[i] == INT64_MAX) {
                // Illegal partition - must split immediately
                bestIdx = i;
                break;
            }
            if (gains[i] > bestGain) {
                bestGain = gains[i];
                bestIdx  = i;
            }
        }
        if (bestIdx == (size_t)-1) {
            break;
        }
        const uint32_t bestBegin = opt->partitions[bestIdx];
        const uint32_t bestEnd =
                PB_partitionEnd(opt->partitions, n, opt->ch.histSize, bestIdx);
        if (bestEnd - bestBegin <= 1) {
            break;
        }

        PB_dividePartitionAt(opt, opt->partitions, bestIdx);
        ++opt->numPartitions;

        // Shift gains right for indices after bestIdx
        for (size_t j = opt->numPartitions - 1; j > bestIdx + 1; --j) {
            gains[j] = gains[j - 1];
        }

        // Recompute gains for the two new partitions at bestIdx and bestIdx+1
        for (size_t j = bestIdx; j <= bestIdx + 1 && j < opt->numPartitions;
             ++j) {
            const uint32_t b = opt->partitions[j];
            const uint32_t e = PB_partitionEnd(
                    opt->partitions, opt->numPartitions, opt->ch.histSize, j);
            gains[j] = PB_partitionSplitGain(opt, b, e);
        }
    }
}

/// Run the greedy optimizer.
/// @param cumHist Cumulative histogram
/// @param cumHistSize Size of cumHist
/// @param prefixPartitions Prefix partitions from DP (may be empty)
/// @param prefixSize Number of prefix partitions
/// @param targetPartitions Target number of partitions
/// @param outPartitions Output array (must hold targetPartitions entries)
/// @returns Actual number of partitions
static size_t PB_greedyOptimize(
        PB_CumHist cumHist,
        const uint32_t* prefixPartitions,
        size_t prefixSize,
        size_t targetPartitions,
        uint32_t* outPartitions)
{
    // Stack-allocated scratch (targetPartitions <= PB_MAX_PARTITIONS = 256)
    uint32_t scratch[PB_MAX_PARTITIONS + 1];

    PB_GreedyOpt opt;
    opt.ch               = cumHist;
    opt.targetPartitions = targetPartitions;
    opt.partitions       = outPartitions;
    opt.scratch          = scratch;

    // Initialize from prefix
    if (prefixSize == 0) {
        outPartitions[0]  = 0;
        opt.numPartitions = 1;
        opt.prefixSize    = 1;
    } else {
        memcpy(outPartitions, prefixPartitions, prefixSize * sizeof(uint32_t));
        opt.numPartitions = prefixSize;
        opt.prefixSize    = prefixSize;
    }

    // Greedily divide the current partitions until we have targetPartitions or
    // can't divide further.
    PB_dividePartitions(&opt, targetPartitions);

    // If there are at least 10K elements in the histogram, run the iterative
    // improvement pass. Otherwise, it is too expensive.
    if (cumHist.cumHist[cumHist.histSize] >= 10000) {
        // Iteratively improve the partition boundries one at a time until we
        // reach a local minima.
        PB_iterativeImprovement(&opt);
    }

    if (!PB_partitionsAreLegal(opt.partitions, opt.numPartitions, &opt.ch)) {
        return 0;
    }

    return opt.numPartitions;
}

// ---------------------------------------------------------------------------
// fixedPartitionFast (inner, over cumulative histogram)
// ---------------------------------------------------------------------------

/// Compute good partitions for a cumulative histogram, attempting to minimize
/// the encoded size of the partition+bitpack graph.
///
/// @param cumHist Cumulative histogram, cumHistSize entries
/// @param cumHistSize Number of entries in cumHist
/// @param numPartitions Target number of partitions
/// @param outPartitions Output array (must hold numPartitions entries)
/// @param graph Graph context for scratch space allocation
/// @returns Actual number of partitions
static size_t PB_fixedPartitionFastInner(
        PB_CumHist cumHist,
        size_t numPartitions,
        uint32_t* outPartitions,
        bool optimal,
        ZL_Graph* graph)
{
    if (!optimal) {
        return PB_greedyOptimize(
                cumHist, NULL, 0, numPartitions, outPartitions);
    }

    PB_DPCostCtx ctx;
    ctx.ch        = cumHist;
    ctx.fixedCost = (unsigned)ZL_nextPow2(numPartitions);
    return PB_dpPartition(numPartitions, &ctx, outPartitions, graph);
}

// ---------------------------------------------------------------------------
// fixedPartitionFast (outer, over raw numeric data)
// ---------------------------------------------------------------------------

typedef struct {
    uint64_t* partitions;
    size_t numPartitions;
    uint64_t bitCost;
    uint64_t maxSymbolValue;
} PB_PartitionResult;

static bool PB_buildCumHist(
        PB_CumHist* out,
        const void* data,
        size_t numElts,
        size_t eltWidth,
        bool optimal,
        ZL_Graph* graph)
{
    const size_t valueBits      = eltWidth * 8;
    const uint32_t histCapacity = PB_histCapacity(valueBits);
    uint32_t* const hist        = (uint32_t*)ZL_Graph_getScratchSpace(
            graph, (histCapacity + 1) * sizeof(uint32_t));
    if (hist == NULL) {
        return false;
    }
    uint64_t* const cumBases = (uint64_t*)ZL_Graph_getScratchSpace(
            graph, (histCapacity + 1) * sizeof(uint64_t));
    if (cumBases == NULL) {
        return false;
    }
    PB_buildCumBases(cumBases, valueBits);

    const uint32_t skip = optimal ? 1 : 3;

    uint32_t minBucket = histCapacity - 1;
    uint32_t maxBucket = 0;
    memset(hist, 0, histCapacity * sizeof(uint32_t));
    for (size_t i = 0; i < numElts; i += skip) {
        const uint32_t bucket = PB_idx2bucket(PB_readValue(data, i, eltWidth));
        ++hist[bucket];
    }

    if (skip > 1 && numElts > 0) {
        // Since we are skipping data while building the histogram, add dummy
        // entries at the actual observed range boundaries.
        for (size_t i = 0; i < numElts; ++i) {
            const uint32_t bucket =
                    PB_idx2bucket(PB_readValue(data, i, eltWidth));
            minBucket = ZL_MIN(minBucket, bucket);
            maxBucket = ZL_MAX(maxBucket, bucket);
        }
        hist[minBucket] += (hist[minBucket] == 0);
        hist[maxBucket] += (hist[maxBucket] == 0);
    }

    uint32_t histSize = histCapacity;
    while (histSize > 0 && hist[histSize - 1] == 0) {
        --histSize;
    }

    // Build cumulative histogram
    uint32_t sum = 0;
    for (size_t i = 0; i < histSize; ++i) {
        // Need to multiply by skip so that our estimated cost at the end is
        // accurate.
        const uint32_t count = hist[i] * skip;
        hist[i]              = sum;
        sum += count;
    }
    hist[histSize] = sum;

    out->cumHist       = hist;
    out->cumBases      = cumBases;
    out->histSize      = histSize;
    out->maxBucketSize = (eltWidth == 2)
            ? ZL_PARTITION_MAX_PARTITION_SIZE_FOR_UNROLL4
            : ZL_PARTITION_MAX_PARTITION_SIZE_FOR_UNROLL2;

    return true;
}

static bool PB_buildCumHistU16(
        PB_CumHist* out,
        const void* data,
        size_t numElts,
        bool optimal,
        ZL_Graph* graph)
{
    return PB_buildCumHist(
            out, data, numElts, sizeof(uint16_t), optimal, graph);
}

static bool PB_buildCumHistU32(
        PB_CumHist* out,
        const void* data,
        size_t numElts,
        bool optimal,
        ZL_Graph* graph)
{
    return PB_buildCumHist(
            out, data, numElts, sizeof(uint32_t), optimal, graph);
}

/// Compute optimal partition boundaries for numeric data.
/// All allocations use stack or graph scratch space (no malloc/free).
static PB_PartitionResult PB_fixedPartition(
        const void* data,
        size_t numElts,
        size_t eltWidth,
        bool optimal,
        ZL_Graph* graph)
{
    PB_PartitionResult result = { NULL, 0, 0, 0 };

    PB_CumHist cumHist;
    if (eltWidth == 2) {
        if (!PB_buildCumHistU16(&cumHist, data, numElts, optimal, graph)) {
            return result;
        }
    } else {
        assert(eltWidth == 4);
        if (!PB_buildCumHistU32(&cumHist, data, numElts, optimal, graph)) {
            return result;
        }
    }

    // Try 16 and 32 partitions, keep best
    uint32_t bestBuf[PB_MAX_PARTITIONS];
    size_t bestSize   = 0;
    uint64_t bestCost = UINT64_MAX;

    uint32_t trialBuf[PB_MAX_PARTITIONS];
    const size_t trialCounts[] = { 16, 32 };
    for (size_t t = 0; t < 2; ++t) {
        size_t numP   = trialCounts[t];
        size_t actual = PB_fixedPartitionFastInner(
                cumHist, numP, trialBuf, optimal, graph);
        if (actual == 0) {
            continue;
        }
        uint64_t cost = PB_fixedBucketCost(cumHist, trialBuf, actual, actual);
        if (cost == UINT64_MAX) {
            continue;
        }
        // Give a small bias towards fewer partitions because it is faster to
        // decode with 16 partitions than 32 partitions.
        if (cost + (cost / 64) < bestCost) {
            bestCost = cost;
            bestSize = actual;
            memcpy(bestBuf, trialBuf, actual * sizeof(uint32_t));
        } else {
            break;
        }
    }

    if (bestSize == 0) {
        return result;
    }

    // Store partition boundaries in scratch space
    result.partitions = (uint64_t*)ZL_Graph_getScratchSpace(
            graph, bestSize * sizeof(uint64_t));
    if (!result.partitions) {
        return result;
    }
    for (size_t i = 0; i < bestSize; ++i) {
        result.partitions[i] = cumHist.cumBases[bestBuf[i]];
    }
    result.numPartitions  = bestSize;
    result.bitCost        = bestCost;
    result.maxSymbolValue = cumHist.cumBases[cumHist.histSize] - 1;

    return result;
}

// ---------------------------------------------------------------------------
// Dynamic graph function
// ---------------------------------------------------------------------------

ZL_Report
EI_partitionBitpackDynGraph(ZL_Graph* graph, ZL_Edge** inputs, size_t numInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    ZL_ASSERT_EQ(numInputs, 1);
    ZL_ASSERT_NN(inputs);
    ZL_ASSERT_NN(graph);

    ZL_Edge* inputEdge    = inputs[0];
    const ZL_Input* input = ZL_Edge_getData(inputEdge);
    const size_t numElts  = ZL_Input_numElts(input);
    const size_t eltWidth = ZL_Input_eltWidth(input);

    ZL_ERR_IF(
            eltWidth != 2 && eltWidth != 4,
            node_invalid_input,
            "Only 2-byte and 4-byte numeric values are accepted");

    const ZL_IntParam optimalParam = ZL_Graph_getLocalIntParam(
            graph, ZL_GRAPH_PARTITION_BITPACK_OPTIMAL_PID);
    const bool optimal =
            (optimalParam.paramId == ZL_GRAPH_PARTITION_BITPACK_OPTIMAL_PID
             && optimalParam.paramValue == ZL_TernaryParam_enable);

    // Fallback to store for small inputs
    if (numElts < 10) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputEdge, ZL_GRAPH_STORE));
        return ZL_returnSuccess();
    }

    // Compute the partitioning that attempts to minimize the size of the
    // bitpacked partition indices + the offsets into the partitions.
    PB_PartitionResult pr = PB_fixedPartition(
            ZL_Input_ptr(input), numElts, eltWidth, optimal, graph);
    if (pr.partitions == NULL) {
        // Allocation failure — fall back to store
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputEdge, ZL_GRAPH_STORE));
        return ZL_returnSuccess();
    }

    // Fallback to bitpack for single partition
    if (pr.numPartitions == 1) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputEdge, ZL_GRAPH_BITPACK));
        return ZL_returnSuccess();
    }

    // Fallback to bitpack for all-size-1 partitions
    {
        bool allOne = true;
        for (size_t i = 0; i < pr.numPartitions; ++i) {
            const uint64_t begin = pr.partitions[i];
            const uint64_t end   = (i + 1 == pr.numPartitions)
                      ? (pr.maxSymbolValue + 1)
                      : pr.partitions[i + 1];
            if (begin + 1 != end) {
                allOne = false;
                break;
            }
        }
        if (allOne) {
            ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputEdge, ZL_GRAPH_BITPACK));
            return ZL_returnSuccess();
        }
    }

    // Fallback to store if not enough gain
    const size_t maxByteCost =
            (eltWidth * (100 - PB_MIN_GAIN_PCT) * numElts) / 100;
    if (pr.bitCost / 8 >= maxByteCost) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputEdge, ZL_GRAPH_STORE));
        return ZL_returnSuccess();
    }

    // Convert partition boundaries to ZL_PartitionParams format:
    //   startValue = partitions[0]
    //   sizes[i] = partitions[i+1] - partitions[i]
    //   sizes[last] = maxSymbolValue + 1 - partitions[last]
    uint64_t partitionParams[PB_MAX_PARTITIONS + 1];
    partitionParams[0] = pr.partitions[0];
    for (size_t i = 0; i < pr.numPartitions; ++i) {
        const uint64_t begin   = pr.partitions[i];
        const uint64_t end     = (i + 1 == pr.numPartitions)
                    ? (pr.maxSymbolValue + 1)
                    : pr.partitions[i + 1];
        partitionParams[i + 1] = end - begin;
    }
    const size_t numParts = pr.numPartitions;

    const ZL_CopyParam copyParam = {
        ZL_PARTITION_CUSTOM_PID,
        partitionParams,
        sizeof(uint64_t) * (numParts + 1),
    };
    const ZL_LocalParams lp = { .copyParams = { &copyParam, 1 } };

    // Run partition node with params
    ZL_TRY_LET(
            ZL_EdgeList,
            outEdges,
            ZL_Edge_runNode_withParams(inputEdge, ZL_NODE_PARTITION, &lp));
    ZL_ASSERT_EQ(outEdges.nbEdges, 2);

    // Output 0 (bucket IDs) -> Bitpack
    ZL_ERR_IF_ERR(ZL_Edge_setDestination(outEdges.edges[0], ZL_GRAPH_BITPACK));
    // Output 1 (offsets) -> Store
    ZL_ERR_IF_ERR(ZL_Edge_setDestination(outEdges.edges[1], ZL_GRAPH_STORE));

    return ZL_returnSuccess();
}
