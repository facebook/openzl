// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "decode_sparse_num_kernel.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/*
 * Distances are numeric stream elements, so the binding guarantees alignment
 * for their element width. Use typed loads instead of byte copies to keep the
 * kernel dependency-free and to match the numeric-stream host-endian contract.
 */
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

/*
 * Most builds use 64-bit size_t. In that case, when both stream counts fit in
 * 32 bits, summing all 32-bit distances plus all literals cannot overflow.
 * This avoids a branch inside the common output-count loop while keeping a
 * checked fallback for 32-bit platforms and oversized counts.
 */
static inline bool ZL_sparseNumDecodeCanUseUncheckedOutputCountSum(
        size_t numDistances,
        size_t numLiterals)
{
    if (sizeof(size_t) < 8) {
        return false;
    }
    return numDistances <= UINT32_MAX && numLiterals <= UINT32_MAX;
}

/*
 * Compute the reconstructed element count before allocation. The public error
 * value is SIZE_MAX, so the checked path rejects both real overflow and the
 * otherwise-valid SIZE_MAX count, keeping the API a single-value return.
 */
size_t ZL_sparseNumDecodeOutputCount(
        const void* distances,
        size_t numDistances,
        size_t distanceWidth,
        size_t numLiterals)
{
    assert(ZL_sparseNumValidDistanceWidth(distanceWidth));

    size_t count = numLiterals;
    if (ZL_sparseNumDecodeCanUseUncheckedOutputCountSum(
                numDistances, numLiterals)) {
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

/*
 * Debug-only write accounting helper. The binding allocates the destination
 * from ZL_sparseNumDecodeOutputCount(), so the kernel does not perform runtime
 * bounds recovery. These predicates exist to assert, in debug builds, that the
 * write plan still matches the caller-computed destination size.
 */
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

/*
 * Account for every planned output write. The produced byte counter advances
 * unconditionally, not inside assert(), so release builds keep the same control
 * flow and future missing accounting remains visible to the final completeness
 * assertion in debug builds.
 */
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

/*
 * Completeness check paired with ZL_sparseNumDecodeTrackWrite(). It catches
 * both under-writing and over-writing in debug builds when a future edit
 * changes the decode loop's write structure.
 */
static inline ZL_UNUSED bool ZL_sparseNumDecodeComplete(
        size_t expectedDstSize,
        size_t producedBytes)
{
    return producedBytes == expectedDstSize;
}

/*
 * Emit one run before a literal, or the optional final run. Dominant value 0 is
 * the sparse-zero mode and maps directly to memset(0), preserving the common
 * case.
 */
static inline void ZL_sparseNumDecodeWriteRun(
        void* dst,
        uint64_t dominant,
        uint32_t distance,
        size_t runBytes,
        size_t valueWidth)
{
    if (dominant == 0) {
        memset(dst, 0, runBytes);
        return;
    }

    assert(dst != NULL);
    assert(ZL_sparseNumIsAlignedForWidth(dst, valueWidth));

    switch (valueWidth) {
        case 1: {
            memset(dst, (uint8_t)dominant, distance);
            return;
        }
        case 2: {
            uint16_t* const out  = (uint16_t*)dst;
            uint16_t const value = (uint16_t)dominant;
            for (uint32_t i = 0; i < distance; ++i) {
                out[i] = value;
            }
            return;
        }
        case 4: {
            uint32_t* const out  = (uint32_t*)dst;
            uint32_t const value = (uint32_t)dominant;
            for (uint32_t i = 0; i < distance; ++i) {
                out[i] = value;
            }
            return;
        }
        case 8: {
            uint64_t* const out = (uint64_t*)dst;
            for (uint32_t i = 0; i < distance; ++i) {
                out[i] = dominant;
            }
            return;
        }
        default:
            assert(false);
            return;
    }
}

/*
 * Shared decode loop. Each distance describes how many dominant symbols precede
 * the next literal; an optional final distance describes the trailing dominant
 * run. Keeping zero and non-zero dominant modes in one body avoids duplicating
 * the distance/literal/write-accounting logic.
 */
static inline void ZL_sparseNumDecodeBody(
        void* dst,
        size_t expectedDstSize,
        const void* distances,
        size_t numDistances,
        size_t distanceWidth,
        uint64_t dominant,
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
        size_t const runBytes = (size_t)distance * valueWidth;
        ZL_sparseNumDecodeTrackWrite(expectedDstSize, &producedBytes, runBytes);
        ZL_sparseNumDecodeWriteRun(
                out, dominant, distance, runBytes, valueWidth);
        out += runBytes;

        ZL_sparseNumDecodeTrackWrite(
                expectedDstSize, &producedBytes, valueWidth);
        ZL_sparseNumCopyAlignedValue(out, vals + i * valueWidth, valueWidth);
        out += valueWidth;
    }

    if (hasFinalDistance) {
        uint32_t const distance =
                ZL_sparseNumReadDistance(distances, numValues, distanceWidth);
        assert((size_t)distance <= SIZE_MAX / valueWidth);
        size_t const runBytes = (size_t)distance * valueWidth;
        ZL_sparseNumDecodeTrackWrite(expectedDstSize, &producedBytes, runBytes);
        ZL_sparseNumDecodeWriteRun(
                out, dominant, distance, runBytes, valueWidth);
    }

    assert(ZL_sparseNumDecodeComplete(expectedDstSize, producedBytes));
}

/*
 * D8 is the expected distance width for normal sparse_num data. These shells
 * force both distance width and value width to compile-time constants in the
 * zero-dominant hot path, while leaving uncommon wider distances on the generic
 * body.
 */
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
            0,
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
            0,
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
            0,
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
            0,
            values,
            numValues,
            8);
}

/*
 * Public kernel entry point. Dominant value 0 keeps the specialized sparse-zero
 * dispatch; non-zero dominant values use the generic shared body.
 */
void ZL_sparseNumDecode(
        void* dst,
        size_t expectedDstSize,
        const void* distances,
        size_t numDistances,
        size_t distanceWidth,
        uint64_t dominant,
        const void* values,
        size_t numValues,
        size_t valueWidth)
{
    assert(ZL_sparseNumValidValueWidth(valueWidth));
    assert(ZL_sparseNumValidDistanceWidth(distanceWidth));
    assert(numDistances == numValues
           || (numValues < SIZE_MAX && numDistances == numValues + 1));

    if (dominant == 0 && distanceWidth == 1) {
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
            dominant,
            values,
            numValues,
            valueWidth);
}
