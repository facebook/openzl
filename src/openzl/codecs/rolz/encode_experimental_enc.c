// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdlib.h>
#include <string.h>
#include "openzl/common/speed.h"
#include "openzl/shared/portability.h"

#define FSE_STATIC_LINKING_ONLY

#include "openzl/codecs/entropy/deprecated/common_entropy.h"
#include "openzl/codecs/entropy/deprecated/encode_fse_kernel.h"
#include "openzl/codecs/rolz/common_markov.h"
#include "openzl/codecs/rolz/encode_encoder.h"
#include "openzl/common/cursor.h"
#include "openzl/fse/bitstream.h"
#include "openzl/fse/fse.h"
#include "openzl/fse/hist.h"
#include "openzl/shared/clustering.h"
#include "openzl/shared/mem.h"

#define kLitCoder ZS_EntropyEncoder_huf
#define kSeqCoder ZS_EntropyEncoder_fse

#if 0
#    define kMaxNumClusters 16
#    define kClusterMethod ZL_ClusteringMode_greedy
#else
#    define kMaxNumClusters 256
#    define kClusterMethod ZL_ClusteringMode_prune
#endif
#define kDoSplit 1
#define kSplitMethod ZS_SplitMethod_fixed

#define kO1Seqs 0
#define kO1MT kO1Seqs
#define kO1LL kO1Seqs
#define kO1ML kO1Seqs
#define kO1MC kO1Seqs

typedef enum {
    ZS_SplitMethod_fast,
    ZS_SplitMethod_fixed,
    ZS_SplitMethod_best,
    ZS_SplitMethod_none,
} ZS_SplitMethod_e;

typedef struct {
    ZS_encoderCtx base;
    ZS_EncoderParameters params;
} ZS_ExperimentalEncoderCtx;

static ZS_encoderCtx* ZS_encoderCtx_create(ZS_EncoderParameters const* params)
{
    ZS_ExperimentalEncoderCtx* ctx = (ZS_ExperimentalEncoderCtx*)malloc(
            sizeof(ZS_ExperimentalEncoderCtx));
    if (ctx == NULL)
        return NULL;
    ctx->params = *params;
    return &ctx->base;
}

static void ZS_encoderCtx_release(ZS_encoderCtx* ctx)
{
    if (ctx == NULL)
        return;
    free(ZL_CONTAINER_OF(ctx, ZS_ExperimentalEncoderCtx, base));
}

static void ZS_encoderCtx_reset(ZS_encoderCtx* ctx)
{
    (void)ctx;
}

static size_t ZS_experimentalEncoder_compressBound(
        size_t numLiterals,
        size_t numSequences)
{
    return 1000 + numLiterals + 16 * numSequences;
}

/**
 * Returns the cost in bits of encoding the normalized count header.
 * Returns an error if any of the helper functions return an error.
 */
static uint64_t ZSTD_NCountCost(
        unsigned const* count,
        unsigned const max,
        size_t const nbSeq,
        unsigned const FSELog)
{
    BYTE wksp[512];
    S16 norm[256];
    if (count[max] == nbSeq)
        return (2 << 3);
    const U32 tableLog = FSE_optimalTableLog(FSELog, nbSeq, max);
    ZL_REQUIRE(!FSE_isError(
            FSE_normalizeCount(norm, tableLog, count, nbSeq, max, 1)));
    size_t const bytes =
            FSE_writeNCount(wksp, sizeof(wksp), norm, max, tableLog);
    ZL_LOG_IF(FSE_isError(bytes), V9, "%s", FSE_getErrorName(bytes));
    ZL_REQUIRE(!FSE_isError(bytes));
    return (uint64_t)(1 + bytes) << 3;
}

/**
 * -log2(x / 256) lookup table for x in [0, 256).
 * If x == 0: Return 0
 * Else: Return floor(-log2(x / 256) * 256)
 */
