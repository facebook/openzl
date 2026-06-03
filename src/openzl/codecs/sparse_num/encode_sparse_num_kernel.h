// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_SPARSE_NUM_ENCODE_SPARSE_NUM_KERNEL_H
#define OPENZL_CODECS_SPARSE_NUM_ENCODE_SPARSE_NUM_KERNEL_H

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
 * Selects an automatic dominant symbol from a bounded input prefix.
 *
 * The detector uses Boyer-Moore majority voting over a small prefix. It is a
 * heuristic for encoder planning, not a decoder-format condition: callers may
 * encode with the returned non-zero symbol even if it is not dominant over the
 * full input. Such output remains valid, only potentially inefficient.
 *
 * @param src The input stream, as numeric elements.
 * @param numElts The number of elements in @p src.
 * @param valueWidth The byte width of each input element.
 * @return The selected dominant symbol candidate.
 *
 * @pre @p src must point to @p numElts valueWidth-byte numeric elements.
 * @pre @p src must be aligned for valueWidth-byte numeric elements.
 * @pre @p valueWidth must be a supported numeric element width.
 */
uint64_t
ZL_sparseNumSelectDominant(const void* src, size_t numElts, size_t valueWidth);

/**
 * Scans src and computes the sparse_num output layout.
 *
 * A dominant value of 0 is the sparse-zero mode. Non-zero dominant values use
 * dominant-symbol mode. In both modes, the values stream contains literal
 * values only.
 *
 * @param src The input stream, as numeric elements.
 * @param numElts The number of elements in @p src.
 * @param valueWidth The byte width of each input element.
 * @param dominant The dominant symbol value, or 0 for sparse-zero mode.
 * @return The number of distance elements, literal value elements, and minimal
 *         distance width needed to encode the stream.
 *
 * @pre @p src must point to @p numElts valueWidth-byte numeric elements.
 * @pre @p src must be aligned for valueWidth-byte numeric elements.
 * @pre @p valueWidth must be a supported numeric element width.
 * @pre @p dominant must fit in @p valueWidth bytes.
 */
ZL_SparseNumEncodeInfo ZL_sparseNumComputeEncodeInfo(
        const void* src,
        size_t numElts,
        size_t valueWidth,
        uint64_t dominant);

/**
 * Encodes src into separate sparse_num distance and value streams.
 *
 * This function is intended to be called after
 * ZL_sparseNumComputeEncodeInfo() has succeeded for the same source stream.
 * A dominant value of 0 is the sparse-zero mode; non-zero values use
 * dominant-symbol mode.
 *
 * @param distances The output distance stream.
 * @param values The output literal value stream.
 * @param src The input stream, as numeric elements.
 * @param numElts The number of elements in @p src.
 * @param valueWidth The byte width of each input and literal value element.
 * @param distanceWidth The byte width of each output distance element.
 * @param dominant The dominant symbol value, or 0 for sparse-zero mode.
 *
 * @pre @p src must point to @p numElts valueWidth-byte numeric elements.
 * @pre @p src must be aligned for valueWidth-byte numeric elements.
 * @pre @p valueWidth must be a supported numeric element width.
 * @pre @p distanceWidth must be a supported distance width.
 * @pre @p dominant must fit in @p valueWidth bytes.
 * @pre @p distanceWidth must be large enough for the largest emitted dominant
 *      run reported by ZL_sparseNumComputeEncodeInfo().
 * @pre @p distances must be aligned for distanceWidth-byte numeric elements.
 * @pre @p distances must have room for info.numDistances * distanceWidth bytes.
 * @pre @p values must be aligned for valueWidth-byte numeric elements.
 * @pre @p values must have room for info.numValues * valueWidth bytes.
 * @pre @p distances and @p values must not overlap @p src or each other in a
 *      way that changes source elements before they are read or corrupts
 *      output writes.
 */
void ZL_sparseNumEncode(
        void* distances,
        void* values,
        const void* src,
        size_t numElts,
        size_t valueWidth,
        size_t distanceWidth,
        uint64_t dominant);

ZL_END_C_DECLS

#endif
