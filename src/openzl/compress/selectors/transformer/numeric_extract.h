// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_EXTRACT_H
#define OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_EXTRACT_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint32_t, uint64_t */

#include "cardinality.h"
#include "numeric_features.h"
#include "numeric_stats.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

enum {
    TRS_NUMERIC_CARDINALITY_SCRATCH_WORDS = TRS_CARDINALITY_U8_BITMAP_WORDS
            + TRS_CARDINALITY_U8_PAIR_BITMAP_WORDS,
};

/*
 * The descriptor remains read-only during extraction; its caller-owned
 * buffers are mutable scratch storage.
 */
typedef struct {
    /* Widened sub-64-bit values consumed by the legacy feature extractor. */
    uint32_t* values;
    size_t capacity;
    /* Caller-owned table for the match4 byte-stream feature. */
    uint32_t* match4_table;
    size_t match4_capacity;
    /* Fixed-size scratch shared by the legacy numeric scans. */
    TRS_NumericKmvEntry* kmv_entries;
    size_t kmv_capacity;
    uint64_t* cardinality_bitmap;
    size_t cardinality_bitmap_capacity;
} TRS_NumericExtractWorkspace;

/*
 * Extract element-domain features from widened numeric values. Byte-domain
 * features (`match4` and `d8_cardinality_est`) remain zero because this API
 * does not receive the original byte stream. Sub-64-bit inputs require one
 * workspace element per input element and `TRS_NUMERIC_KMV_K` KMV entries.
 * Width two additionally requires `TRS_CARDINALITY_U16_BITMAP_WORDS`
 * cardinality bitmap words. Each widened value must be the canonical
 * zero-extension of its declared element width; values outside that width are
 * a programming error. Legacy extraction requires at most UINT32_MAX elements.
 * This bounds the Q32 ratio numerators and, for sub-64-bit inputs, the legacy
 * 64-bit feature sums. Width-one extraction retains the legacy widened-u32
 * cardinality estimators, so its cardinality fields can differ from the byte
 * extractor's trained u8 bitmap estimates. Use the byte extractor when
 * model-compatible complete features are required.
 *
 * Returns 1 on success and 0 if the arguments or workspace are invalid.
 */
int TRS_numericFeatures_extract(
        TRS_NumericFeatures* result,
        const uint64_t* data,
        size_t n_elements,
        size_t elt_width,
        const TRS_NumericExtractWorkspace* workspace);

/*
 * Extract the complete feature set from raw bytes, including `match4` and
 * `d8_cardinality_est`.
 *
 * data_bytes: raw file contents, n_bytes: total byte count.
 * elt_width: 1, 2, 4, or 8 (determines element count for numeric features).
 * The element count must not exceed UINT32_MAX.
 * Num32 and num64 inputs must be naturally aligned, as guaranteed for OpenZL
 * numeric streams. Non-empty inputs require a `match4_table` workspace with at
 * least `TRS_NUMERIC_MATCH4_TABLE_ENTRIES` entries. Num8, num16, and num32 also
 * require `TRS_NUMERIC_KMV_K` KMV entries. Num8 requires
 * `TRS_NUMERIC_CARDINALITY_SCRATCH_WORDS` bitmap words. Num16 requires
 * `TRS_CARDINALITY_U16_BITMAP_WORDS` bitmap words and one `values` workspace
 * element per input element to widen its values to the uint32 representation
 * consumed by the legacy feature extractor.
 *
 * Returns 1 on success and 0 if the arguments or workspace are invalid.
 */
int TRS_numericFeatures_extract_from_bytes(
        TRS_NumericFeatures* result,
        const uint8_t* data_bytes,
        size_t n_bytes,
        size_t elt_width,
        const TRS_NumericExtractWorkspace* workspace);

ZL_END_C_DECLS

#endif /* OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_EXTRACT_H */