static unsigned const kInverseProbabilityLog256[256] = {
    0,    2048, 1792, 1642, 1536, 1453, 1386, 1329, 1280, 1236, 1197, 1162,
    1130, 1100, 1073, 1047, 1024, 1001, 980,  960,  941,  923,  906,  889,
    874,  859,  844,  830,  817,  804,  791,  779,  768,  756,  745,  734,
    724,  714,  704,  694,  685,  676,  667,  658,  650,  642,  633,  626,
    618,  610,  603,  595,  588,  581,  574,  567,  561,  554,  548,  542,
    535,  529,  523,  517,  512,  506,  500,  495,  489,  484,  478,  473,
    468,  463,  458,  453,  448,  443,  438,  434,  429,  424,  420,  415,
    411,  407,  402,  398,  394,  390,  386,  382,  377,  373,  370,  366,
    362,  358,  354,  350,  347,  343,  339,  336,  332,  329,  325,  322,
    318,  315,  311,  308,  305,  302,  298,  295,  292,  289,  286,  282,
    279,  276,  273,  270,  267,  264,  261,  258,  256,  253,  250,  247,
    244,  241,  239,  236,  233,  230,  228,  225,  222,  220,  217,  215,
    212,  209,  207,  204,  202,  199,  197,  194,  192,  190,  187,  185,
    182,  180,  178,  175,  173,  171,  168,  166,  164,  162,  159,  157,
    155,  153,  151,  149,  146,  144,  142,  140,  138,  136,  134,  132,
    130,  128,  126,  123,  121,  119,  117,  115,  114,  112,  110,  108,
    106,  104,  102,  100,  98,   96,   94,   93,   91,   89,   87,   85,
    83,   82,   80,   78,   76,   74,   73,   71,   69,   67,   66,   64,
    62,   61,   59,   57,   55,   54,   52,   50,   49,   47,   46,   44,
    42,   41,   39,   37,   36,   34,   33,   31,   30,   28,   26,   25,
    23,   22,   20,   19,   17,   16,   14,   13,   11,   10,   8,    7,
    5,    4,    2,    1,
};

/**
 * Returns the cost in bits of encoding the distribution described by count
 * using the entropy bound.
 */
static uint64_t
ZSTD_entropyCost(unsigned const* count, unsigned const max, size_t const total)
{
    uint64_t cost = 0;
    unsigned s;
    if (count[max] == total)
        return 0;
    for (s = 0; s <= max; ++s) {
        uint64_t norm = ((256 * (uint64_t)count[s]) / (uint64_t)total);
        if (count[s] != 0 && norm == 0)
            norm = 1;
        assert(count[s] < total);
        cost += (uint64_t)count[s] * (uint64_t)kInverseProbabilityLog256[norm];
    }
    return cost >> 8;
}

static uint64_t split_cost(uint8_t const* codes, size_t size)
{
    assert(size > 0);
    unsigned hist[256];
    unsigned maxCode     = 255;
    unsigned cardinality = 0;
    ZL_REQUIRE(!FSE_isError(
            HIST_countFast(hist, &maxCode, &cardinality, codes, size)));
    return (4 << 3) + ZSTD_NCountCost(hist, maxCode, size, FSE_MAX_TABLELOG)
            + ZSTD_entropyCost(hist, maxCode, size);
}

static size_t split_impl(
        size_t* splits,
        size_t maxNumSplits,
        uint8_t const* codes,
        size_t size,
        size_t offset)
{
    if (maxNumSplits == 0)
        return 0;
    size_t const kNumDivides = 37;
    size_t const chunkSize   = (size + kNumDivides - 1) / kNumDivides;
    uint64_t bestCost        = split_cost(codes, size);
    size_t bestSplit         = 0;
    for (size_t split = chunkSize; split < size; split += chunkSize) {
        size_t const cost = split_cost(codes, split)
                + split_cost(codes + split, size - split);
        if (cost < bestCost) {
            bestCost  = cost;
            bestSplit = split;
        }
    }
    if (bestSplit == 0)
        return 0;
    size_t const leftNumSplits =
            split_impl(splits, maxNumSplits - 1, codes, bestSplit, offset);
    splits += leftNumSplits;
    maxNumSplits -= leftNumSplits;

    *splits = offset + bestSplit;
    splits += 1;
    maxNumSplits -= 1;

    size_t const rightNumSplits = split_impl(
            splits,
            maxNumSplits,
            codes + bestSplit,
            size - bestSplit,
            offset + bestSplit);
    return 1 + leftNumSplits + rightNumSplits;
}

