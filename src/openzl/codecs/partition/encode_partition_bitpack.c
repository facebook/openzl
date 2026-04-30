// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/partition/encode_partition_bitpack.h"

#include <string.h>

#include "openzl/zl_graph_api.h"

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

/// Instead of operating in a 64K histogram, operate on a 64K >>
/// PB_PRECISION_LOSS histogram. This is imporant for two reasons:
/// 1. It significantly speeds up the optimization algorithms.
/// 2. The partition encoding kernel's LUT size reduces by this factor.
#define PB_PRECISION_LOSS 4
/// Limiting the maximum partition size speeds up encoding & decoding
/// significantly because they can guarantee that they can decode 4 values when
/// reading 64-bits from the bitstream (accounting for the up to 7 bits that
/// have already been consumed from the previous iteration): 14 * 4 + 7 = 63.
#define PB_MAX_BUCKET_SIZE (1u << 14)
/// Add a small fixed cost per partition
#define PB_OVERHEAD_BITS 24
/// The maximum possible number of partitions
#define PB_MAX_PARTITIONS 256
/// Don't partition if the gain is less than this and just fallback to store
#define PB_MIN_GAIN_PCT 1

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32_t PB_idx2bucket(uint32_t idx)
{
    return idx >> PB_PRECISION_LOSS;
}

static uint32_t PB_bucket2idx(uint32_t bucket)
{
    return bucket << PB_PRECISION_LOSS;
}

static size_t PB_bucketSize(uint32_t b, uint32_t e)
{
    return (e - b) << PB_PRECISION_LOSS;
}

typedef struct {
    /// Size: histSize + 1
    /// Last value is the total count from [0, histSize)
    const uint32_t* cumHist;
    uint32_t histSize;
} PB_CumHist;

// ---------------------------------------------------------------------------
// Cost function
// ---------------------------------------------------------------------------

/// Compute total bit cost for a set of partitions over a cumulative histogram.
/// It consists of the cost of the fixed bitpacked partition IDs, which take
/// ceil(log2(numPartitions)), plus the cost of the variable partition offsets,
/// each of which takes ceil(log2(partitionSizes[i])) for a value that falls
/// into partition i.
///
/// @param cumHist Cumulative histogram, size = histSize + 1
/// @param histSize Number of entries in cumHist - 1
/// @param partitions Array of partition start indices
/// @param numPartitions Number of partitions
/// @param totalPartitions Total partition count for bucket bits calculation
static uint32_t PB_fixedBucketCost(
        const uint32_t* cumHist,
        uint32_t histSize,
        const uint32_t* partitions,
        size_t numPartitions,
        size_t totalPartitions)
{
    const uint32_t totalCount = cumHist[histSize];
    const uint32_t bucketBits = (uint32_t)ZL_nextPow2(totalPartitions);
    uint32_t cost             = totalCount * bucketBits;
    for (size_t i = 0; i < numPartitions; ++i) {
        const uint32_t b = partitions[i];
        const uint32_t e =
                (i + 1 == numPartitions) ? histSize : partitions[i + 1];
        ZL_ASSERT_LT(b, e);
        ZL_ASSERT_LE(e - b, PB_idx2bucket(PB_MAX_BUCKET_SIZE));
        const uint32_t count   = cumHist[e] - cumHist[b];
        const uint32_t offBits = (uint32_t)ZL_nextPow2(PB_bucketSize(b, e));
        cost += PB_OVERHEAD_BITS + count * offBits;
    }
    return cost;
}

// ---------------------------------------------------------------------------
// DP Partition (ported from utils/Partition.hpp 3rd overload)
// ---------------------------------------------------------------------------

/// Cost function context for the DP partition algorithm.
typedef struct {
    const uint32_t* cumHist;
    uint32_t histSize;
    size_t numPartitions; // total target partitions, for bucketBits calc
    uint32_t fixedCost;   // fixed cost for bucket bits
} PB_DPCostCtx;

