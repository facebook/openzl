// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/selectors/selector_numeric.h"
#include "openzl/codecs/zl_bitpack.h"
#include "openzl/codecs/zl_constant.h"
#include "openzl/codecs/zl_divide_by.h"
#include "openzl/codecs/zl_entropy.h"
#include "openzl/codecs/zl_field_lz.h"
#include "openzl/codecs/zl_sparse_num.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/codecs/zl_zstd.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/compress/selectors/transformer/generated/score_numeric16.h"
#include "openzl/compress/selectors/transformer/generated/score_numeric32.h"
#include "openzl/compress/selectors/transformer/generated/score_numeric64.h"
#include "openzl/compress/selectors/transformer/generated/score_numeric8.h"
#include "openzl/compress/selectors/transformer/generic_numeric_decide.h"
#include "openzl/compress/selectors/transformer/numeric_stats.h"
#include "openzl/compress/selectors/transformer/static_selectors.h"
#include "openzl/shared/overflow.h"
#include "openzl/zl_data.h"
#include "openzl/zl_input.h"
#include "openzl/zl_selector.h"

/*
 * Keep the default compression level (6) on the established numeric path.
 * Level 7 makes the newer Transformer policy explicitly opt-in, avoiding
 * unexpected selection changes for existing users. Reevaluate this threshold
 * after broader production validation.
 */
#define TRANSFORMER_MIN_COMPRESSION_LEVEL 7

static int transformer_operation_is_supported(
        TRS_GenericNumericOpId operation,
        SI_TransformerSupportedOperations supportedOperations)
{
    switch (operation) {
        case TRS_GENERIC_NUMERIC_OP_INVALID:
        case TRS_GENERIC_NUMERIC_OP_COUNT:
            return 0;
        case TRS_GENERIC_NUMERIC_OP_CONSTANT:
            return supportedOperations.constant;
        case TRS_GENERIC_NUMERIC_OP_DIVIDE_BY_GCD:
            return supportedOperations.divideByGcd;
        case TRS_GENERIC_NUMERIC_OP_SPARSE_NUM:
            return supportedOperations.sparseNum;
        /* Available in every format version supported by this selector. */
        case TRS_GENERIC_NUMERIC_OP_BITPACK:
        case TRS_GENERIC_NUMERIC_OP_DELTA_INT:
        case TRS_GENERIC_NUMERIC_OP_ENTROPY:
        case TRS_GENERIC_NUMERIC_OP_FIELD_LZ:
        case TRS_GENERIC_NUMERIC_OP_FSE:
        case TRS_GENERIC_NUMERIC_OP_RANGE_PACK:
        case TRS_GENERIC_NUMERIC_OP_STORE:
        case TRS_GENERIC_NUMERIC_OP_TOKENIZE_NUMERIC:
        case TRS_GENERIC_NUMERIC_OP_TOKENIZE_NUMERIC_SORTED:
        case TRS_GENERIC_NUMERIC_OP_ZSTD:
            return 1;
    }
    return 0;
}

static TRS_GenericNumericOpId transformer_select_supported_operation(
        SI_TransformerDecisionView decision,
        SI_TransformerSupportedOperations supportedOperations)
{
    if (decision.bestIndex < 0) {
        return transformer_operation_is_supported(
                       decision.bestOp, supportedOperations)
                ? decision.bestOp
                : TRS_GENERIC_NUMERIC_OP_INVALID;
    }

    ZL_ASSERT_GT(decision.scoreCount, 0);
    ZL_ASSERT_LT(decision.bestIndex, decision.scoreCount);
    ZL_ASSERT_NN(decision.scores);
    ZL_ASSERT_NN(decision.logits);
    ZL_ASSERT_NN(decision.operationIds);

    if (decision.scores[decision.bestIndex] <= 0.0f) {
        return TRS_GENERIC_NUMERIC_OP_INVALID;
    }

    TRS_GenericNumericOpId const bestOperation =
            decision.operationIds[decision.bestIndex];
    if (transformer_operation_is_supported(
                bestOperation, supportedOperations)) {
        return bestOperation;
    }

    int bestSupported = -1;
    for (int index = 0; index < decision.scoreCount; ++index) {
        TRS_GenericNumericOpId const operation = decision.operationIds[index];
        if (decision.scores[index] <= 0.0f
            || !transformer_operation_is_supported(
                    operation, supportedOperations)) {
            continue;
        }
        if (bestSupported < 0
            || decision.logits[index] > decision.logits[bestSupported]) {
            bestSupported = index;
        }
    }
    return bestSupported >= 0 ? decision.operationIds[bestSupported]
                              : TRS_GENERIC_NUMERIC_OP_INVALID;
}

