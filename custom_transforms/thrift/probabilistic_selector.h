// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/zl_graph_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A selector that chooses between a set of successors with a weighted
 * probability. It must be guaranteed that there is exactly one probability
 * weight for each successor.
 */
ZL_GraphID getProbabilisticSelectorGraph(

        ZL_Compressor* cgraph,
        const size_t* probWeights,
        const ZL_GraphID* successors,
        size_t nbSuccessors,
        const ZL_Type* types,
        size_t nbInputs);
#ifdef __cplusplus
}
#endif
