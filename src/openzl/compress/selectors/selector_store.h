// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_SELECTORS_SELECTOR_STORE_H
#define ZSTRONG_COMPRESS_SELECTORS_SELECTOR_STORE_H

#include "openzl/shared/portability.h"
#include "openzl/zl_selector.h"

ZL_BEGIN_C_DECLS

// Multi-Input version of generic Store
// Will dispatch each input to Store
ZL_Report
MultiInputGraph_store(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs);

#define MIGRAPH_STORE                                          \
    { .name                = "!zl.store",                      \
      .graph_f             = MultiInputGraph_store,            \
      .inputTypeMasks      = (const ZL_Type[]){ ZL_Type_any }, \
      .nbInputs            = 1,                                \
      .lastInputIsVariable = 1 }

// .selector_f = SI_selector_store, .inStreamType = ZL_Type_any,
ZL_GraphID SI_selector_store(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

ZL_END_C_DECLS

#endif
