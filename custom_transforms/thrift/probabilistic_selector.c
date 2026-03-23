// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/probabilistic_selector.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/xxhash.h"
#include "openzl/zl_graph_api.h"

#define ZS2_PROBABILISTIC_SELECTOR_PROBABILITIES_CTID 85

static ZL_Report
probabilisticSelectorImpl(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    ZL_GraphIDList customGraphs = ZL_Graph_getCustomGraphs(gctx);
    size_t nbCustomGraphs       = customGraphs.nbGraphIDs;
    ZL_ERR_IF_EQ(customGraphs.nbGraphIDs, 0, node_invalid_input);

    const size_t* probWeights =
            (const size_t*)ZL_Graph_getLocalRefParam(
                    gctx, ZS2_PROBABILISTIC_SELECTOR_PROBABILITIES_CTID)
                    .paramRef;
    size_t totalWeight = 0;
    for (size_t i = 0; i < nbCustomGraphs; ++i) {
        // Weight overflow
        ZL_ERR_IF_GT(
                totalWeight, SIZE_MAX - probWeights[i], node_invalid_input);
        totalWeight += probWeights[i];
    }
    XXH32_hash_t hash = 0;
    // Gets hash by xoring the hash of each individual input together
    for (size_t i = 0; i < nbInputs; ++i) {
        const ZL_Input* inputStream = ZL_Edge_getData(inputs[i]);
        const void* in              = ZL_Input_ptr(inputStream);
        size_t const intWidth       = ZL_Input_eltWidth(inputStream);
        ZL_ASSERT(
                intWidth == 1 || intWidth == 2 || intWidth == 4
                || intWidth == 8);
        size_t const nbElts = ZL_Input_numElts(inputStream);
        hash ^= XXH32(in, nbElts * intWidth, 0);
    }
    size_t rand        = (hash * totalWeight) >> 32;
    size_t selectedIdx = 0;
    size_t accumWeight = 0;
    for (size_t i = 0; i < nbCustomGraphs; ++i) {
        accumWeight += probWeights[i];
        if (rand < accumWeight) {
            selectedIdx = i;
            break;
        }
    }
    // Passes the input to the input of the successor, acting as a selector
    return ZL_Edge_setParameterizedDestination(
            inputs, nbInputs, customGraphs.graphids[selectedIdx], NULL);
}

ZL_GraphID getProbabilisticSelectorGraph(
        ZL_Compressor* cgraph,
        const size_t* probWeights,
        const ZL_GraphID* successors,
        size_t nbSuccessors,
        const ZL_Type* types,
        size_t nbInputs)
{
    ZL_CopyParam copyParam = {
        .paramId   = ZS2_PROBABILISTIC_SELECTOR_PROBABILITIES_CTID,
        .paramPtr  = probWeights,
        .paramSize = nbSuccessors * sizeof(size_t)
    };
    ZL_FunctionGraphDesc multiInputGraphDesc = (ZL_FunctionGraphDesc){
        .graph_f        = probabilisticSelectorImpl,
        .inputTypeMasks = types,
        .nbInputs       = nbInputs,
        .customGraphs   = successors,
        .nbCustomGraphs = nbSuccessors,
        .localParams    = { .copyParams = { .copyParams   = &copyParam,
                                            .nbCopyParams = 1 } },
    };
    return ZL_Compressor_registerFunctionGraph(cgraph, &multiInputGraphDesc);
}
