// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_SHARED_ESTIMATE_H
#define OPENZL_SHARED_ESTIMATE_H

#include "openzl/shared/portability.h"

#include <stddef.h>
#include <stdint.h>

ZL_BEGIN_C_DECLS

typedef struct {
    uint64_t min;
    uint64_t max;
} ZL_ElementRange;

/**
 * Returns the exact range of a stream of elements.
 */
ZL_ElementRange
ZL_computeUnsignedRange(void const* src, size_t nbElts, size_t eltSize);
ZL_ElementRange ZL_computeUnsignedRange64(uint64_t const* src, size_t srcSize);
ZL_ElementRange ZL_computeUnsignedRange32(uint32_t const* src, size_t srcSize);
ZL_ElementRange ZL_computeUnsignedRange16(uint16_t const* src, size_t srcSize);
ZL_ElementRange ZL_computeUnsignedRange8(uint8_t const* src, size_t srcSize);

/**
 * An estimate of the cardinality of the stream.
 *
 * lowerBound <= estimateLowerBound <= estimate <= estimateUpperBound <=
 * upperBound
 *
 * The lowerBound and upperBound are hard bounds. In the case that they are
 * unknown they are 0 & 2^64-1 respectively. They have no guarantee to be
 * tight.
 *
 * The estimateLowerBound and estimateUpperBound are estimates of the
 * error bars, but note that they are soft bounds, and can still be wrong.
 *
 */
typedef struct {
    uint64_t lowerBound;
    uint64_t estimateLowerBound;
    uint64_t estimate;
    uint64_t estimateUpperBound;
    uint64_t upperBound;
} ZL_CardinalityEstimate;

#define ZL_ESTIMATE_CARDINALITY_ANY 0
#define ZL_ESTIMATE_CARDINALITY_7BITS 128
#define ZL_ESTIMATE_CARDINALITY_8BITS 256
#define ZL_ESTIMATE_CARDINALITY_16BITS 65536
#define ZL_ESTIMATE_CARDINALITY_MAX (1u << 31)

/**
 * Cardinality estimator variants:
 *
 * - `ZL_estimateCardinality_fixed()` is the optimized choice for a packed
 *   array of native unsigned integers. `eltSize` must be 1, 2, 4, or 8.
 * - `ZL_estimateCardinality_variable()` accepts a separate pointer and size
 *   for every element. Use it only when element sizes or locations vary.
 * - `ZL_estimateCardinality_hashed()` is a low-level escape hatch for derived
 *   elements that cannot be represented by either layout. Prefer the other
 *   variants when they apply.
 *
 * The fixed and variable variants accept `cardinalityEarlyExit`, the maximum
 * interesting cardinality. They may stop early and report any value greater
 * than or equal to this limit. Pass `ZL_ESTIMATE_CARDINALITY_ANY` when the
 * full range is relevant. The limit also selects internal table sizes and is
 * adjusted down to respect known bounds such as `nbElts`.
 *
 * The implementation is much faster when `cardinalityEarlyExit` is at most
 * `ZL_ESTIMATE_CARDINALITY_16BITS`. Request a larger limit only when estimates
 * above 64K are useful to the caller.
 */

/**
 * Estimates the cardinality of `nbElts` packed native unsigned integers.
 *
 * `src` must point to `nbElts * eltSize` readable bytes and be suitably
 * aligned for unsigned integers of width `eltSize`.
 */
ZL_CardinalityEstimate ZL_estimateCardinality_fixed(
        void const* src,
        size_t nbElts,
        size_t eltSize,
        uint64_t cardinalityEarlyExit);

/**
 * Estimates the cardinality of elements with independent locations and sizes.
 *
 * @param srcs The source pointers. `srcs[i]` must have length `eltSizes[i]`.
 * @param eltSizes The sizes.
 * @param nbElts The number of entries in `srcs` and `eltSizes`.
 */
ZL_CardinalityEstimate ZL_estimateCardinality_variable(
        void const* const* srcs,
        size_t const* eltSizes,
        size_t nbElts,
        uint64_t cardinalityEarlyExit);

