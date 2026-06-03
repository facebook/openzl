// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "decode_sparse_num_kernel.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint32_t
ZL_sparseNumReadDistance(const void* src, size_t index, size_t width)
{
    const void* const ptr = (const uint8_t*)src + index * width;
    assert(ZL_sparseNumIsAlignedForWidth(ptr, width));
    switch (width) {
        case 1:
            return *(const uint8_t*)ptr;
        case 2:
            return *(const uint16_t*)ptr;
        case 4:
            return *(const uint32_t*)ptr;
        default:
            assert(false);
            return 0;
    }
}

static inline bool ZL_sparseNumDecodeCanUseUncheckedOutputCountSum(
        size_t numDistances,
        size_t numValues)
{
    /*
     * If size_t is at least 64-bit and both stream counts fit in 32 bits, the
     * output count cannot overflow size_t:
     * UINT32_MAX values + UINT32_MAX distances each worth UINT32_MAX zeros
     * is 2^64 - 2^32, which is still below SIZE_MAX.
     */
    if (sizeof(size_t) < 8) {
        return false;
    }
    return numDistances <= UINT32_MAX && numValues <= UINT32_MAX;
}

size_t ZL_sparseNumDecodeOutputCount(
        const void* distances,
        size_t numDistances,
        size_t distanceWidth,
        size_t numValues)
{
    assert(ZL_sparseNumValidDistanceWidth(distanceWidth));

    size_t count = numValues;
    if (ZL_sparseNumDecodeCanUseUncheckedOutputCountSum(
                numDistances, numValues)) {
        for (size_t i = 0; i < numDistances; ++i) {
            count += ZL_sparseNumReadDistance(distances, i, distanceWidth);
        }

        return count;
    }

    if (count == ZL_SPARSE_NUM_DECODE_OUTPUT_COUNT_ERROR) {
        return ZL_SPARSE_NUM_DECODE_OUTPUT_COUNT_ERROR;
    }

    for (size_t i = 0; i < numDistances; ++i) {
        uint32_t const distance =
                ZL_sparseNumReadDistance(distances, i, distanceWidth);
        if ((size_t)distance
            >= ZL_SPARSE_NUM_DECODE_OUTPUT_COUNT_ERROR - count) {
            return ZL_SPARSE_NUM_DECODE_OUTPUT_COUNT_ERROR;
        }
        count += (size_t)distance;
    }

    return count;
}

static inline bool ZL_sparseNumDecodeWriteInBounds(
        size_t expectedDstSize,
        size_t producedBytes,
        size_t writeBytes)
{
    if (producedBytes > expectedDstSize) {
        return false;
    }
    if (writeBytes > expectedDstSize - producedBytes) {
        return false;
    }
    return true;
}

static inline void ZL_sparseNumDecodeTrackWrite(
        size_t expectedDstSize,
        size_t* producedBytes,
        size_t writeBytes)
{
    bool const ok = ZL_sparseNumDecodeWriteInBounds(
            expectedDstSize, *producedBytes, writeBytes);
    assert(ok);
    (void)ok;
    *producedBytes += writeBytes;
}

static inline bool ZL_sparseNumDecodeComplete(
        size_t expectedDstSize,
        size_t producedBytes)
{
    return producedBytes == expectedDstSize;
}