/// Cost of a single bucket spanning [b, e) in the cumulative histogram.
static uint32_t PB_dpBucketCost(const PB_DPCostCtx* ctx, uint32_t b, uint32_t e)
{
    ZL_ASSERT_LT(b, e);
    ZL_ASSERT_LE(e, ctx->histSize);
    ZL_ASSERT_LE(e - b, PB_idx2bucket(PB_MAX_BUCKET_SIZE));
    const uint32_t count    = ctx->cumHist[e] - ctx->cumHist[b];
    const size_t bucketSize = PB_bucketSize(b, e);
    return count * (ctx->fixedCost + (unsigned)ZL_nextPow2(bucketSize));
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
    const uint32_t N  = ctx->histSize;
    const uint32_t B  = ZL_MIN((uint32_t)numBuckets, N);
    const uint32_t B1 = B + 1;
    const uint32_t N1 = N + 1;

    const uint32_t maxBucketSize = PB_idx2bucket(PB_MAX_BUCKET_SIZE);

    // Allocate DP tables from graph scratch space (arena, no free needed)
    const size_t tableBytes = (size_t)N1 * B1 * sizeof(uint32_t);
    uint32_t* opt   = (uint32_t*)ZL_Graph_getScratchSpace(graph, tableBytes);
    uint32_t* begin = (uint32_t*)ZL_Graph_getScratchSpace(graph, tableBytes);
    if (!opt || !begin) {
        return 0;
    }

    // Initialize
    memset(opt, -1, tableBytes);
    memset(begin, -1, tableBytes);

    opt[N1 * 0 + 0]   = 0;
    begin[N1 * 0 + 0] = 0;

    for (uint32_t e = 1; e < N1; ++e) {
        const size_t maxPossibleBuckets = ZL_MIN(e, B);
        for (uint32_t k = 1; k <= maxPossibleBuckets; ++k) {
            const uint32_t maxSz = ZL_MIN(maxBucketSize, e - (k - 1));
            for (uint32_t size = 1; size <= maxSz;
                 size          = (e == N) ? size + 1 : size * 2) {
                const uint32_t b        = e - size;
                const uint32_t prevCost = opt[N1 * (k - 1) + b];
                if (prevCost == UINT32_MAX) {
                    continue;
                }
                const uint32_t oldCost = opt[N1 * k + e];
                const uint32_t endCost = PB_dpBucketCost(ctx, b, e);
                const uint32_t newCost = prevCost + endCost;
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
    const uint32_t* cumHist;
    uint32_t histSize;
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
static uint32_t
PB_singlePartitionCost(const uint32_t* cumHist, uint32_t b, uint32_t e)
{
    const uint32_t count   = cumHist[e] - cumHist[b];
    const uint32_t offBits = (uint32_t)ZL_nextPow2(PB_bucketSize(b, e));
    return PB_OVERHEAD_BITS + count * offBits;
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

/// @returns true iff the partition from [begin, end) is legal
static bool PB_isLegalPartition(uint32_t begin, uint32_t end)
{
    return begin < end && (end - begin) <= PB_idx2bucket(PB_MAX_BUCKET_SIZE);
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
static uint32_t PB_modifiedRangeCost(
        const uint32_t* cumHist,
        const uint32_t* partitions,
        const uint32_t* out,
        size_t n,
        uint32_t histSize,
        size_t from,
        size_t to)
{
    uint32_t cost = 0;
    for (size_t i = from; i < to; ++i) {
        const uint32_t b = (i == from) ? partitions[i] : out[i];
        uint32_t e;
        if (i + 1 == n) {
            e = histSize;
        } else if (i + 1 < to) {
            e = out[i + 1];
        } else {
            e = partitions[i + 1];
        }
        if (e > histSize || !PB_isLegalPartition(b, e)) {
            return UINT32_MAX;
        }
        cost += PB_singlePartitionCost(cumHist, b, e);
    }
    return cost;
}

/// Try applying a cascaded mutation from scratch and accept if it reduces cost.
/// Returns true if the mutation was accepted.
static bool PB_tryMutation(
        PB_GreedyOpt* opt,
        uint32_t* partCost,
        uint32_t* currentCost,
        size_t n,
        size_t idx,
        size_t cascadeEnd)
{
    uint32_t newRangeCost = PB_modifiedRangeCost(
            opt->cumHist,
            opt->partitions,
            opt->scratch,
            n,
            opt->histSize,
            idx,
            cascadeEnd);
    if (newRangeCost == UINT32_MAX) {
        return false;
    }
    uint32_t oldRangeCost = 0;
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
                PB_partitionEnd(opt->partitions, n, opt->histSize, j);
        partCost[j] = PB_singlePartitionCost(opt->cumHist, b, e);
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

    uint32_t partCost[PB_MAX_PARTITIONS];
    uint32_t sumCost = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t b = opt->partitions[i];
        const uint32_t e =
                PB_partitionEnd(opt->partitions, n, opt->histSize, i);
        partCost[i] = PB_singlePartitionCost(opt->cumHist, b, e);
        sumCost += partCost[i];
    }

    const uint32_t totalCount = opt->cumHist[opt->histSize];
    const uint32_t bucketBits = (uint32_t)ZL_nextPow2(opt->targetPartitions);
    const uint32_t baseCost   = totalCount * bucketBits;
    uint32_t currentCost      = baseCost + sumCost;

    for (;;) {
        const uint32_t startCost = currentCost;
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
            opt->partitions, opt->numPartitions, opt->histSize, i);
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
static int32_t
PB_splitGain(const uint32_t* cumHist, uint32_t begin, uint32_t end)
{
    assert(end - begin > 1);
    assert(PB_isLegalPartition(begin, end));
    const uint32_t oldCost   = PB_singlePartitionCost(cumHist, begin, end);
    const uint32_t sz        = end - begin;
    const uint32_t newSize   = 1u << ((unsigned)ZL_nextPow2(sz) - 1);
    const uint32_t mid       = begin + newSize;
    const uint32_t leftCost  = PB_singlePartitionCost(cumHist, begin, mid);
    const uint32_t rightCost = PB_singlePartitionCost(cumHist, mid, end);
    return (int32_t)oldCost - (int32_t)(leftCost + rightCost);
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
    int32_t gains[PB_MAX_PARTITIONS];
    {
        const size_t n     = opt->numPartitions;
        const size_t start = opt->prefixSize > 0 ? opt->prefixSize - 1 : 0;
        for (size_t i = 0; i < start; ++i) {
            gains[i] = INT32_MIN;
        }
        for (size_t i = start; i < n; ++i) {
            const uint32_t b = opt->partitions[i];
            const uint32_t e =
                    PB_partitionEnd(opt->partitions, n, opt->histSize, i);
            gains[i] = (e - b <= 1) ? INT32_MIN
                    : !PB_isLegalPartition(b, e)
                    ? INT32_MAX
                    : PB_splitGain(opt->cumHist, b, e);
        }
    }

    while (opt->numPartitions < targetPartitions) {
        const size_t n   = opt->numPartitions;
        int32_t bestGain = INT32_MIN;
        size_t bestIdx   = (size_t)-1;
        for (size_t i = 0; i < n; ++i) {
            if (gains[i] == INT32_MAX) {
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
                    opt->partitions, opt->numPartitions, opt->histSize, j);
            gains[j] = (e - b <= 1) ? INT32_MIN
                    : !PB_isLegalPartition(b, e)
                    ? INT32_MAX
                    : PB_splitGain(opt->cumHist, b, e);
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
    opt.cumHist          = cumHist.cumHist;
    opt.histSize         = cumHist.histSize;
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
    ctx.cumHist       = cumHist.cumHist;
    ctx.histSize      = cumHist.histSize;
    ctx.numPartitions = numPartitions;
    ctx.fixedCost     = (unsigned)ZL_nextPow2(numPartitions);
    return PB_dpPartition(numPartitions, &ctx, outPartitions, graph);
}

// ---------------------------------------------------------------------------
// fixedPartitionFast (outer, over raw uint16_t data)
// ---------------------------------------------------------------------------

typedef struct {
    uint32_t* partitions;
    size_t numPartitions;
    size_t bitCost;
    uint32_t maxSymbolValue;
} PB_PartitionResult;

static bool PB_buildCumHist(
        PB_CumHist* out,
        const uint16_t* data,
        size_t numElts,
        bool optimal,
        ZL_Graph* graph)
{
    uint32_t histSize    = 1u << (16 - PB_PRECISION_LOSS);
    uint32_t* const hist = (uint32_t*)ZL_Graph_getScratchSpace(
            graph, (histSize + 1) * sizeof(uint32_t));
    if (hist == NULL) {
        return false;
    }

    const uint32_t skip = optimal ? 1 : 3;

    memset(hist, 0, histSize * sizeof(uint32_t));
    for (size_t i = 0; i < numElts; i += skip) {
        ++hist[PB_idx2bucket(data[i])];
    }

    if (skip > 1) {
        // Since we are skipping data, we don't know the min/max value
        // So we need to add dummy entries at the beginning and end
        hist[0] += (hist[0] == 0);
        hist[histSize - 1] += (hist[histSize - 1] == 0);
    }

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

    out->cumHist  = hist;
    out->histSize = histSize;

    return true;
}

/// Compute optimal partition boundaries for 16-bit data.
/// All allocations use stack or graph scratch space (no malloc/free).
static PB_PartitionResult PB_fixedPartition(
        const uint16_t* data,
        size_t numElts,
        bool optimal,
        ZL_Graph* graph)
{
    PB_PartitionResult result = { NULL, 0, 0, 0 };

    PB_CumHist cumHist;
    if (!PB_buildCumHist(&cumHist, data, numElts, optimal, graph)) {
        return result;
    }

    // Try 16 and 32 partitions, keep best
    uint32_t bestBuf[PB_MAX_PARTITIONS];
    size_t bestSize   = 0;
    uint32_t bestCost = UINT32_MAX;

    uint32_t trialBuf[PB_MAX_PARTITIONS];
    const size_t trialCounts[] = { 16, 32 };
    for (size_t t = 0; t < 2; ++t) {
        size_t numP   = trialCounts[t];
        size_t actual = PB_fixedPartitionFastInner(
                cumHist, numP, trialBuf, optimal, graph);
        if (actual == 0) {
            continue;
        }
        uint32_t cost = PB_fixedBucketCost(
                cumHist.cumHist, cumHist.histSize, trialBuf, actual, actual);
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
    result.partitions = (uint32_t*)ZL_Graph_getScratchSpace(
            graph, bestSize * sizeof(uint32_t));
    if (!result.partitions) {
        return result;
    }
    for (size_t i = 0; i < bestSize; ++i) {
        result.partitions[i] = PB_bucket2idx(bestBuf[i]);
    }
    result.numPartitions  = bestSize;
    result.bitCost        = bestCost;
    result.maxSymbolValue = PB_bucket2idx(cumHist.histSize) - 1;

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

    ZL_ERR_IF_NE(
            ZL_Input_eltWidth(input),
            2,
            node_invalid_input,
            "Currently only 2-byte numeric values are accepted");

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
            (const uint16_t*)ZL_Input_ptr(input), numElts, optimal, graph);
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
            const uint32_t begin = pr.partitions[i];
            const uint32_t end   = (i + 1 == pr.numPartitions)
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
            (sizeof(uint16_t) * (100 - PB_MIN_GAIN_PCT) * numElts) / 100;
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
