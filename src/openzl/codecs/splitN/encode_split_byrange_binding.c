// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/splitN/encode_split_byrange_binding.h"

#include <stdint.h>
#include <string.h>

#include "openzl/codecs/zl_split.h"  /* ZL_SPLIT_CHANNEL_ID */
#include "openzl/common/assertion.h" /* ZL_ASSERT */
#include "openzl/common/logging.h"   /* ZL_DLOG */
#include "openzl/compress/enc_interface.h" /* ENC_refTypedStream, ZL_Encoder_getScratchSpace */
#include "openzl/shared/portability.h" /* ZL_FORCE_INLINE */
#include "openzl/shared/varint.h"      /* ZL_varintEncode */
#include "openzl/zl_data.h"            /* ZL_Input_type */
#include "openzl/zl_input.h"           /* ZL_Input_ptr, ZL_Input_eltWidth */

/* Default minimum number of elements per segment.
 * A split is rejected if either side would have fewer elements.
 * This prevents false positives from noise within a single range
 * (e.g. a single extreme value at the start of a segment).
 * Can be overridden via ZL_SPLIT_BYRANGE_MIN_SEGMENT_SIZE_PID parameter.
 * Narrow types (u8) use a larger default for robust min/max statistics. */
#define DEFAULT_MIN_SEGMENT_SIZE 32

static inline size_t defaultMinSegSizeForWidth(size_t eltWidth)
{
    if (eltWidth <= 1)
        return 4 * DEFAULT_MIN_SEGMENT_SIZE; /* u8: 128 */
    if (eltWidth <= 2)
        return 2 * DEFAULT_MIN_SEGMENT_SIZE; /* u16: 64 */
    return DEFAULT_MIN_SEGMENT_SIZE;         /* u32/u64: 32 */
}

/* Number of blocks on each side of a candidate split that must show
 * non-overlapping, stable ranges. */
#define CONFIRMATION_WINDOW 7

/* Gap significance factor: after confirming non-overlap between left and right
 * windows, the split is accepted only if the gap (or transition block
 * amplitude) exceeds GAP_FACTOR × the typical block amplitude. This rejects
 * splits within monotonic ramps (where gap ≈ blockRange) while accepting
 * true range boundaries (where gap >> blockRange). */
#define GAP_FACTOR 2

/* Stationarity factor: a window of M blocks is "stationary" (all blocks from
 * the same range) if windowRange <= STATIONARITY_MARGIN * maxBlockRange.
 *
 * For n uniform samples from [0, R], expected block range = R*(n-1)/(n+1).
 * So the theoretical ratio windowRange/maxBlockRange → (n+1)/(n-1) ≈ 1.13
 * for n=16. In practice the ratio is even closer to 1.0 (max of M blocks
 * pushes maxBlockRange toward R). A linear ramp gives ratio ≈ M (=7).
 *
 * 1.3 provides margin above the theoretical 1.13 for noisy stationary data
 * while staying well below M. A noisy ramp needs noise > 19× the per-block
 * trend to fool this check — at that point the trend is negligible. */
#define STATIONARITY_NUM 13 /* windowRange * 10 <= maxBlockRange * 13 */
#define STATIONARITY_DEN 10 /* i.e. windowRange / maxBlockRange <= 1.3 */

/* Each minimum-sized segment spans BLOCK_DIVISOR blocks, giving
 * sub-segment resolution while keeping the block count low.
 * blockSize = minSegSize / BLOCK_DIVISOR. */
#define BLOCK_DIVISOR 2

/* Expose min block size so tests can adapt segment sizes. */
size_t ZL_splitByRange_minBlockSize(size_t eltWidth)
{
    return defaultMinSegSizeForWidth(eltWidth) / BLOCK_DIVISOR;
}

/* Read element at index as uint64_t regardless of element width.
 * When called with a compile-time constant eltWidth (via force-inlined
 * callers below), the compiler eliminates the switch entirely. */