typedef uint64_t (*ZL_CardinalityHashFn)(void* state, size_t index);

/**
 * Estimates cardinality from caller-defined hashes of derived elements.
 *
 * This is the low-level variant for inputs such as overlapping windows whose
 * logical elements are not directly described by the fixed or variable APIs.
 * The callbacks receive `state` and an element index. They must return stable,
 * well-distributed hashes, and equal logical elements must produce equal
 * 64-bit hashes. The selected callback is invoked `nbHashes` times in
 * increasing index order and may update `state` while iterating. Both
 * callbacks must be non-null when `nbHashes` is non-zero.
 *
 * For compatibility-sensitive callers, the hashing representation and both
 * callbacks are part of the feature definition: changing either can change
 * the deterministic estimate. `linearCountHash` is used when
 * `cardinalityUpperBound` is at most 64K; `hyperLogLogHash` is used otherwise.
 * `nbHashes` may be smaller than `cardinalityUpperBound` when the caller has
 * already removed duplicate elements.
 *
 * Unlike the fixed and variable APIs, the LinearCount path always uses 8192
 * buckets rather than selecting a table size from the bound. LinearCount
 * corrections are rounded to the nearest integer, while full HyperLogLog
 * estimates are truncated. These distinctions are part of this estimator's
 * compatibility contract.
 *
 * The result is capped at `cardinalityUpperBound`. If the LinearCount table
 * saturates, its raw result is the lower-bound sentinel 8192 rather than an
 * accurate estimate or the caller's upper bound.
 */
uint64_t ZL_estimateCardinality_hashed(
        void* state,
        size_t nbHashes,
        size_t cardinalityUpperBound,
        ZL_CardinalityHashFn linearCountHash,
        ZL_CardinalityHashFn hyperLogLogHash);

/// A summary of what we estimate the dimensionality is.
typedef enum {
    /// No dimensionality detected.
    ZL_DimensionalityStatus_none,
    /// The data may be 2D, but it isn't strongly dimensional.
    /// Don't blindly assume the data is dimensional with this result.
    /// Use the match information to decide if the 2D structure is strong
    /// enough for your use case.
    ZL_DimensionalityStatus_possibly2D,
    /// The data is very likely 2D, and is strongly dimensional.
    /// We've verified there is a strong dimensionality component, but it may
    /// not be the only correlation that exists.
    ZL_DimensionalityStatus_likely2D,
} ZL_DimensionalityStatus;

/**
 * An estimate of the data's dimensionality.
 */
typedef struct {
    /// What is the dimensionality?
    ZL_DimensionalityStatus dimensionality;
    /// The estimated stride of the dimensionality
    /// NOTE: In number of elements, not bytes!
    size_t stride;
    /// The number of matching elements we've found at an offset that is an
    /// exact multiple of stride. Use this with totalMatches to find the ratio
    /// of matches that are exactly a 2D match.
    size_t strideMatches;
    /// The total number of matching elements at any offset.
    size_t totalMatches;
} ZL_DimensionalityEstimate;

/// Returns an estimate of the dimensionality of src.
ZL_DimensionalityEstimate
ZL_estimateDimensionality(void const* src, size_t nbElts, size_t eltSize);
ZL_DimensionalityEstimate ZL_estimateDimensionality1(
        void const* src,
        size_t nbElts);
ZL_DimensionalityEstimate ZL_estimateDimensionality2(
        void const* src,
        size_t nbElts);
ZL_DimensionalityEstimate ZS_estimateDimensionality3(
        void const* src,
        size_t nbElts);
ZL_DimensionalityEstimate ZL_estimateDimensionality4(
        void const* src,
        size_t nbElts);
ZL_DimensionalityEstimate ZL_estimateDimensionality8(
        void const* src,
        size_t nbElts);

/**
 * @returns The estimated width of the floating point data
 * in the source stream in bytes. If the source stream is not
 * floating point data, this function may return any width.
 */
size_t ZL_guessFloatWidth(void const* src, size_t srcSize);

ZL_END_C_DECLS

#endif // OPENZL_SHARED_ESTIMATE_H