static ZL_GraphID transformer_graph_for_operation(
        TRS_GenericNumericOpId operation)
{
    switch (operation) {
        case TRS_GENERIC_NUMERIC_OP_CONSTANT:
            return ZL_GRAPH_CONSTANT;
        case TRS_GENERIC_NUMERIC_OP_BITPACK:
            return ZL_GRAPH_BITPACK;
        case TRS_GENERIC_NUMERIC_OP_DELTA_INT:
            return ZL_GRAPH_TRANSFORMER_DELTA_INT;
        case TRS_GENERIC_NUMERIC_OP_DIVIDE_BY_GCD:
            return ZL_GRAPH_TRANSFORMER_DIVIDE_BY_GCD;
        case TRS_GENERIC_NUMERIC_OP_ENTROPY:
            return ZL_GRAPH_ENTROPY;
        case TRS_GENERIC_NUMERIC_OP_FIELD_LZ:
            return ZL_GRAPH_FIELD_LZ;
        case TRS_GENERIC_NUMERIC_OP_FSE:
            return ZL_GRAPH_FSE;
        case TRS_GENERIC_NUMERIC_OP_RANGE_PACK:
            return ZL_GRAPH_TRANSFORMER_RANGE_PACK;
        case TRS_GENERIC_NUMERIC_OP_SPARSE_NUM:
            return ZL_GRAPH_TRANSFORMER_SPARSE_NUM;
        case TRS_GENERIC_NUMERIC_OP_STORE:
            return ZL_GRAPH_STORE;
        case TRS_GENERIC_NUMERIC_OP_TOKENIZE_NUMERIC:
            return ZL_GRAPH_TRANSFORMER_TOKENIZE_NUMERIC;
        case TRS_GENERIC_NUMERIC_OP_TOKENIZE_NUMERIC_SORTED:
            return ZL_GRAPH_TRANSFORMER_TOKENIZE_NUMERIC_SORTED;
        case TRS_GENERIC_NUMERIC_OP_ZSTD:
            return ZL_GRAPH_ZSTD;
        case TRS_GENERIC_NUMERIC_OP_INVALID:
        case TRS_GENERIC_NUMERIC_OP_COUNT:
            return ZL_GRAPH_TRANSFORMER_STATIC_FALLBACK;
    }
    return ZL_GRAPH_TRANSFORMER_STATIC_FALLBACK;
}

ZL_GraphID SI_transformer_select_supported_graph(
        SI_TransformerDecisionView decision,
        SI_TransformerSupportedOperations supportedOperations)
{
    return transformer_graph_for_operation(
            transformer_select_supported_operation(
                    decision, supportedOperations));
}

static ZL_GraphID transformer_graph_for_decision(
        const TRS_GenericNumericDecisionCore* decision,
        const TRS_GenericNumericOpId* operationIds,
        SI_TransformerSupportedOperations supportedOperations)
{
    return SI_transformer_select_supported_graph(
            (SI_TransformerDecisionView){
                    .bestOp       = decision->best_op,
                    .bestIndex    = decision->best_index,
                    .scoreCount   = decision->score_count,
                    .scores       = decision->scores,
                    .logits       = decision->logits,
                    .operationIds = operationIds,
            },
            supportedOperations);
}

static SI_TransformerSupportedOperations transformer_supported_operations(
        const ZL_Selector* selCtx)
{
    return (SI_TransformerSupportedOperations){
        .constant = ZL_Selector_isNodeSupported(selCtx, ZL_NODE_CONSTANT_FIXED)
                && ZL_Selector_isNodeSupported(selCtx, ZL_NODE_CONSTANT_SERIAL),
        .divideByGcd = ZL_Selector_isNodeSupported(selCtx, ZL_NODE_DIVIDE_BY),
        .sparseNum   = ZL_Selector_isNodeSupported(selCtx, ZL_NODE_SPARSE_NUM),
    };
}

static int transformer_numeric_init_legacy_scratch(
        const ZL_Selector* selCtx,
        size_t cardinalityBitmapWords,
        TRS_NumericExtractWorkspace* workspace)
{
    size_t const kmvSize =
            sizeof(*workspace->kmv_entries) * (size_t)TRS_NUMERIC_KMV_K;
    workspace->kmv_entries =
            (TRS_NumericKmvEntry*)ZL_Selector_getScratchSpace(selCtx, kmvSize);
    if (workspace->kmv_entries == NULL) {
        return 0;
    }
    workspace->kmv_capacity = TRS_NUMERIC_KMV_K;

    size_t const bitmapSize =
            sizeof(*workspace->cardinality_bitmap) * cardinalityBitmapWords;
    workspace->cardinality_bitmap =
            (uint64_t*)ZL_Selector_getScratchSpace(selCtx, bitmapSize);
    if (workspace->cardinality_bitmap == NULL) {
        return 0;
    }
    workspace->cardinality_bitmap_capacity = cardinalityBitmapWords;
    return 1;
}