ZL_FORCE_INLINE uint64_t readElt(const void* src, size_t eltWidth, size_t idx)
{
    switch (eltWidth) {
        case 1:
            return ((const uint8_t*)src)[idx];
        case 2:
            return ((const uint16_t*)src)[idx];
        case 4:
            return ((const uint32_t*)src)[idx];
        case 8:
            return ((const uint64_t*)src)[idx];
        default:
            ZL_ASSERT(0);
            return 0;
    }
}

/* =====================================================================
 * Phase 1: Compute per-block min/max in a single O(N) pass.
 * ===================================================================== */

ZL_FORCE_INLINE void computeBlockStats(
        const void* src,
        size_t eltWidth,
        size_t nbElts,
        size_t blockSize,
        uint64_t* blockMin,
        uint64_t* blockMax,
        size_t nbBlocks)
{
    for (size_t b = 0; b < nbBlocks; b++) {
        size_t const start = b * blockSize;
        size_t end         = start + blockSize;
        if (end > nbElts)
            end = nbElts;
        uint64_t bmin = readElt(src, eltWidth, start);
        uint64_t bmax = bmin;
        for (size_t i = start + 1; i < end; i++) {
            uint64_t v = readElt(src, eltWidth, i);
            if (v < bmin)
                bmin = v;
            if (v > bmax)
                bmax = v;
        }
        blockMin[b] = bmin;
        blockMax[b] = bmax;
    }
}

/* =====================================================================
 * Phase 2: Find confirmed split points at block granularity.
 *
 * A candidate split between blocks b and b+1 is confirmed when:
 *   - Left window [b-M+1 .. b] and right window [b+2 .. b+M+1]
 *     (skipping transition block b+1) have non-overlapping ranges.
 *   - The separation is significant (any of three signals):
 *     1. Gap: gap > GAP_FACTOR × typAmp (large discrete jump).
 *     2. TransAmp: transition block amplitude > GAP_FACTOR × typAmp
 *        (boundary falls mid-block, spanning both ranges).
 *     3. Stationarity: both windows have windowRange / maxBlockRange
 *        <= 1.3 (neither side is trending → non-overlap is a real
 *        range boundary, not a ramp artifact).
 *
 * After confirming a split at block b, set leftBound=b+2 and advance
 * b by 2 to skip the transition block. The leftBound prevents the
 * next candidate's left window from including blocks from the
 * previous segment.
 * Returns the number of confirmed splits.
 * ===================================================================== */

