// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_SPARSE_NUM_ENCODE_SPARSE_NUM_KERNEL_H
#define OPENZL_CODECS_SPARSE_NUM_ENCODE_SPARSE_NUM_KERNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openzl/codecs/sparse_num/common_sparse_num.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/**
 * Lean sparse_num encoder kernel API.
 *
 * The binding is responsible for validating stream metadata and allocating the
 * output streams. The kernel only scans numeric elements, reports the required
 * output layout, and writes the distance and literal-value streams.
 */
typedef struct {
    size_t numDistances;
    size_t numValues;
    size_t distanceWidth;
} ZL_SparseNumEncodeInfo;

/**
 * Scans src and computes the canonical sparse_num output layout.
 *
 * Preconditions:
 * - src must point to numElts numeric elements, each valueWidth bytes wide.
 * - src must be aligned for valueWidth-byte numeric elements.
 * - valueWidth must be a supported numeric element width.
 *
 * Returns:
 * - numDistances: number of zero-run distances to emit.
 * - numValues: number of literal values to emit.
 * - distanceWidth: minimal distance width that can encode the largest emitted
 *   run.
 */
ZL_SparseNumEncodeInfo ZL_sparseNumComputeEncodeInfo(
        const void* src,
        size_t numElts,
        size_t valueWidth);

/**
 * Encodes src into separate sparse_num distance and value streams.
 *
 * This function is intended to be called after
 * ZL_sparseNumComputeEncodeInfo() has succeeded for the same source stream.
 *
 * Preconditions:
 * - src must point to numElts numeric elements, each valueWidth bytes wide.
 * - src must be aligned for valueWidth-byte numeric elements.
 * - valueWidth and distanceWidth must be supported widths.
 * - distanceWidth must be large enough for the largest emitted zero run
 *   reported by ZL_sparseNumComputeEncodeInfo().
 * - distances must be aligned for distanceWidth-byte numeric elements.
 * - distances must have room for info.numDistances * distanceWidth bytes.
 * - values must be aligned for valueWidth-byte numeric elements.
 * - values must have room for info.numValues * valueWidth bytes.
 * - distances and values must not overlap src or each other in a way that
 *   changes source elements before they are read or corrupts output writes.
 */
void ZL_sparseNumEncode(
        void* distances,
        void* values,
        const void* src,
        size_t numElts,
        size_t valueWidth,
        size_t distanceWidth);

ZL_END_C_DECLS

#endif
