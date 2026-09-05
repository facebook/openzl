// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_SELECTORS_SELECTOR_NUMERIC_H
#define ZSTRONG_COMPRESS_SELECTORS_SELECTOR_NUMERIC_H

#include "openzl/compress/selectors/transformer/generic_numeric_ops.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_selector.h"

ZL_BEGIN_C_DECLS

typedef struct {
    TRS_GenericNumericOpId bestOp;
    int bestIndex;
    int scoreCount;
    const float* scores;
    const float* logits;
    const TRS_GenericNumericOpId* operationIds;
} SI_TransformerDecisionView;

typedef struct {
    bool constant;
    bool divideByGcd;
    bool sparseNum;
} SI_TransformerSupportedOperations;

/**
 * Select a graph after masking unsupported operations from a Transformer
 * decision.
 *
 * If the preferred operation is unavailable, selects the highest-ranked
 * supported operation whose guarded score is positive. Returns the static
 * fallback when no supported candidate remains. Constant decisions have no
 * ranked alternatives and therefore fall back directly when unavailable. A
 * ranked decision (`bestIndex >= 0`) must have a positive `scoreCount`, an
 * in-range `bestIndex`, and non-null `scores`, `logits`, and `operationIds`
 * arrays.
 */
ZL_GraphID SI_transformer_select_supported_graph(
        SI_TransformerDecisionView decision,
        SI_TransformerSupportedOperations supportedOperations);

// Direct entry point for the pretrained numeric Transformer.
// .selector_f   = SI_transformer_numeric_select,
// .inStreamType = ZL_Type_numeric
ZL_GraphID SI_transformer_numeric_select(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

// .selector_f   = SI_selector_numeric,
// .inStreamType = ZL_Type_numeric
ZL_GraphID SI_selector_numeric(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_SELECTORS_SELECTOR_NUMERIC_H
