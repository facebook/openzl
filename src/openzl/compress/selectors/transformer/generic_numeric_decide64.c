// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "generic_numeric_decide.h"

#include <string.h>

#include "numeric_extract.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/selectors/transformer/generated/score_numeric64.h"
#include "score_guard.h"

ZL_STATIC_ASSERT(
        TRS_SCORE_NUM64_N_OPERATIONS > 0,
        "num64 scorer must expose at least one operation");
ZL_STATIC_ASSERT(
        TRS_SCORE_NUM64_N_OPERATIONS <= TRS_GENERIC_NUMERIC_MAX_OPERATION_COUNT,
        "num64 scorer exceeds the decision score buffer");

static void generic_numeric_finish_decide64_v2(
        TRS_GenericNumericDecision64V2* decision)
{
    TRS_GenericNumericDecisionCore* const core = &decision->core;
    if (decision->features.range_s == 0) {
        core->best_op = TRS_GENERIC_NUMERIC_OP_CONSTANT;
        return;
    }

    core->score_count = TRS_SCORE_NUM64_N_OPERATIONS;
    TRS_score_num64_operations_with_logits(
            &decision->features, core->raw_scores, core->logits);
    memcpy(core->scores,
           core->raw_scores,
           sizeof(float) * (size_t)core->score_count);
    TRS_apply_score_guards_v2(
            core->scores,
            core->score_count,
            TRS_score_num64_operation_ids,
            &decision->features,
            TRS_numeric_half_width_threshold(8));
    core->best_index = TRS_score_guard_argmax_masked(
            core->logits, core->scores, core->score_count);
    if (core->best_index >= 0) {
        core->best_op = TRS_score_num64_operation_ids[core->best_index];
    }
}

TRS_GenericNumericDecision64V2 TRS_generic_numeric_decide64_v2(
        const uint8_t* data_bytes,
        size_t n_bytes,
        const TRS_NumericExtractWorkspace* workspace)
{
    TRS_GenericNumericDecision64V2 decision = {
        .core = {
            .best_op    = TRS_GENERIC_NUMERIC_OP_INVALID,
            .best_index = -1,
        },
    };

    if (!TRS_numericFeaturesV2_extract_from_bytes(
                &decision.features, data_bytes, n_bytes, 8, workspace)) {
        return decision;
    }
    generic_numeric_finish_decide64_v2(&decision);
    return decision;
}

TRS_GenericNumericDecision64V2 TRS_generic_numeric_decide64_v2_fast(
        const uint8_t* data_bytes,
        size_t n_bytes,
        const TRS_NumericExtractWorkspace* workspace)
{
    TRS_GenericNumericDecision64V2 decision = {
        .core = {
            .best_op    = TRS_GENERIC_NUMERIC_OP_INVALID,
            .best_index = -1,
        },
    };

    ZL_ASSERT_EQ(n_bytes % sizeof(uint64_t), 0);
    if (n_bytes != 0) {
        ZL_ASSERT_NN(data_bytes);
        ZL_ASSERT_EQ((uintptr_t)data_bytes % sizeof(uint64_t), (uintptr_t)0);
    }
    decision.features = TRS_numericFeaturesV2_extract_num64_decision(
            (const uint64_t*)(const void*)data_bytes,
            n_bytes / sizeof(uint64_t),
            workspace);
    generic_numeric_finish_decide64_v2(&decision);
    return decision;
}