static size_t findConfirmedSplitBlocks(
        const uint64_t* blockMin,
        const uint64_t* blockMax,
        size_t nbBlocks,
        size_t M,
        size_t* splitBlocks,
        size_t maxSplits)
{
    size_t nbSplits = 0;
    if (M < 1 || nbBlocks < 2 * M + 1)
        return 0;

    /* Minimum left window size: M>=2 requires 2 blocks for stability
     * check (needs block pairs). M==1 works with a single block. */
    size_t const minLSize = (M >= 2) ? 2 : 1;

    /* leftBound: the earliest block that belongs to the current segment.
     * After confirming a split at block b, the next segment starts at
     * b+2 (skipping transition block b+1). This prevents the left window
     * from including blocks belonging to a previous segment, which would
     * contaminate the stability check. */
    size_t leftBound = 0;
    size_t b         = M - 1;
    while (b + M + 1 < nbBlocks && nbSplits < maxSplits) {
        /* Left window: [max(b-M+1, leftBound) .. b] */
        size_t const lStart =
                (b >= M - 1 && b - M + 1 >= leftBound) ? b - M + 1 : leftBound;
        size_t const lSize = b - lStart + 1;
        if (lSize < minLSize) {
            b++;
            continue;
        }

        uint64_t leftMin   = blockMin[lStart];
        uint64_t leftMax   = blockMax[lStart];
        uint64_t leftMaxBR = blockMax[lStart] - blockMin[lStart];
        for (size_t i = lStart + 1; i <= b; i++) {
            if (blockMin[i] < leftMin)
                leftMin = blockMin[i];
            if (blockMax[i] > leftMax)
                leftMax = blockMax[i];
            uint64_t br = blockMax[i] - blockMin[i];
            if (br > leftMaxBR)
                leftMaxBR = br;
        }

        /* Right window [b+2 .. b+M+1] (skip transition block b+1) */
        size_t const rStart = b + 2;
        size_t const rEnd   = b + M + 1;
        uint64_t rightMin   = blockMin[rStart];
        uint64_t rightMax   = blockMax[rStart];
        uint64_t rightMaxBR = blockMax[rStart] - blockMin[rStart];
        for (size_t i = rStart + 1; i <= rEnd; i++) {
            if (blockMin[i] < rightMin)
                rightMin = blockMin[i];
            if (blockMax[i] > rightMax)
                rightMax = blockMax[i];
            uint64_t br = blockMax[i] - blockMin[i];
            if (br > rightMaxBR)
                rightMaxBR = br;
        }

        /* Non-overlap check */
        int nonOverlap = (leftMax < rightMin) || (rightMax < leftMin);

        if (nonOverlap) {
            /* Significance check: accept only if the separation is
             * meaningful, using three independent signals.
             *
             * Signal 1 (gap): gap between ranges > GAP_FACTOR × typAmp.
             *   Catches boundaries aligned with block edges.
             * Signal 2 (transAmp): transition block amplitude > GAP_FACTOR ×
             * typAmp. Catches boundaries that fall mid-block. Signal 3
             * (stationarity): both windows are stationary (windowRange ≈
             * blockRange, not trending). If neither side is a ramp, non-overlap
             * proves a real range boundary. */
            uint64_t typAmp = (leftMaxBR > rightMaxBR) ? leftMaxBR : rightMaxBR;
            uint64_t gap    = (leftMax < rightMin) ? rightMin - leftMax
                                                   : leftMin - rightMax;
            uint64_t transAmp = blockMax[b + 1] - blockMin[b + 1];

            int significant = (typAmp == 0) || (gap > GAP_FACTOR * typAmp)
                    || (transAmp > GAP_FACTOR * typAmp);

            if (!significant) {
                /* Stationarity: windowRange * DEN <= maxBlockRange * NUM
                 * i.e. windowRange / maxBlockRange <= 1.3.
                 * Guard: windowRange <= UINT64_MAX / DEN avoids overflow
                 * on both sides (maxBlockRange <= windowRange always). */
                uint64_t leftRange  = leftMax - leftMin;
                uint64_t rightRange = rightMax - rightMin;
                int leftStationary  = (leftMaxBR == 0)
                        || (leftRange <= UINT64_MAX / STATIONARITY_DEN
                            && leftRange * STATIONARITY_DEN
                                    <= STATIONARITY_NUM * leftMaxBR);
                int rightStationary = (rightMaxBR == 0)
                        || (rightRange <= UINT64_MAX / STATIONARITY_DEN
                            && rightRange * STATIONARITY_DEN
                                    <= STATIONARITY_NUM * rightMaxBR);
                if (leftStationary && rightStationary)
                    significant = 1;
            }

            if (significant) {
                splitBlocks[nbSplits++] = b;
                leftBound = b + 2; /* next segment starts after transition */
                b += 2;
                continue;
            }
        }

        b++;
    }

    return nbSplits;
}

/* =====================================================================
 * Phase 3: Refine a block-level split to an exact element boundary.
 *
 * Scans the transition zone (blocks b and b+1) with a prefix/suffix
 * min-max pass, seeded with context from surrounding blocks.
 * Returns the absolute element index of the refined split point.
 * ===================================================================== */

