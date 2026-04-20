// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/directed_selector.h" // @manual

#include "openzl/zl_data.h"
#include "openzl/zl_selector.h"

static ZL_GraphID directed_selector_impl(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)selCtx;
    assert(customGraphs != NULL);
    assert(nbCustomGraphs > 0);
    ZL_IntMetadata metadata =
            ZL_Input_getIntMetadata(inputStream, kDirectedSelectorMetadataID);
    assert(metadata.isPresent);
    size_t idx = metadata.mValue;
    assert(idx < nbCustomGraphs);
    if (!metadata.isPresent || idx >= nbCustomGraphs || customGraphs == NULL) {
        return (ZL_GraphID){
            0
        }; // return an illegal graph id to indicate failure.
    }
    return customGraphs[idx];
}

ZL_GraphID registerDirectedSelectorBaseGraph(ZL_Compressor* compressor)
{
    ZL_GraphID baseGraph =
            ZL_Compressor_getGraph(compressor, "zl_custom.directed_selector");
    if (baseGraph.gid != ZL_GRAPH_ILLEGAL.gid) {
        return baseGraph;
    }

    ZL_SelectorDesc const baseDesc = {
        .selector_f     = directed_selector_impl,
        .inStreamType   = ZL_Type_any,
        .customGraphs   = NULL,
        .nbCustomGraphs = 0,
        .localParams    = {},
        .name           = "!zl_custom.directed_selector",
    };
    return ZL_Compressor_registerSelectorGraph(compressor, &baseDesc);
}

ZL_GraphID registerDirectedSelectorGraph(
        ZL_Compressor* compressor,
        const ZL_GraphID* successors,
        size_t nbSuccessors)
{
    ZL_GraphID baseGraph = registerDirectedSelectorBaseGraph(compressor);

    ZL_ParameterizedGraphDesc const paramDesc = {
        .graph          = baseGraph,
        .customGraphs   = successors,
        .nbCustomGraphs = nbSuccessors,
    };
    return ZL_Compressor_registerParameterizedGraph(compressor, &paramDesc);
}