static int transformer_numeric_init_workspace(
        const ZL_Selector* selCtx,
        size_t nElements,
        size_t eltWidth,
        TRS_NumericExtractWorkspace* workspace)
{
    if (workspace == NULL) {
        return 0;
    }
    *workspace = (TRS_NumericExtractWorkspace){ 0 };

    if (eltWidth == 1) {
        size_t const matchTableSize =
                sizeof(uint32_t) * (size_t)TRS_NUMERIC_MATCH4_TABLE_ENTRIES;
        workspace->match4_table =
                (uint32_t*)ZL_Selector_getScratchSpace(selCtx, matchTableSize);
        if (workspace->match4_table == NULL) {
            return 0;
        }
        workspace->match4_capacity = TRS_NUMERIC_MATCH4_TABLE_ENTRIES;
        return transformer_numeric_init_legacy_scratch(
                selCtx, TRS_NUMERIC_CARDINALITY_SCRATCH_WORDS, workspace);
    }

    if (eltWidth == 8) {
        size_t const sortedGapBufferSize = sizeof(uint64_t)
                * (size_t)TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES;
        workspace->sorted_gap_buffer = (uint64_t*)ZL_Selector_getScratchSpace(
                selCtx, sortedGapBufferSize);
        if (workspace->sorted_gap_buffer == NULL) {
            return 0;
        }
        workspace->sorted_gap_capacity = TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES;
        return 1;
    }

    if (eltWidth != 2) {
        return 1;
    }

    size_t workspaceSize;
    if (ZL_overflowMulST(nElements, sizeof(uint32_t), &workspaceSize)) {
        return 0;
    }

    workspace->values =
            (uint32_t*)ZL_Selector_getScratchSpace(selCtx, workspaceSize);
    if (workspace->values == NULL) {
        return 0;
    }
    workspace->capacity = nElements;
    return transformer_numeric_init_legacy_scratch(
            selCtx, TRS_CARDINALITY_U16_BITMAP_WORDS, workspace);
}

ZL_GraphID SI_transformer_numeric_select(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)customGraphs;
    (void)nbCustomGraphs;

    ZL_ASSERT_EQ(ZL_Input_type(inputStream), ZL_Type_numeric);
    size_t const eltWidth = ZL_Input_eltWidth(inputStream);
    ZL_ASSERT(
            eltWidth == 1 || eltWidth == 2 || eltWidth == 4 || eltWidth == 8,
            "numeric element width must be 1, 2, 4, or 8");
    if (ZL_Input_numElts(inputStream) == 0) {
        return ZL_GRAPH_STORE;
    }
    if (ZL_Selector_getGraphDepth(selCtx) > TRS_TRANSFORMER_MAX_MODEL_DEPTH) {
        return ZL_GRAPH_TRANSFORMER_STATIC_FALLBACK;
    }

    const uint8_t* const data = ZL_Input_ptr(inputStream);
    size_t const size         = ZL_Input_contentSize(inputStream);
    SI_TransformerSupportedOperations const supportedOperations =
            transformer_supported_operations(selCtx);
    TRS_NumericExtractWorkspace workspace;
    /*
     * Feature extraction is optional: if scratch reservation fails, preserve
     * compression by using the static fallback, which needs no workspace.
     */
    if (!transformer_numeric_init_workspace(
                selCtx, ZL_Input_numElts(inputStream), eltWidth, &workspace)) {
        return ZL_GRAPH_TRANSFORMER_STATIC_FALLBACK;
    }

    if (eltWidth == 1) {
        TRS_GenericNumericDecision decision =
                TRS_generic_numeric_decide8(data, size, &workspace);
        return transformer_graph_for_decision(
                &decision.core,
                TRS_score_num8_operation_ids,
                supportedOperations);
    }
    if (eltWidth == 2) {
        TRS_GenericNumericDecision decision =
                TRS_generic_numeric_decide16_fast(data, size, &workspace);
        return transformer_graph_for_decision(
                &decision.core,
                TRS_score_num16_operation_ids,
                supportedOperations);
    }
    if (eltWidth == 4) {
        TRS_GenericNumericDecision decision =
                TRS_generic_numeric_decide32_fast(data, size);
        return transformer_graph_for_decision(
                &decision.core,
                TRS_score_num32_operation_ids,
                supportedOperations);
    }

    ZL_ASSERT_EQ(eltWidth, 8);
    TRS_GenericNumericDecision64V2 decision =
            TRS_generic_numeric_decide64_v2_fast(data, size, &workspace);
    return transformer_graph_for_decision(
            &decision.core, TRS_score_num64_operation_ids, supportedOperations);
}

ZL_GraphID SI_selector_numeric(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)inputStream;
    (void)customGraphs;
    (void)nbCustomGraphs;

    int const compressionLevel =
            ZL_Selector_getCParam(selCtx, ZL_CParam_compressionLevel);
    if (compressionLevel < TRANSFORMER_MIN_COMPRESSION_LEVEL) {
        return ZL_GRAPH_STRUCT_COMPRESS;
    }
    return ZL_GRAPH_TRANSFORMER_NUMERIC;
}
