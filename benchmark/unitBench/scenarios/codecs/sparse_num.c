// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/unitBench/scenarios/codecs/sparse_num.h"

#include <stdint.h>
#include <stdlib.h>

#include "openzl/codecs/sparse_num/decode_sparse_num_kernel.h"
#include "openzl/codecs/sparse_num/encode_sparse_num_kernel.h"

static size_t
sparseNumEncodeOutSize(const void* src, size_t srcSize, size_t valueWidth)
{
    (void)src;
    size_t const numElts = srcSize / valueWidth;
    if (numElts > (SIZE_MAX - 4) / (valueWidth + 4)) {
        abort();
    }
    return numElts * (valueWidth + 4) + 4;
}

static size_t sparseNumEncode(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        size_t valueWidth)
{
    size_t const numElts = srcSize / valueWidth;
    ZL_SparseNumEncodeInfo const info =
            ZL_sparseNumComputeEncodeInfo(src, numElts, valueWidth);

    size_t const distanceBytes = info.numDistances * info.distanceWidth;
    size_t const valueBytes    = info.numValues * valueWidth;
    if (info.distanceWidth != 1) {
        abort();
    }
    if (valueBytes > dstCapacity || distanceBytes > dstCapacity - valueBytes) {
        abort();
    }

    /*
     * unitBench provides one flat output buffer. Place values first so the
     * value stream keeps the buffer base alignment required by the kernel.
     * Distances are D8 in these scenarios, so they do not need extra alignment.
     */
    ZL_sparseNumEncode(
            (uint8_t*)dst + valueBytes,
            dst,
            src,
            numElts,
            valueWidth,
            info.distanceWidth);

    return distanceBytes + valueBytes;
}

#define DEFINE_SPARSE_NUM_ENCODE(bits, width)                               \
    size_t sparseNumEncode##bits##_outSize(const void* src, size_t srcSize) \
    {                                                                       \
        return sparseNumEncodeOutSize(src, srcSize, width);                 \
    }                                                                       \
                                                                            \
    size_t sparseNumEncode##bits##_wrapper(                                 \
            const void* src,                                                \
            size_t srcSize,                                                 \
            void* dst,                                                      \
            size_t dstCapacity,                                             \
            void* customPayload)                                            \
    {                                                                       \
        (void)customPayload;                                                \
        return sparseNumEncode(src, srcSize, dst, dstCapacity, width);      \
    }

DEFINE_SPARSE_NUM_ENCODE(8, 1)
DEFINE_SPARSE_NUM_ENCODE(16, 2)
DEFINE_SPARSE_NUM_ENCODE(32, 4)
DEFINE_SPARSE_NUM_ENCODE(64, 8)

static size_t sparseNumDecodeOutputSize(
        const void* src,
        size_t srcSize,
        size_t valueWidth,
        size_t distanceWidth)
{
    /*
     * Fixture layout: [values][D8 distances], numDistances == numValues.
     * Values come first to preserve their numeric alignment.
     */
    if (src == NULL) {
        abort();
    }
    if (distanceWidth != 1) {
        abort();
    }
    size_t const recordSize = valueWidth + distanceWidth;
    if (srcSize % recordSize != 0) {
        abort();
    }
    size_t const numValues    = srcSize / recordSize;
    size_t const numDistances = numValues;
    /* D8 distances begin immediately after the aligned values stream. */
    const uint8_t* const distances =
            (const uint8_t*)src + numValues * valueWidth;
    size_t const outputCount = ZL_sparseNumDecodeOutputCount(
            distances, numDistances, distanceWidth, numValues);
    if (outputCount == ZL_SPARSE_NUM_DECODE_OUTPUT_COUNT_ERROR) {
        abort();
    }
    if (outputCount > SIZE_MAX / valueWidth) {
        abort();
    }

    return outputCount * valueWidth;
}

static size_t sparseNumDecode(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        size_t valueWidth,
        size_t distanceWidth)
{
    if (src == NULL) {
        abort();
    }
    size_t const recordSize = valueWidth + distanceWidth;
    if (srcSize % recordSize != 0) {
        abort();
    }
    size_t const numValues    = srcSize / recordSize;
    size_t const numDistances = numValues;
    /* Mirror sparseNumDecodeOutputSize(): [values][D8 distances]. */
    const uint8_t* const values    = (const uint8_t*)src;
    const uint8_t* const distances = values + numValues * valueWidth;
    size_t const outputSize =
            sparseNumDecodeOutputSize(src, srcSize, valueWidth, distanceWidth);

    if (dstCapacity < outputSize) {
        abort();
    }

    ZL_sparseNumDecode(
            dst,
            outputSize,
            distances,
            numDistances,
            distanceWidth,
            values,
            numValues,
            valueWidth);

    return outputSize;
}

#define DEFINE_SPARSE_NUM_DECODE_D8(bits, width)                               \
    size_t sparseNumDecode##bits##_d8_outSize(const void* src, size_t srcSize) \
    {                                                                          \
        return sparseNumDecodeOutputSize(src, srcSize, width, 1);              \
    }                                                                          \
                                                                               \
    size_t sparseNumDecode##bits##_d8_wrapper(                                 \
            const void* src,                                                   \
            size_t srcSize,                                                    \
            void* dst,                                                         \
            size_t dstCapacity,                                                \
            void* customPayload)                                               \
    {                                                                          \
        (void)customPayload;                                                   \
        return sparseNumDecode(src, srcSize, dst, dstCapacity, width, 1);      \
    }

DEFINE_SPARSE_NUM_DECODE_D8(8, 1)
DEFINE_SPARSE_NUM_DECODE_D8(16, 2)
DEFINE_SPARSE_NUM_DECODE_D8(32, 4)
DEFINE_SPARSE_NUM_DECODE_D8(64, 8)