// static int split_order(void const* lhsp, void const* rhsp) {
//   size_t const lhs = *(size_t const*)lhsp;
//   size_t const rhs = *(size_t const*)rhsp;
//   if (lhs < rhs)
//     return -1;
//   if (rhs > lhs)
//     return 1;
//   return 0;
// }

static size_t split_reduce(
        size_t* splits,
        size_t const inNumSplits,
        uint8_t const* codes,
        size_t size)
{
    size_t outNumSplits = 0;
    size_t prevSplit    = 0;
    for (size_t s = 0; s < inNumSplits; ++s) {
        size_t const begin       = prevSplit;
        size_t const split       = splits[s];
        size_t const end         = s + 1 == inNumSplits ? size : splits[s + 1];
        size_t const noSplitCost = split_cost(codes + begin, end - begin);
        size_t const splitCost   = split_cost(codes + begin, split - begin)
                + split_cost(codes + split, end - split);
        if (splitCost < noSplitCost) {
            splits[outNumSplits++] = split;
            prevSplit              = split;
        }
    }
    ZL_DLOG(V9, "Reduce splits: %zu -> %zu", inNumSplits, outNumSplits);
    return outNumSplits;
}

static size_t split_best(
        size_t* splits,
        size_t maxNumSplits,
        uint8_t const* codes,
        size_t size)
{
    size_t numSplits = split_impl(splits, maxNumSplits, codes, size, 0);
    numSplits        = split_reduce(splits, numSplits, codes, size);
    // size_t const originalCost = split_cost(codes, size);
    bool const logSplits = 0;
    ZL_RLOG_IF(logSplits, V9, "Split %zu: ", numSplits);
    for (size_t i = 0; i <= numSplits; ++i) {
        size_t const begin = i == 0 ? 0 : splits[i - 1];
        size_t const split = i == numSplits ? size : splits[i];
        assert(split > begin);
        ZL_RLOG_IF(logSplits, V9, "[%zu, %zu) ", begin, split);
    }
    ZL_RLOG_IF(logSplits, V9, "\n");
    return numSplits;
}

static size_t split_fast_impl(
        size_t* splits,
        size_t maxNumSplits,
        uint8_t const* codes,
        size_t size,
        uint32_t numSymbols)
{
    uint32_t const kCheckEvery = 1024;
    uint32_t prev[256]         = { 0 };
    uint32_t new[256]          = { 0 };
    size_t const lastStart     = size - ZL_MIN(size, kCheckEvery);
    size_t lastSplit           = 0;
    size_t numSplits           = 0;

    for (size_t start = 0; start < lastStart; start += kCheckEvery) {
        if (start == lastSplit) {
            for (size_t i = 0; i < kCheckEvery; ++i) {
                ++prev[codes[i]];
            }
            continue;
        }
        for (size_t i = 0; i < kCheckEvery; ++i) {
            ++new[codes[i]];
        }
        uint32_t const currBlockSize = (uint32_t)(start - lastSplit);
        uint32_t total_delta         = 0;
        for (size_t i = 0; i < numSymbols; ++i) {
            uint32_t const expected = prev[i] * kCheckEvery;
            uint32_t const actual   = new[i] * currBlockSize;
            uint32_t const delta =
                    (actual > expected) ? actual - expected : expected - actual;
            total_delta += delta;
        }

        if (total_delta + (currBlockSize >> 12) * currBlockSize
            >= 25 * numSymbols * currBlockSize) {
            lastSplit           = start;
            splits[numSplits++] = lastSplit;
            memset(prev, 0, sizeof(prev));
            memset(new, 0, sizeof(new));
            if (numSplits == maxNumSplits) {
                ZL_REQUIRE_FAIL("Ran out of splits");
                break;
            }
        }
    }
    return numSplits;
}

