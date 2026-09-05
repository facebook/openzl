// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_FEATURES_V2_H
#define OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_FEATURES_V2_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint64_t, int64_t */

/*
 * Width-correct public output contract for numeric feature extraction.
 *
 * Integer fields are reserved for values that are exact on all widths.
 * Width-sensitive summaries and real-valued statistics use double.
 */
typedef struct {
    /* Exact counts */
    uint64_t count;
    uint64_t max_run_length;
    uint64_t zero_count;
    uint64_t delta_up_count;
    uint64_t delta_down_count;

    /* Exact per-width extrema */
    uint64_t min_u;
    uint64_t max_u;
    int64_t min_s;
    int64_t max_s;

    /* Exact delta/range primitives */
    uint64_t max_abs_delta_u;
    uint64_t max_abs_delta_s;
    uint64_t range_u;
    uint64_t range_s;

    /* Exact estimator / count-like outputs */
    uint64_t cardinality_est;
    uint64_t pair_cardinality_est;
    uint64_t d8_cardinality_est;
    uint64_t min_lb0;
    uint64_t match4;

    /* Width-sensitive summaries */
    double sum_u;
    double sum_s;
    double delta_min_s;
    double delta_max_s;
    double mean_u;
    double mean_s;
    double mean_abs_dev_u;
    double mean_abs_dev_s;
    double mean_abs_delta_u;
    double zero_ratio;
    double delta_up_ratio;
    double delta_down_ratio;
    double sorted_gap_nmad;
    double sorted_gap_mode;
    double transition_gap_cv;

    /* Metadata */
    size_t elt_width; /* element size in bytes: 1, 2, 4, or 8 */
} TRS_NumericFeaturesV2;

#endif /* OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_FEATURES_V2_H */
