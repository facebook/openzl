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
 * Returns ZL_SPARSE_NUM_DECODE_OUTPUT_COUNT_ERROR if the count overflows size_t
 * or reaches the reserved error value.
 *
 * Preconditions:
 * - distanceWidth must be a supported distance width.
 * - distances must point to numDistances distanceWidth-byte numeric elements.
 * - distances must be aligned for distanceWidth-byte numeric elements.
 */
size_t ZL_sparseNumDecodeOutputCount(
        const void* distances,
        size_t numDistances,
        size_t distanceWidth,
        size_t numValues);

/**
 * Decodes sparse_num into dst.
 *
 * Preconditions:
 * - numDistances must be numValues or numValues + 1.
 * - distances must point to numDistances distanceWidth-byte numeric elements.
 * - distances must be aligned for distanceWidth-byte numeric elements.
 * - values must point to numValues valueWidth-byte numeric elements.
 * - values must be aligned for valueWidth-byte numeric elements.
 * - dst must be aligned for valueWidth-byte numeric elements.
 * - expectedDstSize must be the exact byte size produced by this decode:
 *   outputCount * valueWidth, where outputCount is returned by
 *   ZL_sparseNumDecodeOutputCount() for the same distance and value streams.
 * - expectedDstSize is used as a debug invariant that the kernel writes exactly
 *   the caller-computed destination size. Release builds still trust the caller
 *   to provide a correctly sized destination buffer.
 */
void ZL_sparseNumDecode(
        void* dst,
        size_t expectedDstSize,
        const void* distances,
        size_t numDistances,
        size_t distanceWidth,
        const void* values,
        size_t numValues,
        size_t valueWidth);

ZL_END_C_DECLS

#endif
