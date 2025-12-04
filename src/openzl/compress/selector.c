// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/selector.h" // ZL_Selector definition
#include "openzl/compress/cctx.h"     // CCTX_*
#include "openzl/compress/graphmgr.h" // GM_*
#include "openzl/compress/localparams.h"
#include "openzl/zl_data.h"
#include "openzl/zl_reflection.h" // ZL_Compressor_Graph_getInput0Mask
#include "openzl/zl_selector.h"   // ZL_GraphReport, ZL_Selector_tryGraph def

ZL_Report SelCtx_initSelectorCtx(
        ZL_Selector* selCtx,
        ZL_CCtx* cctx,
        Arena* wkspArena,
        const ZL_LocalParams* lparams,
        SelectorSuccessorParams* successorLParams,
        const void* opaque)
{
    ZL_ASSERT_NN(selCtx);
    ZL_ASSERT_NN(wkspArena);
    *selCtx = (ZL_Selector){ .cctx             = cctx,
                             .wkspArena        = wkspArena,
                             .lparams          = lparams,
                             .successorLParams = successorLParams,
                             .opaque           = opaque };
    return ZL_returnSuccess();
}

void SelCtx_destroySelectorCtx(ZL_Selector* selCtx)
{
    ZL_ASSERT_NN(selCtx);
    ALLOC_Arena_freeAll(selCtx->wkspArena);
}

ZL_Type ZL_Selector_getInput0MaskForGraph(
        ZL_Selector const* selCtx,
        ZL_GraphID gid)
{
    ZL_ASSERT_NN(selCtx);
    ZL_ASSERT_NN(selCtx->cctx);
    const ZL_Compressor* cgraph = CCTX_getCGraph(selCtx->cctx);
    return ZL_Compressor_Graph_getInput0Mask(cgraph, gid);
}

ZL_GraphReport ZL_Selector_tryGraph(
        const ZL_Selector* selCtx,
        const ZL_Input* input,
        ZL_GraphID graphid)
{
    ZL_RESULT_OF(ZL_GraphPerformance)
    perf = CCTX_tryGraph(
            selCtx->cctx, &input, 1, selCtx->wkspArena, graphid, NULL);
    ZL_GraphReport ret;
    if (ZL_RES_isError(perf)) {
        ret.finalCompressedSize = ZL_returnError(ZL_RES_code(perf));
    } else {
        ret.finalCompressedSize =
                ZL_returnValue(ZL_RES_value(perf).compressedSize);
    }
    return ret;
}

ZL_Report ZL_Selector_setSuccessorParams(
        const ZL_Selector* selCtx,
        const ZL_LocalParams* lparams)
{
    if (lparams) {
        ALLOC_ARENA_MALLOC_CHECKED(
                ZL_LocalParams, lparamsCopy, 1, selCtx->wkspArena);
        *lparamsCopy = *lparams;
        ZL_RET_R_IF_ERR(LP_transferLocalParams(selCtx->wkspArena, lparamsCopy));
        SelectorSuccessorParams* p = selCtx->successorLParams;
        p->params                  = lparamsCopy;
    }
    return ZL_returnSuccess();
}

int ZL_Selector_getCParam(const ZL_Selector* selCtx, ZL_CParam gparam)
{
    ZL_ASSERT_NN(selCtx);
    return CCTX_getAppliedGParam(selCtx->cctx, gparam);
}

ZL_LocalIntParams ZL_Selector_getLocalIntParams(const ZL_Selector* selCtx)
{
    ZL_ASSERT_NN(selCtx);
    return LP_getLocalIntParams(selCtx->lparams);
}

ZL_IntParam ZL_Selector_getLocalIntParam(
        const ZL_Selector* selCtx,
        int intParamId)
{
    ZL_ASSERT_NN(selCtx);
    return LP_getLocalIntParam(selCtx->lparams, intParamId);
}

ZL_RefParam ZL_Selector_getLocalParam(const ZL_Selector* selCtx, int refParamId)
{
    ZL_ASSERT_NN(selCtx);
    return LP_getLocalRefParam(selCtx->lparams, refParamId);
}

ZL_CopyParam ZL_Selector_getLocalCopyParam(
        const ZL_Selector* selCtx,
        int copyParamId)
{
    ZL_ASSERT_NN(selCtx);
    ZL_LocalCopyParams const lgp = selCtx->lparams->copyParams;
    for (size_t n = 0; n < lgp.nbCopyParams; n++) {
        if (lgp.copyParams[n].paramId == copyParamId) {
            return lgp.copyParams[n];
        }
    }
    return (ZL_CopyParam){ .paramId   = ZL_LP_INVALID_PARAMID,
                           .paramPtr  = NULL,
                           .paramSize = 0 };
}

void* ZL_Selector_getScratchSpace(const ZL_Selector* selCtx, size_t size)
{
    return ALLOC_Arena_malloc(selCtx->wkspArena, size);
}

const void* ZL_Selector_getOpaquePtr(const ZL_Selector* selector)
{
    return selector->opaque;
}
