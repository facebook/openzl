// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_SELECTORS_TRANSFORMER_GENERIC_NUMERIC_DECIDE_H
#define OPENZL_COMPRESS_SELECTORS_TRANSFORMER_GENERIC_NUMERIC_DECIDE_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint8_t */

#include "generic_numeric_ops.h"
#include "numeric_extract.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

typedef struct {
    TRS_GenericNumericOpId best_op;
    int best_index;  /* Width-local scorer index, or -1 for constant/invalid. */
    int score_count; /* Number of valid entries in scores[]. */
    float scores[TRS_GENERIC_NUMERIC_MAX_OPERATION_COUNT]; /* Post-guard scores
                                                          used for routing. */
    float raw_scores[TRS_GENERIC_NUMERIC_MAX_OPERATION_COUNT]; /* Pre-guard
                                                              sigmoid outputs.
                                                            */
    float logits[TRS_GENERIC_NUMERIC_MAX_OPERATION_COUNT];     /* Pre-sigmoid
                                                                * outputs.
                                                                */
} TRS_GenericNumericDecisionCore;

typedef struct {
    TRS_NumericFeatures features;
    TRS_GenericNumericDecisionCore core;
} TRS_GenericNumericDecision;

/*
 * Width-correct num64 decision payload.
 *
 * Sub-64 routes still use TRS_GenericNumericDecision; num64 uses the V2 raw
 * contract directly.
 */
typedef struct {
    TRS_NumericFeaturesV2 features;
    TRS_GenericNumericDecisionCore core;
} TRS_GenericNumericDecision64V2;

/*
 * Num8 extraction requires the match4, KMV, and cardinality workspace buffers
 * documented by `TRS_numericFeatures_extract_from_bytes()`.
 */
TRS_GenericNumericDecision TRS_generic_numeric_decide8(
        const uint8_t* data_bytes,
        size_t n_bytes,
        const TRS_NumericExtractWorkspace* workspace);

/*
 * Full decisions require the workspace documented by the corresponding full
 * extractor. The num16 fast path requires its widening, KMV, and cardinality
 * buffers. Num32 fast extraction retains its small local KMV sample and needs
 * no workspace. Num64 full and fast decisions require a sorted-gap buffer; for
 * the fast path this is a programming precondition. Insufficient workspace
 * produces an INVALID decision where extraction exposes a recoverable error.
 * Byte counts must contain complete elements. Num32 and num64 inputs must be
 * naturally aligned, as guaranteed for OpenZL numeric streams. Non-empty
 * inputs must have a non-null data pointer. Violating these input-shape
 * requirements is a programming error.
 */
TRS_GenericNumericDecision TRS_generic_numeric_decide16(
        const uint8_t* data_bytes,
        size_t n_bytes,
        const TRS_NumericExtractWorkspace* workspace);

TRS_GenericNumericDecision TRS_generic_numeric_decide16_fast(
        const uint8_t* data_bytes,
        size_t n_bytes,
        const TRS_NumericExtractWorkspace* workspace);

TRS_GenericNumericDecision TRS_generic_numeric_decide32(
        const uint8_t* data_bytes,
        size_t n_bytes,
        const TRS_NumericExtractWorkspace* workspace);

TRS_GenericNumericDecision TRS_generic_numeric_decide32_fast(
        const uint8_t* data_bytes,
        size_t n_bytes);

TRS_GenericNumericDecision64V2 TRS_generic_numeric_decide64_v2(
        const uint8_t* data_bytes,
        size_t n_bytes,
        const TRS_NumericExtractWorkspace* workspace);

TRS_GenericNumericDecision64V2 TRS_generic_numeric_decide64_v2_fast(
        const uint8_t* data_bytes,
        size_t n_bytes,
        const TRS_NumericExtractWorkspace* workspace);

ZL_END_C_DECLS

#endif /* OPENZL_COMPRESS_SELECTORS_TRANSFORMER_GENERIC_NUMERIC_DECIDE_H */
