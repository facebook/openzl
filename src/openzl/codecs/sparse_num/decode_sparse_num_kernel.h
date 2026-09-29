// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_SPARSE_NUM_DECODE_SPARSE_NUM_KERNEL_H
#define OPENZL_CODECS_SPARSE_NUM_DECODE_SPARSE_NUM_KERNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openzl/codecs/sparse_num/common_sparse_num.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

#define ZL_SPARSE_NUM_DECODE_OUTPUT_COUNT_ERROR SIZE_MAX

/**
 * Computes the number of numeric elements produced by sparse_num decode.
 *
 * @param distances The distance stream, as numeric elements.
 * @param numDistances The number of elements in @p distances.
 * @param distanceWidth The byte width of each distance element.
 * @param numLiterals The number of literal values in the value stream.
 * @return The decoded output element count, or
 *         ZL_SPARSE_NUM_DECODE_OUTPUT_COUNT_ERROR if the count overflows
 *         size_t or reaches the reserved error value.
 *
 * @pre @p distanceWidth must be a supported distance width.
 * @pre @p distances must point to @p numDistances distanceWidth-byte numeric
 *      elements.
 * @pre @p distances must be aligned for distanceWidth-byte numeric elements.
 */
size_t ZL_sparseNumDecodeOutputCount(
        const void* distances,
        size_t numDistances,
        size_t distanceWidth,
        size_t numLiterals);

/**
 * Decodes sparse_num into dst.
 *
 * A dominant value of 0 is the common sparse-zero mode and uses the
 * zero-specialized internal dispatch.
 *
 * @param dst The destination buffer for decoded numeric elements.
 * @param expectedDstSize The exact byte size produced by this decode.
 * @param distances The distance stream, as numeric elements.
 * @param numDistances The number of elements in @p distances.
 * @param distanceWidth The byte width of each distance element.
 * @param dominant The dominant symbol value.
 * @param values The literal value stream, as numeric elements.
 * @param numValues The number of elements in @p values.
 * @param valueWidth The byte width of each literal and output element.
 *
 * @pre @p numDistances must be @p numValues or @p numValues + 1.
 * @pre @p distanceWidth must be a supported distance width.
 * @pre @p distances must point to @p numDistances distanceWidth-byte numeric
 *      elements.
 * @pre @p distances must be aligned for distanceWidth-byte numeric elements.
 * @pre @p dominant must fit in @p valueWidth bytes.
 * @pre @p valueWidth must be a supported numeric element width.
 * @pre @p values must point to @p numValues valueWidth-byte numeric elements.
 * @pre @p values must be aligned for valueWidth-byte numeric elements.
 * @pre @p dst must be aligned for valueWidth-byte numeric elements.
 * @pre @p expectedDstSize must be outputCount * valueWidth, where outputCount
 *      is returned by ZL_sparseNumDecodeOutputCount() for the same distance
 *      and value streams.
 *
 * @note @p expectedDstSize is used as a debug invariant that the kernel writes
 *       exactly the caller-computed destination size. Release builds still
 *       trust the caller to provide a correctly sized destination buffer.
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
        size_t valueWidth);

ZL_END_C_DECLS

#endif