ZL_FORCE_INLINE size_t refineSplitBoundary(
        const void* src,
        size_t eltWidth,
        size_t nbElts,
        size_t blockSize,
        size_t splitBlock,
        size_t segStartElt,
        size_t segEndBlk,
        const uint64_t* blockMin,
        const uint64_t* blockMax,
        uint64_t* suffBuf,
        uint64_t* suffBufMax)
{
    /* The transition zone is blocks [splitBlock, splitBlock+1].
     * We scan from segStartElt to transition zone end to give the
     * forward prefix full left-side statistics. Only check for gaps
     * within the transition zone (±1 block). */
    size_t transStart = (splitBlock > 0) ? (splitBlock - 1) * blockSize : 0;
    if (transStart < segStartElt)
        transStart = segStartElt;
    size_t const transEnd = (splitBlock + 3) * blockSize;
    size_t const scanEnd  = (transEnd < nbElts) ? transEnd : nbElts;
    size_t const scanStart =
            (segStartElt < transStart) ? segStartElt : transStart;

    if (scanEnd <= scanStart + 1)
        return (splitBlock + 1) * blockSize;

    /* Suffix array: only needed for the transition zone [transStart..scanEnd).
     * Elements before transStart are prefix-only (scanned but not checked). */
    size_t const transLen = scanEnd - transStart;

    /* Backward pass: suffix min/max within the transition zone */
    suffBuf[transLen - 1] = readElt(src, eltWidth, transStart + transLen - 1);
    suffBufMax[transLen - 1] = suffBuf[transLen - 1];
    for (size_t i = transLen - 1; i > 0; i--) {
        uint64_t v        = readElt(src, eltWidth, transStart + i - 1);
        suffBuf[i - 1]    = (v < suffBuf[i]) ? v : suffBuf[i];
        suffBufMax[i - 1] = (v > suffBufMax[i]) ? v : suffBufMax[i];
    }

    /* Merge suffix with right context (blocks fully after the zone) */
    size_t transEndBlk = (scanEnd + blockSize - 1) / blockSize;
    if (transEndBlk < segEndBlk) {
        uint64_t suffCtxMin = blockMin[transEndBlk];
        uint64_t suffCtxMax = blockMax[transEndBlk];
        for (size_t i = transEndBlk + 1; i < segEndBlk; i++) {
            if (blockMin[i] < suffCtxMin)
                suffCtxMin = blockMin[i];
            if (blockMax[i] > suffCtxMax)
                suffCtxMax = blockMax[i];
        }
        for (size_t i = 0; i < transLen; i++) {
            if (suffCtxMin < suffBuf[i])
                suffBuf[i] = suffCtxMin;
            if (suffCtxMax > suffBufMax[i])
                suffBufMax[i] = suffCtxMax;
        }
    }

    /* Forward pass: scan from segStartElt, but only check gaps within the
     * transition zone. The prefix accumulates ALL left-side elements. */
    uint64_t prefMin = readElt(src, eltWidth, scanStart);
    uint64_t prefMax = prefMin;
    size_t bestPos   = (splitBlock + 1) * blockSize; /* default */
    uint64_t bestGap = 0;

    for (size_t pos = scanStart + 1; pos < scanEnd; pos++) {
        /* Check for gap only within the transition zone */
        if (pos >= transStart) {
            size_t ti    = pos - transStart; /* index into suffix arrays */
            uint64_t gap = 0;
            if (prefMax < suffBuf[ti]) {
                gap = suffBuf[ti] - prefMax;
            } else if (suffBufMax[ti] < prefMin) {
                gap = prefMin - suffBufMax[ti];
            }
            if (gap > bestGap) {
                bestGap = gap;
                bestPos = pos;
            }
        }
        uint64_t v = readElt(src, eltWidth, pos);
        if (v < prefMin)
            prefMin = v;
        if (v > prefMax)
            prefMax = v;
    }

    return bestPos;
}

