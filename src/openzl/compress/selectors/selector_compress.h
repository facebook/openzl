// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_SELECTORS_SELECTOR_COMPRESS_H
#define ZSTRONG_COMPRESS_SELECTORS_SELECTOR_COMPRESS_H

#include "openzl/shared/portability.h"
#include "openzl/zl_selector.h"

ZL_BEGIN_C_DECLS

// Multi-Input version of generic Compress
// Dispatches each Input to its own default Compress
ZL_Report
MultiInputGraph_compress(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs);

#define MIGRAPH_COMPRESS                                         \
    {                                                            \
        .name                = "!zl.compress_generic",           \
        .graph_f             = MultiInputGraph_compress,         \
        .inputTypeMasks      = (const ZL_Type[]){ ZL_Type_any }, \
        .nbInputs            = 1,                                \
        .lastInputIsVariable = 1,                                \
    }

// .selector_f = SI_selector_compress, .inStreamType = ZL_Type_any,
ZL_GraphID SI_selector_compress(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

// .selector_f   = SI_selector_compress_serial, .inStreamType = ZL_Type_serial,
ZL_GraphID SI_selector_compress_serial(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

// .selector_f   = SI_selector_compress_fixed_size, .inStreamType =
// ZL_Type_struct,
ZL_GraphID SI_selector_compress_struct(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

// .selector_f   = SI_selector_compress_numeric, .inStreamType =
// ZL_Type_numeric,
ZL_GraphID SI_selector_compress_numeric(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

// .selector_f   = SI_selector_compress_variable_size, .inStreamType =
// ZL_Type_string,
ZL_GraphID SI_selector_compress_string(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

ZL_END_C_DECLS

#endif
