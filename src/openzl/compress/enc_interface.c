// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/enc_interface.h" // ZL_Encoder definition
#include "openzl/common/allocation.h"      // ZL_malloc, ZL_free
#include "openzl/common/assertion.h"       // ZL_ASSERT, ZL_REQUIRE
#include "openzl/common/errors_internal.h" // ZL_TRY_LET
#include "openzl/common/introspection.h" // WAYPOINT, ZL_CompressIntrospectionHooks
#include "openzl/common/limits.h"
#include "openzl/common/operation_context.h"
#include "openzl/common/scope_context.h"
#include "openzl/compress/cctx.h" // CCTX_*
#include "openzl/compress/cnode.h"
#include "openzl/compress/localparams.h"
#include "openzl/compress/trStates.h" // TRS_getState
#include "openzl/zl_common_types.h"   // ZL_TernaryParam_disable
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"

ZL_Report ENC_initEICtx(
        ZL_Encoder* eictx,
        ZL_CCtx* cctx,
        Arena* wkspArena,
        const RTNodeID* rtnodeid,
        const CNode* cnode,
        const ZL_LocalParams* lparams,
        CachedStates* cachedStates)
{
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(wkspArena);
    ZL_ASSERT_NN(rtnodeid);
    *eictx = (ZL_Encoder){ .cctx         = cctx,
                           .rtnodeid     = *rtnodeid,
                           .cnode        = cnode,
                           .wkspArena    = wkspArena,
                           .lparams      = lparams,
                           .cachedStates = cachedStates };
    return ZL_returnSuccess();
}

void ENC_destroyEICtx(ZL_Encoder* ei)
{
    ZL_ASSERT_NN(ei);
    ALLOC_Arena_freeAll(ei->wkspArena);
}

int ZL_Encoder_getCParam(const ZL_Encoder* eic, ZL_CParam gparam)
{
    ZL_ASSERT_NN(eic);
    return CCTX_getAppliedGParam(eic->cctx, gparam);
}

ZL_LocalIntParams ZL_Encoder_getLocalIntParams(const ZL_Encoder* eic)
{
    ZL_ASSERT_NN(eic);
    return LP_getLocalIntParams(eic->lparams);
}

ZL_IntParam ZL_Encoder_getLocalIntParam(const ZL_Encoder* eic, int intParamId)
{
    ZL_ASSERT_NN(eic);
    return LP_getLocalIntParam(eic->lparams, intParamId);
}

ZL_RefParam ZL_Encoder_getLocalParam(const ZL_Encoder* eic, int refParamId)
{
    ZL_ASSERT_NN(eic);
    return LP_getLocalRefParam(eic->lparams, refParamId);
}

ZL_CopyParam ZL_Encoder_getLocalCopyParam(
        const ZL_Encoder* eic,
        int copyParamId)
{
    ZL_ASSERT_NN(eic);
    ZL_LocalCopyParams const lgp = eic->lparams->copyParams;
    for (size_t n = 0; n < lgp.nbCopyParams; n++) {
        if (lgp.copyParams[n].paramId == copyParamId) {
            return lgp.copyParams[n];
        }
    }
    return (ZL_CopyParam){ .paramId   = ZL_LP_INVALID_PARAMID,
                           .paramPtr  = NULL,
                           .paramSize = 0 };
}

const ZL_LocalParams* ZL_Encoder_getLocalParams(const ZL_Encoder* eic)
{
    ZL_ASSERT_NN(eic);
    return eic->lparams;
}

const void* ENC_getPrivateParam(const ZL_Encoder* eictx)
{
    return eictx->privateParam;
}

// ZL_Encoder_sendCodecHeader():
// Note : this operation can fail,
// in which case, the operation failure is marked,
// and the orchestrator later get to detect the issue and react adequately.
void ZL_Encoder_sendCodecHeader(
        ZL_Encoder* eictx,
        const void* trh,
        size_t trhSize)
{
    ZL_DLOG(SEQ, "ZL_Encoder_sendCodecHeader (%zu bytes)", trhSize);
    CWAYPOINT(on_ZL_Encoder_sendCodecHeader, eictx, trh, trhSize);
    ZL_ASSERT_NN(eictx);
    if (trhSize)
        ZL_ASSERT_NN(trh);
    if (eictx->hasSentTrHeader) {
        eictx->sendTransformHeaderError = ZL_REPORT_ERROR(
                transform_executionFailure, "Transform header sent twice");
        return;
    }
    eictx->hasSentTrHeader = 1;
    ZL_Report const r      = CCTX_sendTrHeader(
            eictx->cctx, eictx->rtnodeid, (ZL_RBuffer){ trh, trhSize });
    if (ZL_isError(r))
        eictx->sendTransformHeaderError = r;
}

