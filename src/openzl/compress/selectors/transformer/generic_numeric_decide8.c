// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "generic_numeric_decide.h"

#include <string.h>

#include "numeric_extract.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/selectors/transformer/generated/score_numeric8.h"
#include "score_guard.h"

ZL_STATIC_ASSERT(
        TRS_SCORE_NUM8_N_OPERATIONS > 0,
        "num8 scorer must expose at least one operation");
ZL_STATIC_ASSERT(
        TRS_SCORE_NUM8_N_OPERATIONS <= TRS_GENERIC_NUMERIC_MAX_OPERATION_COUNT,
        "num8 scorer exceeds the decision score buffer");

TRS_GenericNumericDecision TRS_generic_numeric_decide8(
        const uint8_t* data_bytes,
        size_t n_bytes,
        const TRS_NumericExtractWorkspace* workspace)
{
    TRS_GenericNumericDecision decision = {
        .core = {
            .best_op    = TRS_GENERIC_NUMERIC_OP_INVALID,
            .best_index = -1,
        },
    };
    TRS_GenericNumericDecisionCore* const core = &decision.core;

    if (!TRS_numericFeatures_extract_from_bytes(
                &decision.features, data_bytes, n_bytes, 1, workspace)) {
        return decision;
    }
    if (decision.features.range_s == 0) {
        core->best_op = TRS_GENERIC_NUMERIC_OP_CONSTANT;
        return decision;
    }

    core->score_count = TRS_SCORE_NUM8_N_OPERATIONS;
    TRS_score_num8_operations_with_logits(
            &decision.features, core->raw_scores, core->logits);
    memcpy(core->scores,
           core->raw_scores,
           sizeof(float) * (size_t)core->score_count);
    TRS_apply_score_guards(
            core->scores,
            core->score_count,
            TRS_score_num8_operation_ids,
            &decision.features,
            TRS_numeric_half_width_threshold(1));
    core->best_index = TRS_score_guard_argmax_masked(
            core->logits, core->scores, core->score_count);
    if (core->best_index >= 0) {
        core->best_op = TRS_score_num8_operation_ids[core->best_index];
    }
    return decision;
}
