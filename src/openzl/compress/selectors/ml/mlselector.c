// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/selectors/ml/mlselector.h"
#include "openzl/zl_public_nodes.h" // ZL_GRAPH_ILLEGAL
#include "openzl/zl_selector.h"     // TypedSelector

enum { cgraph_gpid_ml_selector = 45012 };

typedef struct {
    /**
     * Struct used to pass necessary information through opaque. The model is
     * used to get the prediction function and corresponding context (opaque).
     * We use the inStreamType to verify that the we are using the correct
     * prediction function based on the stream. We use the graphs to match the
     * resulting label from the prediction function.
     */
    ZL_MLModel_Desc model;
    ZL_Type inStreamType;
    ZL_LabeledGraphID* graphs;
    size_t nbGraphs;
} MLSelector;

static MLSelector* createMLSelector(const ZL_MLSelectorDesc* csd)
{
    MLSelector* dst = malloc(sizeof(MLSelector));
    if (dst == NULL) {
        return NULL;
    }

    ZL_LabeledGraphID* graphs =
            malloc(csd->nbGraphs * sizeof(ZL_LabeledGraphID));
    if (graphs == NULL) {
        return NULL;
    }

    memcpy(graphs, csd->graphs, csd->nbGraphs * sizeof(ZL_LabeledGraphID));

    dst->model        = csd->model;
    dst->inStreamType = csd->inStreamType;
    dst->graphs       = graphs;
    dst->nbGraphs     = csd->nbGraphs;

    return dst;
}

static void freeMLSelector(void* opaque, void* paramPtr)
{
    (void)opaque;
    ZL_ASSERT_NULL(opaque);
    MLSelector* mlSelector = (MLSelector*)paramPtr;
    if (mlSelector == NULL) {
        return;
    }

    if (mlSelector->model.free != NULL) {
        mlSelector->model.free(mlSelector->model.opaque);
    }

    free(mlSelector->graphs);
    free(mlSelector);
}

static ZL_GraphID ML_selector(
        const ZL_Selector* selCtx,
        const ZL_Input* in,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)customGraphs;
    (void)nbCustomGraphs;

    ZL_ASSERT_NN(in);
    ZL_ASSERT_NN(selCtx);

    const MLSelector* mlSelector = ZL_Selector_getOpaquePtr(selCtx);

    ZL_ASSERT_EQ(ZL_Input_type(in), mlSelector->inStreamType);

    const char* label = mlSelector->model.predict(mlSelector->model.opaque, in);
    size_t graphIdx   = mlSelector->nbGraphs;
    for (size_t i = 0; i < mlSelector->nbGraphs; i++) {
        if (!strcmp(mlSelector->graphs[i].label, label)) {
            graphIdx = i;
            break;
        }
    }

    if (graphIdx >= mlSelector->nbGraphs) {
        return ZL_GRAPH_ILLEGAL;
    }
    return mlSelector->graphs[graphIdx].graph;
}

ZL_GraphID ZL_Compressor_registerMLSelectorGraph(
        ZL_Compressor* cgraph,
        const ZL_MLSelectorDesc* csd)
{
    ZL_DLOG(BLOCK, "ZL_Compressor_registerMLSelectorGraph");

    ZL_SelectorDesc const tselDesc = {
        .selector_f     = ML_selector,
        .inStreamType   = csd->inStreamType,
        .customGraphs   = NULL, // NULL since we are using labeledGraphs instead
        .nbCustomGraphs = 0,
        .name           = csd->name,
        .opaque         = { createMLSelector(csd), NULL, freeMLSelector },
    };

    return ZL_Compressor_registerSelectorGraph(cgraph, &tselDesc);
}