/* =====================================================================
 * Main boundary detection: block-based algorithm with fallback.
 *
 * FORCE_INLINE with eltWidth as parameter: when called from the thin
 * per-width shells below, the compiler constant-propagates eltWidth
 * into computeBlockStats and refineSplitBoundary (both FORCE_INLINE),
 * specializing readElt at compile time on the hot path.
 * ===================================================================== */

ZL_FORCE_INLINE void detectBoundaries_internal(
        const void* src,
        size_t eltWidth,
        size_t nbElts,
        size_t minSegSize,
        void* scratch,
        size_t* segmentSizes,
        size_t* nbSegments,
        size_t maxSegments)
{
    size_t const blockSize =
            (minSegSize / BLOCK_DIVISOR > 0) ? minSegSize / BLOCK_DIVISOR : 1;
    size_t const nbBlocks = (nbElts + blockSize - 1) / blockSize;
    size_t const M_raw    = (nbBlocks >= 3) ? (nbBlocks - 1) / 2 : 0;
    size_t const M =
            (M_raw < CONFIRMATION_WINDOW) ? M_raw : CONFIRMATION_WINDOW;

    /* Too few blocks for confirmation windows → use M=1 (no stability
     * check, just non-overlap). This is safe for small inputs where
     * there's little room for false positives.
     * With default parameters, this branch is unreachable since the
     * caller already handles nbElts < 2*minSegSize as a single segment.
     * It fires only with custom small minSegmentSize values. */
    size_t const Meff = (M >= 2 && nbBlocks >= 2 * M + 1) ? M : 1;
    if (nbBlocks < 3) {
        segmentSizes[0] = nbElts;
        *nbSegments     = 1;
        return;
    }

    /* Layout: [blockMin|blockMax|suffBuf|suffBufMax][splitBlocks] */
    size_t const maxZone = 4 * blockSize + 1;
    uint64_t* blockMin   = (uint64_t*)scratch;
    uint64_t* blockMax   = blockMin + nbBlocks;
    uint64_t* suffBuf    = blockMax + nbBlocks;
    uint64_t* suffBufMax = suffBuf + maxZone;
    size_t* splitBlocks  = (size_t*)(suffBufMax + maxZone);

    /* Phase 1 */
    computeBlockStats(
            src, eltWidth, nbElts, blockSize, blockMin, blockMax, nbBlocks);

    /* Phase 2 */
    size_t nbSplits = findConfirmedSplitBlocks(
            blockMin, blockMax, nbBlocks, Meff, splitBlocks, maxSegments - 1);

    if (nbSplits == 0) {
        segmentSizes[0] = nbElts;
        *nbSegments     = 1;
        return;
    }

    /* Phase 3: refine each split to exact element boundary */
    size_t pos  = 0;
    *nbSegments = 0;
    for (size_t s = 0; s < nbSplits; s++) {
        size_t sb = splitBlocks[s];

        /* Limit suffix context to blocks within the current segment. */
        /* The next split at block b' has transition at b'+1.         */
        /* Include blocks up to b' (same range as current segment).  */
        size_t segEndBlk =
                (s + 1 < nbSplits) ? splitBlocks[s + 1] + 1 : nbBlocks;
        if (segEndBlk > nbBlocks)
            segEndBlk = nbBlocks;

        size_t splitPos = refineSplitBoundary(
                src,
                eltWidth,
                nbElts,
                blockSize,
                sb,
                pos,
                segEndBlk,
                blockMin,
                blockMax,
                suffBuf,
                suffBufMax);
        /* Clamp to ensure minimum segment size */
        if (splitPos < pos + minSegSize)
            splitPos = pos + minSegSize;
        if (splitPos > nbElts - minSegSize) {
            continue; /* skip: remaining too small */
        }
        if (splitPos <= pos) {
            continue; /* skip: no progress */
        }

        if (*nbSegments < maxSegments) {
            segmentSizes[*nbSegments] = splitPos - pos;
        }
        (*nbSegments)++;
        pos = splitPos;
    }

    /* Final segment */
    if (*nbSegments < maxSegments) {
        segmentSizes[*nbSegments] = nbElts - pos;
    }
    (*nbSegments)++;
}

