// Copyright (c) Meta Platforms, Inc. and affiliates.

/*
 * Score guard rails for numeric routing.
 *
 * Deterministic post-NN filter that masks provably-useless operation scores.
 * The masks prevent infinite compression loops caused by no-op or identity
 * transforms.
 */

#ifndef OPENZL_COMPRESS_SELECTORS_TRANSFORMER_SCORE_GUARD_H
#define OPENZL_COMPRESS_SELECTORS_TRANSFORMER_SCORE_GUARD_H

#include <stdint.h>
#include "generic_numeric_ops.h"
#include "numeric_features.h"
#include "numeric_features_v2.h"
#include "openzl/common/assertion.h"

/*
 * The guard rails depend only on exact integer fields that are shared by both
 * raw schemas, so project both contracts onto this narrow internal view.
 */
typedef struct {
    uint64_t cardinality_est;
    uint64_t range_u;
    uint64_t min_lb0;
    uint64_t min_u;
    uint64_t max_u;
} TRS_ScoreGuardFeatures;

static inline int TRS_score_guard_argmax_masked(
        const float* ranking_values,
        const float* guarded_scores,
        int n)
{
    ZL_ASSERT_GT(n, 0);

    if (!guarded_scores)
        return -1;

    const float* values = ranking_values ? ranking_values : guarded_scores;
    int best            = -1;
    for (int i = 0; i < n; i++) {
        if (guarded_scores[i] <= 0.0f) {
            continue;
        }
        if (best < 0 || values[i] > values[best]) {
            best = i;
        }
    }

    return best;
}

static inline void TRS_apply_score_guards_common(
        float* scores,
        int n,
        const TRS_GenericNumericOpId* op_ids,
        const TRS_ScoreGuardFeatures* f,
        uint64_t rp_threshold)
{
    /*
     * Range density is cardinality / domain_size, where domain_size is
     * range_u + 1. For a full-width unsigned span, range_u == UINT64_MAX and
     * the domain size is 2^64, which does not fit in uint64_t. Widen before
     * adding 1 so the dense-range tokenize guard does not wrap to zero and
     * misclassify the stream as maximally dense.
     */
    double rd = (double)f->cardinality_est / ((double)f->range_u + 1.0);
    /*
     * Preserve the float-rounded cutoff used by the training runtime. Changing
     * this to a double literal changes decisions at and just below 90%.
     */
    double const dense_range_threshold = (double)0.90f;

    for (int i = 0; i < n; i++) {
        TRS_GenericNumericOpId const op = op_ids[i];

        /*
         * min_u == 1 guarantees divide_by_gcd is a no-op. min_lb0 == 0
         * preserves the training policy for inputs with an odd nonzero value.
         */
        if ((f->min_lb0 == 0 || f->min_u == 1)
            && op == TRS_GENERIC_NUMERIC_OP_DIVIDE_BY_GCD)
            scores[i] = 0.0f;

        /* tokenize: pointless on dense-range data */
        if (rd > dense_range_threshold
            && (op == TRS_GENERIC_NUMERIC_OP_TOKENIZE_NUMERIC
                || op == TRS_GENERIC_NUMERIC_OP_TOKENIZE_NUMERIC_SORTED))
            scores[i] = 0.0f;

        /* range_pack: useless when min is already 0 and range exceeds
         * half-width (can't narrow to a smaller type) */
        if (f->min_u == 0 && f->max_u >= rp_threshold
            && op == TRS_GENERIC_NUMERIC_OP_RANGE_PACK)
            scores[i] = 0.0f;
    }
}

/*
 * Zero out scores for operations that are provably useless given the numeric
 * features.
 *
 *   scores      – mutable array of per-operation scores (modified in place)
 *   n           – number of operations (length of scores / op_ids)
 *   op_ids      – operation identifiers, indexed like scores
 *   f           – numeric features of the current stream
 *   rp_threshold – half-width boundary for range_pack guard
 *                  (num8: 16, num16: 256, num32: 65536, num64: 2^32)
 */
static inline void TRS_apply_score_guards(
        float* scores,
        int n,
        const TRS_GenericNumericOpId* op_ids,
        const TRS_NumericFeatures* f,
        uint64_t rp_threshold)
{
    const TRS_ScoreGuardFeatures guard_features = {
        .cardinality_est = f->cardinality_est,
        .range_u         = f->range_u,
        .min_lb0         = f->min_lb0,
        .min_u           = f->min_u,
        .max_u           = f->max_u,
    };
    TRS_apply_score_guards_common(
            scores, n, op_ids, &guard_features, rp_threshold);
}

static inline void TRS_apply_score_guards_v2(
        float* scores,
        int n,
        const TRS_GenericNumericOpId* op_ids,
        const TRS_NumericFeaturesV2* f,
        uint64_t rp_threshold)
{
    const TRS_ScoreGuardFeatures guard_features = {
        .cardinality_est = f->cardinality_est,
        .range_u         = f->range_u,
        .min_lb0         = f->min_lb0,
        .min_u           = f->min_u,
        .max_u           = f->max_u,
    };

    TRS_apply_score_guards_common(
            scores, n, op_ids, &guard_features, rp_threshold);
}

#endif /* OPENZL_COMPRESS_SELECTORS_TRANSFORMER_SCORE_GUARD_H */
