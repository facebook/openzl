// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "encode_sparse_num_kernel.h"

#include <assert.h>

#define ZL_SPARSE_NUM_DOMINANT_SAMPLE_MAX 4096

static uint64_t ZL_sparseNumReadValue(const void* ptr, size_t width)
{
    assert(ZL_sparseNumIsAlignedForWidth(ptr, width));
    switch (width) {
        case 1:
            return *(const uint8_t*)ptr;
        case 2:
            return *(const uint16_t*)ptr;
        case 4:
            return *(const uint32_t*)ptr;
        case 8:
            return *(const uint64_t*)ptr;
        default:
            assert(false);
            return 0;
    }
}

uint64_t
ZL_sparseNumSelectDominant(const void* src, size_t numElts, size_t valueWidth)
{
    assert(ZL_sparseNumValidValueWidth(valueWidth));

    const uint8_t* const bytes = (const uint8_t*)src;
    size_t const sampleElts    = numElts < ZL_SPARSE_NUM_DOMINANT_SAMPLE_MAX
               ? numElts
               : ZL_SPARSE_NUM_DOMINANT_SAMPLE_MAX;
    uint64_t candidate         = 0;
    size_t votes               = 0;

    for (size_t i = 0; i < sampleElts; ++i) {
        uint64_t const value =
                ZL_sparseNumReadValue(bytes + i * valueWidth, valueWidth);
        if (votes == 0) {
            candidate = value;
            votes     = 1;
            continue;
        }
        if (value == candidate) {
            ++votes;
        } else {
            --votes;
        }
    }

    return votes == 0 ? 0 : candidate;
}

static void ZL_sparseNumWriteValue(void* ptr, size_t width, uint64_t value)
{
    assert(ZL_sparseNumIsAlignedForWidth(ptr, width));
    switch (width) {
        case 1:
            *(uint8_t*)ptr = (uint8_t)value;
            return;
        case 2:
            *(uint16_t*)ptr = (uint16_t)value;
            return;
        case 4:
            *(uint32_t*)ptr = (uint32_t)value;
            return;
        case 8:
            *(uint64_t*)ptr = value;
            return;
        default:
            assert(false);
            return;
    }
}

static inline bool ZL_sparseNumIsZero(const void* ptr, size_t width)
{
    assert(ZL_sparseNumIsAlignedForWidth(ptr, width));
    switch (width) {
        case 1:
            return *(const uint8_t*)ptr == 0;
        case 2:
            return *(const uint16_t*)ptr == 0;
        case 4:
            return *(const uint32_t*)ptr == 0;
        case 8:
            return *(const uint64_t*)ptr == 0;
        default:
            assert(false);
            return false;
    }
}

static inline bool
ZL_sparseNumMatchesDominant(const void* ptr, size_t width, uint64_t dominant)
{
    return dominant == 0 ? ZL_sparseNumIsZero(ptr, width)
                         : ZL_sparseNumReadValue(ptr, width) == dominant;
}

static size_t ZL_sparseNumDistanceWidth(uint32_t maxDistance)
{
    if (maxDistance <= UINT8_MAX) {
        return 1;
    }
    if (maxDistance <= UINT16_MAX) {
        return 2;
    }
    return 4;
}

static void ZL_sparseNumWriteDistance(
        void* dst,
        size_t index,
        uint32_t distance,
        size_t width)
{
    void* const ptr = (uint8_t*)dst + index * width;
    assert(ZL_sparseNumIsAlignedForWidth(ptr, width));
    switch (width) {
        case 1:
            *(uint8_t*)ptr = (uint8_t)distance;
            return;
        case 2:
            *(uint16_t*)ptr = (uint16_t)distance;
            return;
        case 4:
            *(uint32_t*)ptr = distance;
            return;
        default:
            assert(false);
            return;
    }
}

/*
 * Shared encode-info scan. The zero-dominant callers still dispatch by
 * valueWidth before entering this body; the non-zero path is intentionally
 * generic until benchmark data justifies more specializations.
 */
static inline ZL_SparseNumEncodeInfo ZL_sparseNumComputeEncodeInfo_internal(
        const void* src,
        size_t numElts,
        size_t valueWidth,
        uint64_t dominant)
{
    assert(ZL_sparseNumValidValueWidth(valueWidth));

    const uint8_t* const bytes = (const uint8_t*)src;
    uint32_t run               = 0;
    uint32_t maxDistance       = 0;
    size_t numValues           = 0;
    size_t numDistances        = 0;

    for (size_t i = 0; i < numElts; ++i) {
        const uint8_t* const elt = bytes + i * valueWidth;
        if (ZL_sparseNumMatchesDominant(elt, valueWidth, dominant)) {
            if (run == UINT32_MAX) {
                maxDistance = UINT32_MAX;
                run         = 0;
                ++numValues;
                ++numDistances;
                continue;
            }
            ++run;
            continue;
        }

        if (run > maxDistance) {
            maxDistance = run;
        }
        run = 0;
        ++numValues;
        ++numDistances;
    }

    if (run > 0) {
        if (run > maxDistance) {
            maxDistance = run;
        }
        ++numDistances;
    }

    return (ZL_SparseNumEncodeInfo){
        .numDistances  = numDistances,
        .numValues     = numValues,
        .distanceWidth = ZL_sparseNumDistanceWidth(maxDistance),
    };
}

