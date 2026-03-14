// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/decompress/dictx.h"
#include "openzl/common/allocation.h"
#include "openzl/common/logging.h"
#include "openzl/common/operation_context.h" // ZL_OperationContext
#include "openzl/decompress/dctx2.h"         // DCTX_* declarations
#include "openzl/zl_data.h"

ZL_Decoder* DI_createDICtx(ZL_DCtx* dctx)
{
    ZL_Decoder* const d = ZL_calloc(sizeof(ZL_Decoder));
    if (d == NULL)
        return NULL;
    d->dctx = dctx;
    return d;
}

void DI_freeDICtx(ZL_Decoder* dictx)
{
    ZL_free(dictx);
}

ZL_Output* ZL_Decoder_createTypedStream(
        ZL_Decoder* dictx,
        int index,
        size_t eltsCapacity,
        size_t eltWidth)
{
    size_t dstCapacity;
    bool const overflow =
            ZL_overflowMulST(eltsCapacity, eltWidth, &dstCapacity);
    if (overflow) {
        ZL_DLOG(ERROR,
                "ZL_Decoder_createTypedStream: size request overflow (%zu x %zu)",
                eltsCapacity,
                eltWidth);
        return NULL;
    }
    if (eltWidth == 0) {
        ZL_DLOG(ERROR, "ZL_Decoder_createTypedStream: eltWidth=0 requested");
        return NULL;
    }
    ZL_DLOG(BLOCK,
            "ZL_Decoder_createTypedStream id:%i<%zu of size %zu bytes (%zu x %zu)",
            index,
            dictx->nbRegens,
            dstCapacity,
            eltsCapacity,
            eltWidth);

    ZL_ASSERT_NN(dictx);
    if (index >= (int)dictx->nbRegens) {
        // invalid index
        ZL_DLOG(ERROR,
                "ZL_Decoder_createTypedStream: regen index %i invalid (>= %zu)",
                index,
                dictx->nbRegens);
        return NULL;
    }
    return ZL_codemodDataAsOutput(DCTX_newStream(
            dictx->dctx,
            dictx->regensID[index],
            DT_getRegenType(dictx->dt, index),
            eltWidth,
            eltsCapacity));
}

ZL_Output* ZL_Decoder_create1OutStream(
        ZL_Decoder* dictx,
        size_t eltsCapacity,
        size_t eltWidth)
{
    return ZL_Decoder_createTypedStream(dictx, 0, eltsCapacity, eltWidth);
}

ZL_Output* ZL_Decoder_createStringStream(
        ZL_Decoder* dictx,
        int index,
        size_t nbStringsMax,
        size_t sumStringLensMax)
{
    ZL_Output* const stringS =
            ZL_Decoder_createTypedStream(dictx, index, sumStringLensMax, 1);
    if (stringS == NULL)
        return NULL;
    if (ZL_Output_type(stringS) != ZL_Type_string)
        return NULL;
    uint32_t* const strLens =
            ZL_Output_reserveStringLens(stringS, nbStringsMax);
    if (strLens == NULL) {
        /* note: stringS is allocated, but we just have a reference,
         *       stringS is owned and its lifetime managed by dictx */
        return NULL;
    }
    return stringS;
}

ZL_Output* ZL_Decoder_create1StringStream(
        ZL_Decoder* dictx,
        size_t nbStringsMax,
        size_t sumStringLensMax)
{
    return ZL_Decoder_createStringStream(
            dictx, 0, nbStringsMax, sumStringLensMax);
}

ZL_RBuffer ZL_Decoder_getCodecHeader(const ZL_Decoder* dictx)
{
    ZL_ASSERT_NN(dictx);
    DWAYPOINT_WITH_OC(
            on_ZL_Decoder_getCodecHeader,
            dictx->dctx,
            dictx,
            dictx->thContent.start,
            dictx->thContent.size);
    return dictx->thContent;
}

ZL_Output* DI_outStream_asReference(
        ZL_Decoder* dictx,
        int index,
        const ZL_Input* ref,
        size_t offsetBytes,
        size_t eltWidth,
        size_t nbElts)
{
    ZL_ASSERT_NN(dictx);
    if (index >= (int)dictx->nbRegens) {
        // invalid index
        return NULL;
    }
    ZL_DLOG(BLOCK,
            "DI_outStream_asReference (local out id=%i => stream id=%u) of size %zu bytes",
            index,
            dictx->regensID[index],
            nbElts * eltWidth);

    return ZL_codemodDataAsOutput(DCTX_newStreamFromStreamRef(
            dictx->dctx,
            dictx->regensID[index],
            DT_getRegenType(dictx->dt, index),
            eltWidth,
            nbElts,
            ZL_codemodInputAsData(ref),
            offsetBytes));
}

ZL_Output* DI_reference1OutStream(
        ZL_Decoder* dictx,
        ZL_Input const* ref,
        size_t offsetBytes,
        size_t eltWidth,
        size_t nbElts)
{
    ZL_DLOG(BLOCK,
            "DI_reference1OutStream of size %zu bytes (%zu x %zu bytes)",
            nbElts * eltWidth,
            nbElts,
            eltWidth);
    ZL_ASSERT_NN(dictx);
    if (dictx->nbRegens != 1) {
        // supports only single-regen transforms
        return NULL;
    }

    return DI_outStream_asReference(
            dictx, 0, ref, offsetBytes, eltWidth, nbElts);
}

unsigned DI_getFrameFormatVersion(const ZL_Decoder* dictx)
{
    return ZL_DCtx_getFrameFormatVersion(dictx->dctx);
}

ZL_CONST_FN
ZL_OperationContext* ZL_Decoder_getOperationContext(ZL_Decoder* dictx)
{
    if (dictx == NULL) {
        return NULL;
    }
    return ZL_DCtx_getOperationContext(dictx->dctx);
}

void const* ZL_Decoder_getOpaquePtr(const ZL_Decoder* dictx)
{
    ZL_ASSERT_NN(dictx);
    return dictx->dt->opaque;
}

void* ZL_Decoder_getState(const ZL_Decoder* dictx)
{
    ZL_ASSERT_NN(dictx);
    if (dictx->statePtr[0] == NULL) {
        // Transform's state must be created
        const ZL_CodecStateManager* const tsm =
                DT_getTransformStateMgr(dictx->dt);
        ZL_ASSERT_NN(tsm);
        if (tsm->stateAlloc == NULL) {
            // should not be possible (wrong definition)
            return NULL;
        }
        ZL_ASSERT_NN(tsm->stateFree);
        dictx->statePtr[0] = tsm->stateAlloc();
    }
    return dictx->statePtr[0];
}

size_t DI_getNbRegens(const ZL_Decoder* dictx)
{
    return dictx->nbRegens;
}

void* ZL_Decoder_getScratchSpace(ZL_Decoder* di, size_t size)
{
    return ALLOC_Arena_malloc(di->workspaceArena, size);
}
