// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_STATS_H
#define OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_STATS_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/common/assertion.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/** Number of entries retained by the Transformer KMV sample. */
#define TRS_NUMERIC_KMV_K 256

/** Number of uint32_t entries required by the match4 feature table. */
#define TRS_NUMERIC_MATCH4_TABLE_ENTRIES 4096

/** Number of uint64_t entries required by sorted-gap feature extraction. */
#define TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES 1536

/** Return the absolute difference between two unsigned values. */
static inline uint64_t TRS_numeric_abs_diff_u64(uint64_t a, uint64_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

/**
 * Return the byte at `index` in the canonical little-endian representation of
 * native big-endian elements. `elt_width` must be 1, 2, 4, or 8 bytes.
 */
static inline uint8_t TRS_numeric_canonical_byte_from_big_endian(
        const uint8_t* data,
        size_t index,
        size_t elt_width)
{
    ZL_ASSERT(
            elt_width == 1 || elt_width == 2 || elt_width == 4
                    || elt_width == 8,
            "unsupported numeric element width");
    size_t const byte_in_element = index % elt_width;
    size_t const element_start   = index - byte_in_element;
    return data[element_start + elt_width - byte_in_element - 1];
}

/**
 * Internal KMV sample entry. Callers should not inspect or modify its fields.
 */
typedef struct {
    uint64_t hash;
    uint64_t value;
} TRS_NumericKmvEntry;

/**
 * Add `value` to a KMV sample that retains the smallest hashes of observed
 * values. Sampling does not remove duplicates; repeated values may occupy
 * multiple slots and are compacted only by
 * `TRS_numeric_kmv_compute_gap_nmad_fp()`. This matches the trained feature
 * contract.
 *
 * `heap` must have capacity for at least `TRS_NUMERIC_KMV_K` entries. Set
 * `*size` to zero before adding the first value, then preserve it between
 * calls. The entries themselves do not need to be initialized.
 */
void TRS_numeric_kmv_track_value(
        TRS_NumericKmvEntry* heap,
        size_t* size,
        uint64_t value);

/**
 * Return the sampled sorted-gap normalized mean absolute deviation in Q32.
 *
 * Sorts and compacts the first `kmv_size` entries of `heap` in place. The
 * array is no longer a valid KMV heap afterward and must not be passed back to
 * `TRS_numeric_kmv_track_value()` without rebuilding it. `kmv_size` must not
 * exceed `TRS_NUMERIC_KMV_K`.
 */
uint64_t TRS_numeric_kmv_compute_gap_nmad_fp(
        TRS_NumericKmvEntry* heap,
        size_t kmv_size);

/**
 * Return the fraction of retained adjacent gaps equal to the most common gap.
 * Retains at most the 512 smallest distinct values, then measures adjacent
 * gaps over the largest 384 values in that retained band. These limits are
 * part of the trained feature contract. `buffer` must contain at least
 * `TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES` entries.
 */
double TRS_numeric_compute_sorted_gap_mode(
        const uint64_t* data,
        size_t n_elements,
        uint64_t* buffer,
        size_t buffer_capacity);

/**
 * Return the sorted-gap mode for native-endian integer elements stored as
 * bytes. `elt_width` must be 1, 2, 4, or 8. `buffer` must contain at least
 * `TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES` entries.
 */
double TRS_numeric_compute_sorted_gap_mode_from_bytes(
        const uint8_t* data,
        size_t n_elements,
        size_t elt_width,
        uint64_t* buffer,
        size_t buffer_capacity);

/**
 * Return the coefficient of variation from pre-aggregated transition gaps.
 * Returns zero when there are no gaps or their mean is not positive.
 */
double TRS_numeric_compute_transition_gap_cv(
        double sum_gaps,
        double sum_gap_squares,
        size_t n_gaps);

/**
 * Count matching four-byte windows in the canonical little-endian byte
 * representation of a native-endian numeric stream using caller-provided
 * scratch storage. `elt_width` must be 1, 2, 4, or 8, and `n_bytes` must be a
 * multiple of it.
 *
 * `table_capacity` must be at least `TRS_NUMERIC_MATCH4_TABLE_ENTRIES`.
 */
uint64_t TRS_numeric_compute_lz_matches_with_table(
        const uint8_t* data,
        size_t n_bytes,
        size_t elt_width,
        uint32_t* table,
        size_t table_capacity);

/**
 * Encode a positive finite value with 32 fractional bits, saturating finite
 * overflow at `UINT64_MAX`. Non-positive and non-finite inputs return zero.
 */
uint64_t TRS_numeric_encode_double_fp(double value);

ZL_END_C_DECLS

#endif /* OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_STATS_H */
