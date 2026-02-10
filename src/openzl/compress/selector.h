// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_SELECTOR_H
#define ZSTRONG_COMPRESS_SELECTOR_H

#include "openzl/common/allocation.h" // Arena
#include "openzl/shared/portability.h"
#include "openzl/zl_compress.h" // ZL_CCtx, ZL_Report
#include "openzl/zl_selector.h"

ZL_BEGIN_C_DECLS

typedef struct SelectorSuccessorParams_s {
    ZL_LocalParams* params;
} SelectorSuccessorParams;

struct ZL_Selector_s {
    /// link to parent cctx
    ZL_CCtx* cctx;
    /// Parameters passed from cctx
    const ZL_LocalParams* lparams;
    /// Parameters designated for the successor graph. Note: within
    /// graph_registry.c this is allocated from the wkspArena, so should be read
    /// before the arena is cleared via SI_destroySelectorCtx().
    SelectorSuccessorParams* successorLParams;
    /// Allocator to use for temporary allocations that
    /// are scoped to the current context
    Arena* wkspArena;
    const void* opaque;
    /// Current graph execution depth
    unsigned depth;
}; /// typedef'd to ZL_Selector within zs2_transform_api.h

ZL_Report SelCtx_initSelectorCtx(
        ZL_Selector* selCtx,
        ZL_CCtx* cctx,
        Arena* wkspArena,
        const ZL_LocalParams* lparams,
        SelectorSuccessorParams* successorLParams,
        const void* opaque,
        unsigned depth);

void SelCtx_destroySelectorCtx(ZL_Selector* selCtx);

ZL_END_C_DECLS

#endif