ZL_SparseNumEncodeInfo ZL_sparseNumComputeEncodeInfo(
        const void* src,
        size_t numElts,
        size_t valueWidth,
        uint64_t dominant)
{
    if (dominant == 0) {
        switch (valueWidth) {
            case 1:
                return ZL_sparseNumComputeEncodeInfo_internal(
                        src, numElts, 1, 0);
            case 2:
                return ZL_sparseNumComputeEncodeInfo_internal(
                        src, numElts, 2, 0);
            case 4:
                return ZL_sparseNumComputeEncodeInfo_internal(
                        src, numElts, 4, 0);
            case 8:
                return ZL_sparseNumComputeEncodeInfo_internal(
                        src, numElts, 8, 0);
            default:
                assert(false);
                return (ZL_SparseNumEncodeInfo){ 0 };
        }
    }

    switch (valueWidth) {
        case 1:
            return ZL_sparseNumComputeEncodeInfo_internal(
                    src, numElts, 1, dominant);
        case 2:
            return ZL_sparseNumComputeEncodeInfo_internal(
                    src, numElts, 2, dominant);
        case 4:
            return ZL_sparseNumComputeEncodeInfo_internal(
                    src, numElts, 4, dominant);
        case 8:
            return ZL_sparseNumComputeEncodeInfo_internal(
                    src, numElts, 8, dominant);
        default:
            assert(false);
            return (ZL_SparseNumEncodeInfo){ 0 };
    }
}

static inline void ZL_sparseNumEncode_internal(
        void* distances,
        uint8_t* valueDst,
        const uint8_t* source,
        size_t numElts,
        size_t valueWidth,
        size_t distanceWidth,
        uint64_t dominant,
        bool splitFullRun)
{
    assert(ZL_sparseNumValidValueWidth(valueWidth));
    assert(ZL_sparseNumValidDistanceWidth(distanceWidth));
    assert(!splitFullRun || distanceWidth == 4);

    size_t index = 0;
    size_t elts  = 0;
    size_t run   = 0;

    for (size_t i = 0; i < numElts; ++i) {
        const uint8_t* const elt = source + i * valueWidth;
        if (ZL_sparseNumMatchesDominant(elt, valueWidth, dominant)) {
            if (splitFullRun && run == UINT32_MAX) {
                ZL_sparseNumWriteDistance(
                        distances, index, UINT32_MAX, distanceWidth);
                if (dominant == 0) {
                    ZL_sparseNumCopyAlignedValue(
                            valueDst + index * valueWidth, elt, valueWidth);
                } else {
                    ZL_sparseNumWriteValue(
                            valueDst + index * valueWidth,
                            valueWidth,
                            dominant);
                }
                ++index;
                elts = i + 1;
                run  = 0;
                continue;
            }
            ++run;
            continue;
        }

        assert(run <= UINT32_MAX);
        ZL_sparseNumWriteDistance(
                distances, index, (uint32_t)run, distanceWidth);
        ZL_sparseNumCopyAlignedValue(
                valueDst + index * valueWidth, elt, valueWidth);
        ++index;
        elts += run + 1;
        run = 0;
    }

    /* write the final dominant-symbol run, if it exists */
    if (elts < numElts) {
        assert(elts <= numElts);
        const size_t lastRun = numElts - elts;
        assert(lastRun <= UINT32_MAX);
        ZL_sparseNumWriteDistance(
                distances, index, (uint32_t)lastRun, distanceWidth);
    }
}

void ZL_sparseNumEncode(
        void* distances,
        void* values,
        const void* src,
        size_t numElts,
        size_t valueWidth,
        size_t distanceWidth,
        uint64_t dominant)
{
    assert(ZL_sparseNumValidValueWidth(valueWidth));
    assert(ZL_sparseNumValidDistanceWidth(distanceWidth));

    const uint8_t* const source = (const uint8_t*)src;
    uint8_t* const valueDst     = (uint8_t*)values;

    if (dominant == 0 && distanceWidth == 1) {
        switch (valueWidth) {
            case 1:
                ZL_sparseNumEncode_internal(
                        distances, valueDst, source, numElts, 1, 1, 0, false);
                return;
            case 2:
                ZL_sparseNumEncode_internal(
                        distances, valueDst, source, numElts, 2, 1, 0, false);
                return;
            case 4:
                ZL_sparseNumEncode_internal(
                        distances, valueDst, source, numElts, 4, 1, 0, false);
                return;
            case 8:
                ZL_sparseNumEncode_internal(
                        distances, valueDst, source, numElts, 8, 1, 0, false);
                return;
            default:
                assert(false);
                return;
        }
    }

    /*
     * Wider distance streams are uncommon for sparse_num's intended inputs, so
     * keep them on one generic path for now. This path is expected to be slower
     * because valueWidth and distanceWidth are not compile-time constants. If a
     * workload needs faster D16 or D32 encoding, add their specialization.
     */
    ZL_sparseNumEncode_internal(
            distances,
            valueDst,
            source,
            numElts,
            valueWidth,
            distanceWidth,
            dominant,
            distanceWidth == 4);
}
