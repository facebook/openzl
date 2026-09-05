// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_SELECTORS_TRANSFORMER_STATIC_SELECTORS_H
#define OPENZL_COMPRESS_SELECTORS_TRANSFORMER_STATIC_SELECTORS_H

#include "openzl/shared/portability.h"
#include "openzl/zl_selector.h"

ZL_BEGIN_C_DECLS

/*
 * Recursive model-selected operations stop at depth 24. This was validated on
 * the training and held-out datasets: no beneficial graph approached that
 * depth, while pathological decisions could otherwise repeat transforms or
 * build uselessly long chains. The static fallback may add a short compound
 * cleanup pipeline, so allow six additional graph levels before forcing its
 * terminal codec.
 */
#define TRS_TRANSFORMER_MAX_MODEL_DEPTH 24
#define TRS_TRANSFORMER_STATIC_DEPTH_MARGIN 6
#define TRS_TRANSFORMER_MAX_STATIC_DEPTH \
    (TRS_TRANSFORMER_MAX_MODEL_DEPTH + TRS_TRANSFORMER_STATIC_DEPTH_MARGIN)

/*
 * General recursive policy used after static numeric transforms. It selects
 * another transform or a terminal codec for the resulting numeric stream.
 */
ZL_GraphID SI_transformer_static_core_select(
        const ZL_Selector* selCtx,
        const ZL_Input* input,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

/*
 * Entry point used when model selection cannot produce a supported operation.
 * It tries specialized tokenize gates before delegating to the core policy.
 */
ZL_GraphID SI_transformer_static_fallback_select(
        const ZL_Selector* selCtx,
        const ZL_Input* input,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

/*
 * Successor policy for the index stream emitted by tokenize_sorted. It may
 * delta-code localized indices before delegating to the core policy.
 */
ZL_GraphID SI_transformer_static_index_select(
        const ZL_Selector* selCtx,
        const ZL_Input* input,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

ZL_END_C_DECLS

#endif /* OPENZL_COMPRESS_SELECTORS_TRANSFORMER_STATIC_SELECTORS_H */
