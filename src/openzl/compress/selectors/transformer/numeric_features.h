// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_FEATURES_H
#define OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_FEATURES_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint64_t, int64_t */

#include "openzl/common/assertion.h"

/*
 * Return the first value that requires more than half of the source element's
 * bit width. `elt_width` must be 1, 2, 4, or 8 bytes.
 */
static inline uint64_t TRS_numeric_half_width_threshold(size_t elt_width)
{
    ZL_ASSERT(
            elt_width == 1 || elt_width == 2 || elt_width == 4
            || elt_width == 8);
    return UINT64_C(1) << (elt_width * 4);
}

/*
 * Statistical features extracted from numeric input.
 */
typedef struct {
    /* Primitive features */
    uint64_t min_u;           /* min unsigned */
    uint64_t max_u;           /* max unsigned */
    uint64_t sum_u;           /* sum of unsigned values */
    int64_t min_s;            /* min signed */
    int64_t max_s;            /* max signed */
    int64_t sum_s;            /* sum of signed values */
    size_t count;             /* number of elements */
    size_t max_run_length;    /* longest contiguous run of equal values */
    uint64_t max_abs_delta_u; /* max |delta| between consecutive (unsigned) */
    uint64_t max_abs_delta_s; /* max |delta| between consecutive (signed) */
    size_t zero_count;        /* count of zero values */
    size_t delta_up_count;    /* count of upward transitions */
    size_t delta_down_count;  /* count of downward transitions */
    int64_t delta_min_s;      /* min signed delta between consecutive */
    int64_t delta_max_s;      /* max signed delta between consecutive */

    /* Complex raw statistics (each requires independent data scan) */
    uint64_t mean_abs_dev_u_fp; /* mean absolute deviation (unsigned), scaled by
                                   2^32 */
    uint64_t mean_abs_dev_s_fp; /* mean absolute deviation (signed), scaled by
                                   2^32 */
    uint64_t mean_abs_delta_u_fp;  /* mean |delta| between consecutive
                                      (unsigned), scaled by 2^32 */
    uint64_t cardinality_est;      /* estimated number of distinct values */
    uint64_t pair_cardinality_est; /* estimated number of distinct consecutive
                                      pairs */
    uint64_t d8_cardinality_est;   /* estimated distinct overlapping 8-byte
                                      windows */
    uint64_t sorted_gap_cv_fp;     /* KMV sorted-gap CV, scaled by 2^32 */
    uint64_t transition_gap_cv_fp; /* CV of gaps between value changes, scaled
                                      by 2^32 */
    uint64_t min_lb0;              /* min trailing zero bits (excl. zeros) */
    uint64_t match4;               /* LZ-style 4-byte hash match count */

    /* Derived features retained by the current legacy sub-64 model contract. */
    uint64_t range_u;           /* max_u - min_u */
    uint64_t range_s;           /* (uint64_t)(max_s - min_s) */
    uint64_t mean_u_fp;         /* mean unsigned, scaled by 2^32 */
    int64_t mean_s_fp;          /* mean signed, scaled by 2^32 */
    uint64_t zero_ratio_fp;     /* zero fraction, scaled by 2^32 */
    uint64_t delta_up_ratio_fp; /* upward transition fraction, scaled by 2^32 */
    uint64_t delta_down_ratio_fp; /* downward transition fraction, scaled by
                                     2^32 */

    /* Metadata */
    size_t elt_width; /* element size in bytes: 1, 2, 4, or 8 */
} TRS_NumericFeatures;

#endif /* OPENZL_COMPRESS_SELECTORS_TRANSFORMER_NUMERIC_FEATURES_H */