static size_t split_fast(
        size_t* splits,
        size_t maxNumSplits,
        uint8_t const* codes,
        size_t size,
        uint32_t maxSymbol)
{
    if (maxSymbol < 8)
        return split_fast_impl(splits, maxNumSplits, codes, size, 8);
    if (maxSymbol < 32)
        return split_fast_impl(splits, maxNumSplits, codes, size, 32);
    if (maxSymbol < 64)
        return split_fast_impl(splits, maxNumSplits, codes, size, 64);
    return split_fast_impl(splits, maxNumSplits, codes, size, 256);
}

static size_t split_fixed(
        size_t* splits,
        size_t maxNumSplits,
        uint8_t const* codes,
        size_t size)
{
    size_t const kBlockSize = 1u << 13;
    size_t numSplits        = 0;
    (void)codes;
    for (size_t split = kBlockSize; split < size; split += kBlockSize) {
        splits[numSplits++] = split;
        if (numSplits == maxNumSplits) {
            ZL_REQUIRE_FAIL("ran outta splits");
            break;
        }
    }
    return numSplits;
}

// split function appears to be unused but kept for potential future use
static ZL_UNUSED_ATTR size_t
split(size_t* splits,
      size_t maxNumSplits,
      uint8_t const* codes,
      size_t size,
      uint32_t maxSymbol)
{
    switch (kSplitMethod) {
        case ZS_SplitMethod_best:
            return split_best(splits, maxNumSplits, codes, size);
        case ZS_SplitMethod_fast:
            return split_fast(splits, maxNumSplits, codes, size, maxSymbol);
        case ZS_SplitMethod_fixed:
            return split_fixed(splits, maxNumSplits, codes, size);
        case ZS_SplitMethod_none:
            return 0;
        default:
            ZL_REQUIRE_FAIL("Impossible");
    }
}

typedef enum {
    ZS_EntropyEncoder_huf = 0,
    ZS_EntropyEncoder_fse = 1,
} ZS_EntropyEncoder;

static void encodeCodes(
        ZL_WC* out,
        uint8_t const* codes,
        size_t numSequences,
        uint32_t maxSymbol,
        char const* name,
        uint32_t extraCost,
        bool o1,
        ZS_EntropyEncoder coder);

