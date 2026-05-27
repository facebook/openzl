// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "encode_sparse_num_kernel.h"

#include <assert.h>

static inline bool ZL_sparseNumIsZero(const void* ptr, size_t width)
{
    const uint8_t* const bytes = (const uint8_t*)ptr;
    for (size_t i = 0; i < width; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
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

static ZL_SparseNumEncodeInfo ZL_sparseNumComputeEncodeInfo_internal(
        const void* src,
        size_t numElts,
        size_t valueWidth)
{
    assert(ZL_sparseNumValidValueWidth(valueWidth));

    const uint8_t* const bytes = (const uint8_t*)src;
    uint32_t zeroRun           = 0;
    uint32_t maxDistance       = 0;
    size_t numValues           = 0;
    size_t numDistances        = 0;

    for (size_t i = 0; i < numElts; ++i) {
        const uint8_t* const elt = bytes + i * valueWidth;
        if (ZL_sparseNumIsZero(elt, valueWidth)) {
            if (zeroRun == UINT32_MAX) {
                maxDistance = UINT32_MAX;
                zeroRun     = 0;
                ++numValues;
                ++numDistances;
                continue;
            }
            ++zeroRun;
            continue;
        }

        if (zeroRun > maxDistance) {
            maxDistance = zeroRun;
        }
        zeroRun = 0;
        ++numValues;
        ++numDistances;
    }

    if (zeroRun > 0) {
        if (zeroRun > maxDistance) {
            maxDistance = zeroRun;
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
        size_t valueWidth)
{
    switch (valueWidth) {
        case 1:
            return ZL_sparseNumComputeEncodeInfo_internal(src, numElts, 1);
        case 2:
            return ZL_sparseNumComputeEncodeInfo_internal(src, numElts, 2);
        case 4:
            return ZL_sparseNumComputeEncodeInfo_internal(src, numElts, 4);
        case 8:
            return ZL_sparseNumComputeEncodeInfo_internal(src, numElts, 8);
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
        bool splitFullRun)
{
    assert(ZL_sparseNumValidValueWidth(valueWidth));
    assert(ZL_sparseNumValidDistanceWidth(distanceWidth));
    assert(!splitFullRun || distanceWidth == 4);

    size_t index   = 0;
    size_t elts    = 0;
    size_t zeroRun = 0;

    for (size_t i = 0; i < numElts; ++i) {
        const uint8_t* const elt = source + i * valueWidth;
        if (ZL_sparseNumIsZero(elt, valueWidth)) {
            if (splitFullRun && zeroRun == UINT32_MAX) {
                ZL_sparseNumWriteDistance(
                        distances, index, UINT32_MAX, distanceWidth);
                ZL_sparseNumCopyAlignedValue(
                        valueDst + index * valueWidth, elt, valueWidth);
                ++index;
                elts    = i + 1;
                zeroRun = 0;
                continue;
            }
            ++zeroRun;
            continue;
        }

        assert(zeroRun <= UINT32_MAX);
        ZL_sparseNumWriteDistance(
                distances, index, (uint32_t)zeroRun, distanceWidth);
        ZL_sparseNumCopyAlignedValue(
                valueDst + index * valueWidth, elt, valueWidth);
        ++index;
        elts += zeroRun + 1;
        zeroRun = 0;
    }

    /* write the final zero run, if it exists */
    if (elts < numElts) {
        assert(elts <= numElts);
        const size_t lastZeroRun = numElts - elts;
        assert(lastZeroRun <= UINT32_MAX);
        ZL_sparseNumWriteDistance(
                distances, index, (uint32_t)lastZeroRun, distanceWidth);
    }
}

void ZL_sparseNumEncode(
        void* distances,
        void* values,
        const void* src,
        size_t numElts,
        size_t valueWidth,
        size_t distanceWidth)
{
    assert(ZL_sparseNumValidValueWidth(valueWidth));
    assert(ZL_sparseNumValidDistanceWidth(distanceWidth));

    const uint8_t* const source = (const uint8_t*)src;
    uint8_t* const valueDst     = (uint8_t*)values;

    if (distanceWidth == 1) {
        switch (valueWidth) {
            case 1:
                ZL_sparseNumEncode_internal(
                        distances, valueDst, source, numElts, 1, 1, false);
                return;
            case 2:
                ZL_sparseNumEncode_internal(
                        distances, valueDst, source, numElts, 2, 1, false);
                return;
            case 4:
                ZL_sparseNumEncode_internal(
                        distances, valueDst, source, numElts, 4, 1, false);
                return;
            case 8:
                ZL_sparseNumEncode_internal(
                        distances, valueDst, source, numElts, 8, 1, false);
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
            distanceWidth == 4);
}