static inline void ZL_sparseNumDecodeBody(
        void* dst,
        size_t expectedDstSize,
        const void* distances,
        size_t numDistances,
        size_t distanceWidth,
        const void* values,
        size_t numValues,
        size_t valueWidth)
{
    uint8_t* out                = (uint8_t*)dst;
    const uint8_t* const vals   = (const uint8_t*)values;
    bool const hasFinalDistance = numDistances == numValues + 1;
    size_t producedBytes        = 0;
    (void)expectedDstSize;

    for (size_t i = 0; i < numValues; ++i) {
        uint32_t const distance =
                ZL_sparseNumReadDistance(distances, i, distanceWidth);
        assert((size_t)distance <= SIZE_MAX / valueWidth);
        size_t const zeroBytes = (size_t)distance * valueWidth;
        ZL_sparseNumDecodeTrackWrite(
                expectedDstSize, &producedBytes, zeroBytes);
        memset(out, 0, zeroBytes);
        out += zeroBytes;

        ZL_sparseNumDecodeTrackWrite(
                expectedDstSize, &producedBytes, valueWidth);
        ZL_sparseNumCopyAlignedValue(out, vals + i * valueWidth, valueWidth);
        out += valueWidth;
    }

    if (hasFinalDistance) {
        uint32_t const distance =
                ZL_sparseNumReadDistance(distances, numValues, distanceWidth);
        assert((size_t)distance <= SIZE_MAX / valueWidth);
        size_t const zeroBytes = (size_t)distance * valueWidth;
        ZL_sparseNumDecodeTrackWrite(
                expectedDstSize, &producedBytes, zeroBytes);
        memset(out, 0, zeroBytes);
    }

    assert(ZL_sparseNumDecodeComplete(expectedDstSize, producedBytes));
}

static inline void ZL_sparseNumDecodeD8V1(
        void* dst,
        size_t expectedDstSize,
        const void* distances,
        size_t numDistances,
        const void* values,
        size_t numValues)
{
    ZL_sparseNumDecodeBody(
            dst,
            expectedDstSize,
            distances,
            numDistances,
            1,
            values,
            numValues,
            1);
}

static inline void ZL_sparseNumDecodeD8V2(
        void* dst,
        size_t expectedDstSize,
        const void* distances,
        size_t numDistances,
        const void* values,
        size_t numValues)
{
    ZL_sparseNumDecodeBody(
            dst,
            expectedDstSize,
            distances,
            numDistances,
            1,
            values,
            numValues,
            2);
}

static inline void ZL_sparseNumDecodeD8V4(
        void* dst,
        size_t expectedDstSize,
        const void* distances,
        size_t numDistances,
        const void* values,
        size_t numValues)
{
    ZL_sparseNumDecodeBody(
            dst,
            expectedDstSize,
            distances,
            numDistances,
            1,
            values,
            numValues,
            4);
}

static inline void ZL_sparseNumDecodeD8V8(
        void* dst,
        size_t expectedDstSize,
        const void* distances,
        size_t numDistances,
        const void* values,
        size_t numValues)
{
    ZL_sparseNumDecodeBody(
            dst,
            expectedDstSize,
            distances,
            numDistances,
            1,
            values,
            numValues,
            8);
}

void ZL_sparseNumDecode(
        void* dst,
        size_t expectedDstSize,
        const void* distances,
        size_t numDistances,
        size_t distanceWidth,
        const void* values,
        size_t numValues,
        size_t valueWidth)
{
    assert(ZL_sparseNumValidValueWidth(valueWidth));
    assert(ZL_sparseNumValidDistanceWidth(distanceWidth));
    assert(numDistances == numValues
           || (numValues < SIZE_MAX && numDistances == numValues + 1));

    if (distanceWidth == 1) {
        switch (valueWidth) {
            case 1:
                ZL_sparseNumDecodeD8V1(
                        dst,
                        expectedDstSize,
                        distances,
                        numDistances,
                        values,
                        numValues);
                return;
            case 2:
                ZL_sparseNumDecodeD8V2(
                        dst,
                        expectedDstSize,
                        distances,
                        numDistances,
                        values,
                        numValues);
                return;
            case 4:
                ZL_sparseNumDecodeD8V4(
                        dst,
                        expectedDstSize,
                        distances,
                        numDistances,
                        values,
                        numValues);
                return;
            case 8:
                ZL_sparseNumDecodeD8V8(
                        dst,
                        expectedDstSize,
                        distances,
                        numDistances,
                        values,
                        numValues);
                return;
            default:
                assert(false);
                return;
        }
    }

    ZL_sparseNumDecodeBody(
            dst,
            expectedDstSize,
            distances,
            numDistances,
            distanceWidth,
            values,
            numValues,
            valueWidth);
}