static void encodeLiterals(
        ZL_WC* out,
        uint8_t const* lits,
        uint8_t const* litsCtx,
        size_t numLits,
        ZS_LiteralEncoding_e literalEncoding)
{
    ZL_WC const outCopy = *out;
    if (numLits == 0) {
        return;
    }
    ZL_ASSERT_EQ(litsCtx[0], 0);
    //> Compute O0 size for debug info
    uint32_t o0Size = 0;
    if (ZL_DBG_LVL >= 4) {
        ZL_WC cpy = *out;
        ZL_WC_push(&cpy, 0);
        encodeCodes(&cpy, lits, numLits, 255, NULL, 0, false, kLitCoder);
        o0Size = (uint32_t)(ZL_WC_avail(out) - ZL_WC_avail(&cpy));
        if (literalEncoding == ZS_LiteralEncoding_o0) {
            ZL_LOG(TRANSFORM, "O0 lits: %zu -> %u", numLits, o0Size);
            *out = cpy;
            return;
        }
    }
    //> Cluster
    ZL_ContextClustering clustering;
    {
        ZL_RC src = ZL_RC_wrap(lits, numLits);
        ZL_RC ctx = ZL_RC_wrap(litsCtx, numLits);
        ZL_REQUIRE_SUCCESS(ZL_cluster(
                &clustering, src, ctx, 255, kMaxNumClusters, kClusterMethod));
    }

    //> Count size of each cluster
    uint32_t counts[kMaxNumClusters];
    memset(counts, 0, sizeof(counts));
    for (size_t i = 0; i < numLits; ++i) {
        ++counts[clustering.contextToCluster[litsCtx[i]]];
    }

    //> Write header (1 means O0 + clustering)
    ZL_WC_REQUIRE_HAS(out, 1);
    ZL_WC_push(out, 1);
    ZL_REQUIRE_SUCCESS(ZL_ContextClustering_encode(out, &clustering));

    uint8_t* const clusteredLits = (uint8_t*)malloc(numLits);
    ZL_REQUIRE_NN(clusteredLits);
    {
        //> Compute transpose
        uint32_t offsets[kMaxNumClusters];
        offsets[0] = 0;
        for (size_t i = 0; i < clustering.numClusters - 1; ++i) {
            offsets[i + 1] = offsets[i] + counts[i];
        }
        ZL_ASSERT_EQ(
                offsets[clustering.numClusters - 1]
                        + counts[clustering.numClusters - 1],
                numLits);
        //> Transpose
        for (size_t i = 0; i < numLits; ++i) {
            size_t const cluster = clustering.contextToCluster[litsCtx[i]];
            clusteredLits[offsets[cluster]++] = lits[i];
        }
        //> Reset offsets
        ZL_ASSERT_EQ(offsets[clustering.numClusters - 1], numLits);
        for (size_t i = 0; i < clustering.numClusters; ++i) {
            offsets[i] -= counts[i];
        }
        ZL_ASSERT_EQ(offsets[0], 0);
        //> Compress
        uint8_t* prev = clusteredLits;
        for (size_t i = 0; i < clustering.numClusters; ++i) {
            uint8_t* const clusterLits = clusteredLits + offsets[i];
            ZL_ASSERT_EQ(prev, clusterLits);
            prev += counts[i];
            uint32_t const numClusterLits = counts[i];
            ZL_WC_REQUIRE_HAS(out, 4);
            ZL_WC_pushLE32(out, numClusterLits);
            encodeCodes(
                    out,
                    clusterLits,
                    numClusterLits,
                    255,
                    NULL,
                    0,
                    false,
                    kLitCoder);
            // ZL_ASSERT_LE(offsets[i] + numClusterLits, numLits);
            // size_t splits[1000];
            // size_t const numSplits = split(splits, 1000, clusterLits,
            // numClusterLits); ZL_WC_pushVarint(out, numSplits); for (size_t s
            // = 0; s
            // <= numSplits; ++s) {
            //   uint8_t* const hdr = ZL_WC_ptr(out);
            //   ZL_WC_REQUIRE_HAS(out, 4);
            //   ZL_WC_advance(out, 4);
            //   uint8_t* const splitLits = clusterLits + (s == 0 ? 0 : splits[s
            //   - 1]); uint8_t* const splitLitsEnd =
            //       clusterLits + (s == numSplits ? numClusterLits :
            //       splits[s]);
            //   ZL_ASSERT_LE(splitLitsEnd, clusteredLits + numLits);
            //   ZL_ASSERT_LT(splitLits, splitLitsEnd);
            //   size_t const numSplitLits = (size_t)(splitLitsEnd - splitLits);
            //   ZL_RC in                  = ZL_RC_wrap(splitLits,
            //   numSplitLits); ZL_REQUIRE_SUCCESS(ZS_fseEncode(out, &in));
            //   ZL_writeLE32(hdr, (uint32_t)(ZL_WC_ptr(out) - hdr - 4));
            // }
        }
        ZL_ASSERT_EQ(prev, clusteredLits + numLits);
    }
    free(clusteredLits);
    uint32_t const o1Size =
            (uint32_t)(ZL_WC_avail(&outCopy) - ZL_WC_avail(out));
    ZL_LOG(TRANSFORM,
           "Literals: n=%u o0=%u o1=%u --> Chose %s",
           (uint32_t)numLits,
           o0Size,
           o1Size,
           o0Size <= o1Size ? "O0" : "O1");
    // if (o0Size <= o1Size) {
    //   *out = outCopy;
    //   ZL_WC_push(out, 0);
    //   encodeCodes(out, lits, numLits, NULL, 0, false, kLitCoder);
    // }
}