/* Thin per-width shells: call detectBoundaries_internal with a
 * compile-time constant eltWidth for full specialization. */
static void detectBoundaries_u8(
        const void* src,
        size_t nbElts,
        size_t minSegSize,
        void* scratch,
        size_t* segmentSizes,
        size_t* nbSegments,
        size_t maxSegments)
{
    detectBoundaries_internal(
            src,
            1,
            nbElts,
            minSegSize,
            scratch,
            segmentSizes,
            nbSegments,
            maxSegments);
}
static void detectBoundaries_u16(
        const void* src,
        size_t nbElts,
        size_t minSegSize,
        void* scratch,
        size_t* segmentSizes,
        size_t* nbSegments,
        size_t maxSegments)
{
    detectBoundaries_internal(
            src,
            2,
            nbElts,
            minSegSize,
            scratch,
            segmentSizes,
            nbSegments,
            maxSegments);
}
static void detectBoundaries_u32(
        const void* src,
        size_t nbElts,
        size_t minSegSize,
        void* scratch,
        size_t* segmentSizes,
        size_t* nbSegments,
        size_t maxSegments)
{
    detectBoundaries_internal(
            src,
            4,
            nbElts,
            minSegSize,
            scratch,
            segmentSizes,
            nbSegments,
            maxSegments);
}
static void detectBoundaries_u64(
        const void* src,
        size_t nbElts,
        size_t minSegSize,
        void* scratch,
        size_t* segmentSizes,
        size_t* nbSegments,
        size_t maxSegments)
{
    detectBoundaries_internal(
            src,
            8,
            nbElts,
            minSegSize,
            scratch,
            segmentSizes,
            nbSegments,
            maxSegments);
}

/* Dispatch to width-specialized variant */
static void detectBoundaries(
        const void* src,
        size_t eltWidth,
        size_t nbElts,
        size_t minSegSize,
        void* scratch,
        size_t scratchSize,
        size_t* segmentSizes,
        size_t* nbSegments,
        size_t maxSegments)
{
    (void)scratchSize;
    switch (eltWidth) {
        case 1:
            detectBoundaries_u8(
                    src,
                    nbElts,
                    minSegSize,
                    scratch,
                    segmentSizes,
                    nbSegments,
                    maxSegments);
            return;
        case 2:
            detectBoundaries_u16(
                    src,
                    nbElts,
                    minSegSize,
                    scratch,
                    segmentSizes,
                    nbSegments,
                    maxSegments);
            return;
        case 4:
            detectBoundaries_u32(
                    src,
                    nbElts,
                    minSegSize,
                    scratch,
                    segmentSizes,
                    nbSegments,
                    maxSegments);
            return;
        case 8:
            detectBoundaries_u64(
                    src,
                    nbElts,
                    minSegSize,
                    scratch,
                    segmentSizes,
                    nbSegments,
                    maxSegments);
            return;
        default:
            ZL_ASSERT(0);
    }
}