ZL_Report ZL_Encoder_createAllOutBuffers(
        ZL_Encoder* eic,
        void* buffStarts[],
        const size_t buffSizes[],
        size_t nbBuffs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eic);

    /* General idea :
     *
     * 1) Access the definition of the node in the immutable cgraph,
     *    which is tracked from the RT_node within the RT_graph,
     *    itself tracked within the Encoder Interface Context (EICtx),
     *    in order to access the definition(s) of possible output stream Types.
     * 2) Stream type must be "ZL_Type_serial" when invoking this function.
     * 3) What matters is to know the nb of output streams declared
     * 4) Ensure that this nb matches @nbBuffs
     * 5) Loop over @buffSizes[], generate a buffer for each one.
     *    Return the pointers in @buffStarts.
     * 6) return success
     *    or return early if there was an issue (such as failed allocation).
     */

    /* TODO(@cyan) :
     * Retrieve the nb of output streams
     * as defined at transform's registration time,
     * then compare it to `nbBuffs`, ensure it's equal,
     * consider how to bubble up an error when it's not.
     **/
    ZL_ASSERT_NN(eic);

    // Triggering that assert means that
    // the user has been invoking this function twice
    // or has started creating some streams with ZL_Encoder_createTypedStream()
    // and then called ZL_Encoder_createAllOutBuffers() afterwards.
    // Both of these cases are in direct violation of the API contract.
    // Hence it's technically UB, though this is less stupid than previous case.
    ZL_ASSERT_EQ(
            RTGM_getNbOutStreams(CCTX_getRTGraph(eic->cctx), eic->rtnodeid),
            0,
            "Method ZL_Encoder_createAllOutBuffers() "
            "can only be invoked once ");

    for (int n = 0; n < (int)nbBuffs; n++) {
        ZL_Output* const data =
                ZL_Encoder_createTypedStream(eic, n, buffSizes[n], 1);
        ZL_ERR_IF_NULL(data, allocation);
        buffStarts[n] = ZL_Output_ptr(data);
        if (buffSizes[n] > 0 && buffStarts[n] == NULL)
            ZL_ERR(allocation);
    }
    return ZL_returnSuccess();
}

ZL_Output* ZL_Encoder_createTypedStream(
        ZL_Encoder* eic,
        int outStreamIndex,
        size_t eltsCapacity,
        size_t eltWidth)
{
    ZL_ASSERT_NN(eic);
    ZL_Data* ret = CCTX_getNewStream(
            eic->cctx, eic->rtnodeid, outStreamIndex, eltWidth, eltsCapacity);
    CWAYPOINT(
            on_ZL_Encoder_createTypedStream,
            eic,
            outStreamIndex,
            eltsCapacity,
            eltWidth,
            ZL_codemodDataAsOutput(ret));
    return ZL_codemodDataAsOutput(ret);
}

ZL_Output* ZL_Encoder_createStringStream(
        ZL_Encoder* eic,
        int outcomeIndex,
        size_t nbStringsMax,
        size_t sumStringLenMax)
{
    ZL_Output* const stringS =
            ZL_Encoder_createTypedStream(eic, outcomeIndex, sumStringLenMax, 1);
    if (stringS == NULL)
        return NULL;
    if (ZL_Output_type(stringS) != ZL_Type_string)
        return NULL;
    uint32_t* const stringLenArr =
            ZL_Output_reserveStringLens(stringS, nbStringsMax);
    if (stringLenArr == NULL)
        return NULL;
    return stringS;
}

// -------------------------------------------------
// Non-public methods
// -------------------------------------------------

ZL_Output* ENC_refTypedStream(
        ZL_Encoder* eictx,
        int outcomeIndex,
        size_t eltWidth,
        size_t nbElts,
        ZL_Input const* ref,
        size_t offsetBytes)
{
    ZL_ASSERT_NN(eictx);
    return ZL_codemodDataAsOutput(CCTX_refContentIntoNewStream(
            eictx->cctx,
            eictx->rtnodeid,
            outcomeIndex,
            eltWidth,
            nbElts,
            ZL_codemodInputAsData(ref),
            offsetBytes));
}

