// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/empty_input_selector.h" // @manual

#include "openzl/zl_data.h"

static ZL_GraphID empty_input_selector_impl(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)selCtx;
    if (customGraphs == NULL || nbCustomGraphs != 2) {
        return (ZL_GraphID){
            0
        }; // return an illegal graph id to indicate failure.
    }
    size_t nbElts = ZL_Input_numElts(inputStream);
    return customGraphs[!!nbElts];
}

ZL_SelectorDesc buildEmptyInputSelectorDesc(
        ZL_Type type,
        const ZL_GraphID* successors,
        size_t nbSuccessors)
{
    return (ZL_SelectorDesc){
        .selector_f     = empty_input_selector_impl,
        .inStreamType   = type,
        .customGraphs   = successors,
        .nbCustomGraphs = nbSuccessors,
        .localParams = { .intParams  = { .intParams = NULL, .nbIntParams = 0 },
                         .copyParams = { .copyParams   = NULL,
                                         .nbCopyParams = 0 } },
    };
}

ZL_GraphID registerEmptyInputSelectorBaseGraph(ZL_Compressor* compressor)
{
    ZL_GraphID baseGraph = ZL_Compressor_getGraph(
            compressor, "zl_custom.empty_input_selector");
    if (baseGraph.gid != ZL_GRAPH_ILLEGAL.gid) {
        return baseGraph;
    }

    ZL_SelectorDesc const baseDesc = {
        .selector_f     = empty_input_selector_impl,
        .inStreamType   = ZL_Type_any,
        .customGraphs   = NULL,
        .nbCustomGraphs = 0,
        .localParams = { .intParams  = { .intParams = NULL, .nbIntParams = 0 },
                         .copyParams = { .copyParams   = NULL,
                                         .nbCopyParams = 0 } },
        .name        = "!zl_custom.empty_input_selector",
    };
    return ZL_Compressor_registerSelectorGraph(compressor, &baseDesc);
}

ZL_GraphID registerEmptyInputSelectorGraph(
        ZL_Compressor* compressor,
        const ZL_GraphID* successors,
        size_t nbSuccessors)
{
    ZL_GraphID baseGraph = registerEmptyInputSelectorBaseGraph(compressor);

    ZL_ParameterizedGraphDesc const paramDesc = {
        .graph          = baseGraph,
        .customGraphs   = successors,
        .nbCustomGraphs = nbSuccessors,
    };
    return ZL_Compressor_registerParameterizedGraph(compressor, &paramDesc);
}