static uint8_t const valueToCode[32] = { 0,  1,  2,  3,  4,  5,  6,  7,
                                         8,  9,  10, 11, 12, 13, 14, 15,
                                         16, 17, 18, 19, 20, 21, 22, 23,
                                         24, 25, 26, 27, 28, 29, 30, 31 };
#define maxLog2 5
#define maxPow2 (1u << maxLog2)
static uint32_t const delta = maxPow2 - maxLog2;
static uint32_t bits[]      = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
                                0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
                                0,  0,  0,  0,  0,  0,  0,  0,  5,  6,  7,  8,
                                9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                                21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
ZL_STATIC_ASSERT(sizeof(valueToCode) == (1u << maxLog2), "Assumption");

static void encodeCodes(
        ZL_WC* out,
        uint8_t const* codes,
        size_t numSequences,
        uint32_t maxSymbol,
        char const* name,
        uint32_t extraCost,
        bool o1,
        ZS_EntropyEncoder coder)
{
    ZL_REQUIRE(!o1);
    (void)coder;
    ZS_Entropy_EncodeParameters params = {
        .allowedTypes = ZS_Entropy_TypeMask_all,
        .encodeSpeed =
                ZL_EncodeSpeed_fromBaseline(ZL_EncodeSpeedBaseline_faster),
        .decodeSpeed = ZL_DecodeSpeed_fromBaseline(ZL_DecodeSpeedBaseline_zlib),
        .precomputedHistogram = NULL,
        .cardinalityEstimate  = maxSymbol + 1,
        .maxValueUpperBound   = maxSymbol,
        .maxTableLog          = 0,
        .blockSplits          = NULL,
        .tableManager         = NULL,
    };
    size_t const outAvail = ZL_WC_avail(out);
    ZL_REQUIRE_SUCCESS(ZS_Entropy_encode(out, codes, numSequences, 1, &params));
    if (name) {
        ZL_LOG(TRANSFORM,
               "%s %u: O0: %zu (extra=%u)",
               name,
               (uint32_t)numSequences,
               outAvail - ZL_WC_avail(out) + extraCost,
               extraCost);
    }
}

static void
encodeMatchTypes(ZL_WC* out, uint8_t const* codes, size_t numSequences)
{
    uint32_t const kMaxMatchType = 4;
    encodeCodes(
            out,
            codes,
            numSequences,
            kMaxMatchType,
            "MatchType",
            0,
            kO1MT,
            kSeqCoder);
}