ZL_Report
EI_split_byrange(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);

    size_t const nbElts   = ZL_Input_numElts(in);
    size_t const eltWidth = ZL_Input_eltWidth(in);
    ZL_ASSERT(eltWidth == 1 || eltWidth == 2 || eltWidth == 4 || eltWidth == 8);

    /* Read optional min segment size parameter, default is width-dependent */
    size_t minSegSize = defaultMinSegSizeForWidth(eltWidth);
    {
        ZL_IntParam const mss = ZL_Encoder_getLocalIntParam(
                eictx, ZL_SPLIT_BYRANGE_MIN_SEGMENT_SIZE_PID);
        if (mss.paramId == ZL_SPLIT_BYRANGE_MIN_SEGMENT_SIZE_PID) {
            ZL_ASSERT(mss.paramValue > 0);
            minSegSize = (size_t)mss.paramValue;
        }
    }

    ZL_DLOG(BLOCK,
            "EI_split_byrange (input:%zu elts, width:%zu, minSegSize:%zu)",
            nbElts,
            eltWidth,
            minSegSize);

    /* Handle empty input: same as splitN empty case */
    if (nbElts == 0) {
        if (eltWidth != 1) {
            uint8_t header[ZL_VARINT_LENGTH_64];
            size_t const headerSize = ZL_varintEncode(eltWidth, header);
            ZL_Encoder_sendCodecHeader(eictx, header, headerSize);
        }
        return ZL_returnSuccess();
    }

    /* If too few elements for any split, output as a single segment */
    if (nbElts < 2 * minSegSize) {
        ZL_Output* const s =
                ENC_refTypedStream(eictx, 0, eltWidth, nbElts, in, 0);
        ZL_ERR_IF_NULL(s, allocation);
        ZL_ERR_IF_ERR(ZL_Output_setIntMetadata(s, ZL_SPLIT_CHANNEL_ID, 0));
        return ZL_returnSuccess();
    }

    /* Compute block parameters */
    size_t const blockSize =
            (minSegSize / BLOCK_DIVISOR > 0) ? minSegSize / BLOCK_DIVISOR : 1;
    size_t const nbBlocks = (nbElts + blockSize - 1) / blockSize;
    size_t const maxZone  = 4 * blockSize + 1;

    /* Allocate scratch for block algorithm:
     *   blockMin[nb] + blockMax[nb] + suffBuf[maxZone] + suffBufMax[maxZone]
     *   + splitBlocks[nb] + segmentSizes[nb+1]
     * uint64_t arrays first, then size_t arrays, to keep alignment. */
    size_t const nU64 =
            nbBlocks * 2 + maxZone * 2; /* blockMin/Max + suffBuf/Max */
    /* size_t region */
    size_t const nSizeT = nbBlocks /* splitBlocks */
            + (nbBlocks + 1);      /* segmentSizes */
    size_t const totalScratch =
            nU64 * sizeof(uint64_t) + nSizeT * sizeof(size_t);

    void* scratch = ZL_Encoder_getScratchSpace(eictx, totalScratch);
    ZL_ERR_IF_NULL(scratch, allocation);

    size_t* segmentSizes = (size_t*)((uint64_t*)scratch + nU64)
            + nbBlocks; /* after splitBlocks */

    /* Detect range boundaries */
    size_t nbSegments = 0;
    detectBoundaries(
            ZL_Input_ptr(in),
            eltWidth,
            nbElts,
            minSegSize,
            scratch,
            totalScratch,
            segmentSizes,
            &nbSegments,
            nbBlocks + 1);

    ZL_DLOG(BLOCK, "EI_split_byrange: detected %zu segments", nbSegments);

    /* Emit segments (same mechanism as EI_splitN) */
    size_t pos = 0;
    ZL_ASSERT_LT(nbSegments, INT_MAX);
    for (size_t n = 0; n < nbSegments; n++) {
        size_t segSize = segmentSizes[n];
        ZL_DLOG(SEQ, "EI_split_byrange: segment %zu of size %zu", n, segSize);
        ZL_Output* const s = ENC_refTypedStream(
                eictx, 0, eltWidth, segSize, in, pos * eltWidth);
        ZL_ERR_IF_NULL(s, allocation);
        ZL_ERR_IF_ERR(ZL_Output_setIntMetadata(s, ZL_SPLIT_CHANNEL_ID, (int)n));
        pos += segSize;
    }
    ZL_ASSERT_EQ(pos, nbElts);

    return ZL_returnSuccess();
}
