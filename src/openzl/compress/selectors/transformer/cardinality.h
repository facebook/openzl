// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_SELECTORS_TRANSFORMER_CARDINALITY_H
#define OPENZL_COMPRESS_SELECTORS_TRANSFORMER_CARDINALITY_H

#include <limits.h> /* CHAR_BIT */
#include <stddef.h> /* size_t */
#include <stdint.h> /* uint64_t */

#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

enum {
    TRS_CARDINALITY_U8_BITMAP_WORDS = (1U << 8) / (sizeof(uint64_t) * CHAR_BIT),
    TRS_CARDINALITY_U16_BITMAP_WORDS =
            (1U << 16) / (sizeof(uint64_t) * CHAR_BIT),
    TRS_CARDINALITY_U8_PAIR_BITMAP_WORDS = TRS_CARDINALITY_U16_BITMAP_WORDS,
};

/*
 * Transformer feature adapters over the shared OpenZL cardinality machinery.
 *
 * Their hashing representation and estimator policy reproduce the feature
 * values used to train the Transformer models. They must not be replaced by a
 * different estimator merely because it has equivalent statistical accuracy.
 * In particular, a saturated 8192-bucket LinearCount table yields 8192 as a
 * lower-bound sentinel. Changing that policy requires regenerating features,
 * retraining the models, and evaluating the resulting selector.
 */

/** Estimate the number of distinct values, capped at `n_elements`. */
uint64_t TRS_estimate_cardinality_u64(const uint64_t* data, size_t n_elements);
uint64_t TRS_estimate_cardinality_u32(const uint32_t* data, size_t n_elements);

/**
 * Estimate distinct consecutive pairs `(data[i], data[i + 1])`, capped at
 * `n_elements - 1`. Returns zero when fewer than two elements are present.
 */
uint64_t TRS_estimate_pair_cardinality_u64(
        const uint64_t* data,
        size_t n_elements);
uint64_t TRS_estimate_pair_cardinality_u32(
        const uint32_t* data,
        size_t n_elements);

/**
 * Reproduce the current Transformer model's byte or 16-bit cardinality
 * feature from an exact presence bitmap.
 *
 * Although the bitmap contains enough information to return an exact count,
 * these functions deliberately feed its distinct values through the
 * historical cardinality estimator. The resulting bias, hashing, estimator
 * selection, and capping are part of the current model's feature contract and
 * must remain stable for the lifetime of that model version. This is not a
 * temporary migration step; a future model may instead define an exact count
 * as a separately versioned feature.
 *
 * The byte bitmap must contain `TRS_CARDINALITY_U8_BITMAP_WORDS` words. The
 * 16-bit bitmap must contain `TRS_CARDINALITY_U16_BITMAP_WORDS` words.
 */
uint64_t TRS_estimate_cardinality_u8_bitmap(
        const uint64_t seen_values[TRS_CARDINALITY_U8_BITMAP_WORDS],
        size_t n_elements);
uint64_t TRS_estimate_cardinality_u16_bitmap(
        const uint64_t seen_values[TRS_CARDINALITY_U16_BITMAP_WORDS],
        size_t n_elements);

/**
 * Reproduce the current model's byte-pair cardinality feature from an exact
 * 65536-entry presence bitmap, with the same compatibility contract above.
 * A pair `(first, second)` is represented by bit index
 * `((uint16_t)first << 8) | second`; for adjacent input bytes, `first` is the
 * previous byte and `second` is the current byte.
 * `seen_pairs` must contain `TRS_CARDINALITY_U8_PAIR_BITMAP_WORDS` words.
 */
uint64_t TRS_estimate_pair_cardinality_u8_bitmap(
        const uint64_t seen_pairs[TRS_CARDINALITY_U8_PAIR_BITMAP_WORDS],
        size_t n_elements);

/**
 * Estimate the cardinality of overlapping 8-byte windows in the canonical
 * little-endian byte representation of a native-endian numeric stream.
 *
 * Returns an estimate capped at `n_bytes - 7`, or zero when fewer than eight
 * bytes are present. `elt_width` must be 1, 2, 4, or 8, and `n_bytes` must be
 * a multiple of it.
 */
uint64_t TRS_estimate_d8_cardinality(
        const uint8_t* data,
        size_t n_bytes,
        size_t elt_width);

ZL_END_C_DECLS

#endif /* OPENZL_COMPRESS_SELECTORS_TRANSFORMER_CARDINALITY_H */
