// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/selectors/selector_numeric.h"
#include "openzl/compress/private_nodes.h"

ZL_GraphID SI_selector_numeric(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)selCtx;
    (void)inputStream;
    (void)customGraphs;
    (void)nbCustomGraphs;
    return ZL_GRAPH_STRUCT_COMPRESS;
}