ZL_FORCE_INLINE void encodeSeq(
        ZL_WC* out,
        uint8_t const* states,
        uint32_t const* values,
        size_t numSequences,
        char const* type,
        bool o1)
{
    ZL_LOG(TRANSFORM, "%s", type);
    if (numSequences == 0)
        return;

    uint8_t* const codes = (uint8_t*)malloc(2 * numSequences);
    ZL_REQUIRE_NN(codes);
    uint8_t* const tmpCodes = codes + numSequences;

    // Bitstream size + offset for each type
    ZL_WC_REQUIRE_HAS(out, 4 + ZS_MARKOV_NUM_STATES * 4);
    uint8_t* const header = ZL_WC_ptr(out);
    ZL_WC_advance(out, 4 + ZS_MARKOV_NUM_STATES * 4);

    BIT_CStream_t cstream;
    BIT_initCStream(&cstream, ZL_WC_ptr(out), ZL_WC_avail(out));

    uint32_t bitCosts[ZS_MARKOV_NUM_STATES] = { 0 };
    uint32_t sizes[ZS_MARKOV_NUM_STATES]    = { 0 };

    for (size_t s = numSequences; s-- > 0;) {
        uint8_t code;
        if (values[s] >= maxPow2)
            code = (uint8_t)(delta + (uint32_t)ZL_highbit32(values[s]));
        else
            code = valueToCode[values[s]];
        BIT_addBits(&cstream, values[s], bits[code]);
        if (s & 1)
            BIT_flushBits(&cstream);
        tmpCodes[s] = code;
        ++sizes[states[s]];
        if (ZL_DBG_LVL >= 4)
            bitCosts[states[s]] += bits[code];
    }
    BIT_flushBits(&cstream);
    size_t const bitSize = BIT_closeCStream(&cstream);
    ZL_REQUIRE_NE(bitSize, 0);
    ZL_WC_advance(out, bitSize);
    ZL_writeLE32(header, (uint32_t)bitSize);

    uint32_t offsets[ZS_MARKOV_NUM_STATES] = { 0 };
    for (size_t i = 1; i < ZS_MARKOV_NUM_STATES; ++i)
        offsets[i] = offsets[i - 1] + sizes[i - 1];

    for (size_t s = 0; s < numSequences; ++s) {
        uint32_t const offset = offsets[states[s]]++;
        codes[offset]         = tmpCodes[s];
    }
    ZL_ASSERT_EQ(offsets[0], sizes[0]);
    ZL_ASSERT_EQ(offsets[ZS_MARKOV_NUM_STATES - 1], numSequences);

    for (size_t m = 0; m < ZS_MARKOV_NUM_STATES; ++m) {
        uint32_t const size   = sizes[m];
        uint32_t const offset = offsets[m] - size;
        // if (m < ZL_ASSERT_EQ(offsets[m] + size, offsets[m+1]);
        char name[100];
        const char* sn[ZS_MARKOV_NUM_STATES] = { "lz",    "rz",    "*-r0",
                                                 "rz-r0", "r0-r0", "rep" };
        snprintf(name, sizeof(name), "%s %s", sn[m], type);
        encodeCodes(
                out,
                codes + offset,
                size,
                delta + 32,
                name,
                bitCosts[m] >> 3,
                o1,
                kSeqCoder);

        ZL_writeLE32(header + 4 + 4 * m, offsets[m]);
    }

    // //> Write codes
    // uint8_t* const fseStart = ZL_WC_ptr(out);
    // ZL_RC ctx               = ZL_RC_wrap(types, numSequences);
    // ZL_RC src               = ZL_RC_wrap(codes, numSequences);
    // ZL_ContextClustering clustering;
    // ZL_REQUIRE_SUCCESS(
    //     ZL_cluster(&clustering, src, ctx, 16, ZL_ClusteringMode_identity));
    // ZS_fseContextEncode(out, &src, &ctx, &clustering);
    // uint32_t const fseSize = (uint32_t)(ZL_WC_ptr(out) - fseStart);
    // ZL_writeLE32(fseSizePtr, fseSize);
    free(codes);

    ZL_LOG(TRANSFORM,
           "%s: %u bytes",
           type,
           (unsigned)(ZL_WC_ptr(out) - header));
}

static void encodeLitLengths(
        ZL_WC* out,
        uint8_t const* states,
        uint32_t const* codes,
        size_t numSequences)
{
    encodeSeq(out, states, codes, numSequences, "LitLength", kO1LL);
    // if (numSequences == 0)
    //   return;
    // uint8_t* const hdr = ZL_WC_ptr(out);
    // ZL_WC_advance(out, 4);
    // ZL_REQUIRE_SUCCESS(ZS_baseBitsEncode(
    //     out, codes, numSequences, valueToCode, delta, maxPow2, bits));
    // uint32_t const size = (uint32_t)(ZL_WC_ptr(out) - hdr - 4);
    // ZL_writeLE32(hdr, size);
}

static void encodeMatchLengths(
        ZL_WC* out,
        uint8_t const* states,
        uint32_t const* codes,
        size_t numSequences)
{
    encodeSeq(out, states, codes, numSequences, "MatchLength", kO1ML);
}

static void encodeMatchCodes(
        ZL_WC* out,
        uint8_t const* states,
        uint32_t const* codes,
        size_t numSequences)
{
    encodeSeq(out, states, codes, numSequences, "MatchCode", kO1MC);
}