static ZL_Report ENC_runTransform_internal(
        ZL_Encoder* eictx,
        ZL_NodeID nodeid,
        const InternalTransform_Desc* trDesc,
        const ZL_Data* inStreams[],
        size_t nbInStreams)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_DLOG(BLOCK,
            "ENC_runTransform_internal (%s, nodeid=%zu, nbInputs=%zu)",
            CT_getTrName(trDesc),
            nodeid.nid,
            nbInStreams);
    ZL_SCOPE_GRAPH_CONTEXT(
            eictx,
            { .transformID = trDesc->publicDesc.gd.CTid,
              .name        = trDesc->publicDesc.name });

    eictx->privateParam             = trDesc->privateParam;
    eictx->opaquePtr                = trDesc->publicDesc.opaque.ptr;
    eictx->sendTransformHeaderError = ZL_returnSuccess();

    // Run transform
    ZL_ASSERT_NN(trDesc->publicDesc.transform_f);
    IF_CWAYPOINT_ENABLED(on_codecEncode_start, eictx)
    {
        CWAYPOINT(
                on_codecEncode_start,
                eictx,
                CCTX_getCGraph(eictx->cctx),
                nodeid,
                ZL_codemodDatasAsInputs(inStreams),
                nbInStreams);
    }
    ZL_Report codecExecResult = (trDesc->publicDesc.transform_f(
            eictx, ZL_codemodDatasAsInputs(inStreams), nbInStreams));
    if (ZL_isError(codecExecResult)) {
        CWAYPOINT(on_codecEncode_end, eictx, NULL, 0, codecExecResult);
        ZL_RET_R_IF_ERR_COERCE(
                codecExecResult, "transform %s failed", CT_getTrName(trDesc));
    }
    const RTGraph* rtgm       = CCTX_getRTGraph(eictx->cctx);
    const size_t nbOutStreams = RTGM_getNbOutStreams(rtgm, eictx->rtnodeid);
    IF_CWAYPOINT_ENABLED(on_codecEncode_end, eictx)
    {
        DECLARE_VECTOR_CONST_POINTERS_TYPE(ZL_Data);
        VECTOR_CONST_POINTERS(ZL_Data) odata;
        VECTOR_INIT(odata, nbOutStreams);
        for (size_t i = 0; i < nbOutStreams; ++i) {
            RTStreamID rtsid =
                    RTGM_getOutStreamID(rtgm, eictx->rtnodeid, (int)i);
            const ZL_Data* d     = RTGM_getRStream(rtgm, rtsid);
            bool pushbackSuccess = VECTOR_PUSHBACK(odata, d);
            ZL_ERR_IF_NOT(
                    pushbackSuccess,
                    allocation,
                    "Unable to append to the waypoint odata vector");
        }
        CWAYPOINT(
                on_codecEncode_end,
                eictx,
                ZL_codemodConstDatasAsOutputs(VECTOR_DATA(odata)),
                VECTOR_SIZE(odata),
                ZL_returnSuccess());
        VECTOR_DESTROY(odata);
    }

    // Check that we didn't encounter an error sending the transform header.
    ZL_ERR_IF_ERR(eictx->sendTransformHeaderError);

    // Check that the transform has generated
    // at least as many output streams as compulsory singleton outputs.
    // Note : the check could be more thorough, for example
    //        it could verify that all compulsory outputs have been created.
    //        This can't be done with a simple counter though,
    //        and would require contribution from the RTGraph Manager.
    size_t const nbOut1 = trDesc->publicDesc.gd.nbSOs;
    ZL_ERR_IF_LT(nbOutStreams, nbOut1, transform_executionFailure);

    unsigned const formatVersion =
            (unsigned)ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion);
    if (formatVersion < 9) {
        // Format versions less than 9 don't support 0 output streams.
        ZL_ERR_IF_EQ(
                nbOutStreams,
                0,
                formatVersion_unsupported,
                "Not supported until format version 9");
    }

    ZL_ERR_IF_GT(
            nbOutStreams,
            ZL_transformOutStreamsLimit(formatVersion),
            formatVersion_unsupported);

    return ZL_returnValue(nbOutStreams);
}

ZL_Report ENC_runTransform(
        const InternalTransform_Desc* trDesc,
        const ZL_Data* inputs[],
        size_t nbInputs,
        ZL_NodeID nodeid,
        RTNodeID rtnodeid,
        const CNode* cnode,
        const ZL_LocalParams* lparams,
        ZL_CCtx* cctx,
        Arena* wkspArena,
        CachedStates* trstates)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(cctx);
    ZL_ASSERT_NN(trDesc);
    ZL_DLOG(BLOCK,
            "ENC_runTransform on Transform '%s' (%u) (lparams=%p)",
            CNODE_getName(cnode),
            trDesc->publicDesc.gd.CTid,
            lparams);
    if (lparams == NULL)
        lparams = CNODE_getLocalParams(cnode);
    ZL_Encoder eiState;
    ZL_ERR_IF_ERR(ENC_initEICtx(
            &eiState, cctx, wkspArena, &rtnodeid, cnode, lparams, trstates));
    ZL_Report const transformRes = ENC_runTransform_internal(
            &eiState, nodeid, trDesc, inputs, nbInputs);
    ENC_destroyEICtx(&eiState);
    return transformRes;
}

void* ZL_Encoder_getScratchSpace(ZL_Encoder* ei, size_t size)
{
    CWAYPOINT(on_ZL_Encoder_getScratchSpace, ei, size);
    return ALLOC_Arena_malloc(ei->wkspArena, size);
}

ZL_CONST_FN
ZL_OperationContext* ZL_Encoder_getOperationContext(ZL_Encoder* ei)
{
    if (ei == NULL) {
        return NULL;
    }
    return ZL_CCtx_getOperationContext(ei->cctx);
}

void* ZL_Encoder_getState(ZL_Encoder* ei)
{
    ZL_ASSERT_NN(ei);
    return TRS_getCodecState(ei->cachedStates, ei->cnode);
}

const void* ZL_Encoder_getOpaquePtr(const ZL_Encoder* eictx)
{
    ZL_ASSERT_NN(eictx);
    return eictx->opaquePtr;
}