static size_t ZS_experimentalEncoder_compress(
        ZS_encoderCtx* base,
        uint8_t* dst,
        size_t capacity,
        ZS_RolzSeqStore const* seqStore)
{
    ZS_ExperimentalEncoderCtx* const ctx =
            ZL_CONTAINER_OF(base, ZS_ExperimentalEncoderCtx, base);
    uint32_t const numLiterals =
            (uint32_t)(seqStore->lits.ptr - seqStore->lits.start);
    uint32_t const numSequences =
            (uint32_t)(seqStore->seqs.ptr - seqStore->seqs.start);
    ZL_ASSERT_GE(
            capacity,
            ZS_experimentalEncoder_compressBound(numLiterals, numSequences));
    if (capacity
        < ZS_experimentalEncoder_compressBound(numLiterals, numSequences)) {
        return 0;
    }
    ZL_WC out = ZL_WC_wrap(dst, capacity);
    ZL_WC_REQUIRE_HAS(&out, 15);
    ZL_WC_push(&out, (uint8_t)ctx->params.rolzContextDepth);
    ZL_WC_push(&out, (uint8_t)ctx->params.rolzContextLog);
    ZL_WC_push(&out, (uint8_t)ctx->params.rolzRowLog);
    ZL_WC_push(&out, (uint8_t)ctx->params.rolzMinLength);
    ZL_WC_push(&out, (uint8_t)ctx->params.rolzPredictMatchLength);
    ZL_WC_push(&out, (uint8_t)ctx->params.lzMinLength);
    ZL_WC_push(&out, (uint8_t)ctx->params.repMinLength);
    ZL_WC_pushLE32(&out, numLiterals);
    ZL_WC_pushLE32(&out, numSequences);

    // Write literals
    ZL_ASSERT_EQ(
            seqStore->lits.ptr - seqStore->lits.start,
            seqStore->litsCtx.ptr - seqStore->litsCtx.start);
    encodeLiterals(
            &out,
            seqStore->lits.start,
            seqStore->litsCtx.start,
            numLiterals,
            ctx->params.literalEncoding);
    // encodeLiterals(
    //     &out, seqStore->lits.start, seqStore->lits.start-1, numLiterals);

    uint8_t* const mts  = malloc(numSequences);
    uint32_t* const lls = malloc(numSequences * sizeof(uint32_t));
    uint32_t* const mls = malloc(numSequences * sizeof(uint32_t));
    uint32_t* const mcs = malloc(numSequences * sizeof(uint32_t));

    ZL_REQUIRE(mts != NULL && lls != NULL && mls != NULL && mcs != NULL);

    ZS_sequence const* seqs = seqStore->seqs.start;
    for (size_t i = 0; i < numSequences; ++i) {
        ZL_REQUIRE_UINT_FITS(seqs[i].matchType, uint8_t);
        mts[i] = (uint8_t)seqs[i].matchType;
        lls[i] = seqs[i].literalLength;
        mls[i] = seqs[i].matchLength;
        mcs[i] = seqs[i].matchCode;
    }

    encodeMatchTypes(&out, mts, numSequences);

    uint8_t* const states = mts;
    {
        uint32_t state = ZS_MARKOV_RZ_INITIAL_STATE;
        for (size_t s = 0; s < numSequences; ++s) {
            state     = ZS_markov_nextState(state, mts[s]);
            states[s] = (uint8_t)state;
        }
    }

    encodeLitLengths(&out, states, lls, numSequences);
    encodeMatchLengths(&out, states, mls, numSequences);
    encodeMatchCodes(&out, states, mcs, numSequences);

    free(mts);
    free(lls);
    free(mls);
    free(mcs);
    return (size_t)(ZL_WC_ptr(&out) - dst);
}

const ZS_encoder ZS_experimentalEncoder = {
    .name          = "experimental",
    .ctx_create    = ZS_encoderCtx_create,
    .ctx_release   = ZS_encoderCtx_release,
    .ctx_reset     = ZS_encoderCtx_reset,
    .compressBound = ZS_experimentalEncoder_compressBound,
    .compress      = ZS_experimentalEncoder_compress,
};
