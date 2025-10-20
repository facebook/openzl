// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/dyngraph_interface.h"
#include "openzl/common/allocation.h"    // ALLOC_*
#include "openzl/common/introspection.h" // WAYPOINT, ZL_CompressIntrospectionHooks
#include "openzl/common/logging.h"
#include "openzl/common/vector.h"
#include "openzl/compress/cctx.h" // CCTX_getAppliedGParam
#include "openzl/compress/cgraph.h"
#include "openzl/compress/localparams.h" // LP_*
#include "openzl/compress/rtgraphs.h"
#include "openzl/shared/mem.h" // ZL_memcpy
#include "openzl/zl_errors.h"  // ZL_returnSuccess
#include "openzl/zl_graph_api.h"
#include "openzl/zl_localParams.h"
#include "openzl/zl_opaque_types.h"

/* ===   state management   === */

void GCTX_destroy(ZL_Graph* gctx)
{
    ALLOC_Arena_freeAll(gctx->graphArena);
    VECTOR_DESTROY(gctx->streamCtxs);
    VECTOR_DESTROY(gctx->dstGraphDescs);
    VECTOR_DESTROY(gctx->rtsids);
    // Note: Nodes defined at runtime still need to be present at end of
    // compression to properly collect their connection map and transform ids
    return;
}

ZL_Report SCTX_initInput(ZL_Edge* outEdge, ZL_Graph* gctx, RTStreamID irtsid)
{
    ZL_DLOG(SEQ, "SCTX_initInput on RTStreamID=%u", irtsid.rtsid);
    ZL_ASSERT_NN(gctx);
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx->cctx);
    ZL_ERR_IF_NOT(
            VECTOR_PUSHBACK(
                    gctx->streamCtxs, (DG_StreamCtx){ .rtsid = irtsid }),
            allocation);
    ZL_ASSERT_GE(VECTOR_SIZE(gctx->streamCtxs), 1);
    ZL_IDType n = (ZL_IDType)VECTOR_SIZE(gctx->streamCtxs) - 1;
    ZL_ASSERT_NN(sctx);
    outEdge[0] = (ZL_Edge){
        .gctx     = gctx,
        .scHandle = n,
    };
    return ZL_returnSuccess();
}

void SCTX_destroy(ZL_Edge* sctx)
{
    (void)sctx;
    return;
}

/* ===   actions   === */

ZL_Report
GCTX_runMultiInputGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs)
{
    ZL_ASSERT_NN(gctx);
    ZL_FunctionGraphFn const graphf = gctx->dgd->graph_f;
    ZL_ASSERT_NN(graphf);
    return graphf(gctx, inputs, nbInputs);
}

/* accessors */

ZL_GraphIDList ZL_Graph_getCustomGraphs(const ZL_Graph* gctx)
{
    ZL_ASSERT_NN(gctx);
    ZL_ASSERT_NN(gctx->dgd);
    return (ZL_GraphIDList){
        .graphids   = gctx->dgd->customGraphs,
        .nbGraphIDs = gctx->dgd->nbCustomGraphs,
    };
}

ZL_NodeIDList ZL_Graph_getCustomNodes(const ZL_Graph* gctx)
{
    ZL_ASSERT_NN(gctx);
    ZL_ASSERT_NN(gctx->dgd);
    return (ZL_NodeIDList){
        .nodeids   = gctx->dgd->customNodes,
        .nbNodeIDs = gctx->dgd->nbCustomNodes,
    };
}

int ZL_Graph_getCParam(const ZL_Graph* gctx, ZL_CParam gparam)
{
    ZL_ASSERT_NN(gctx);
    return CCTX_getAppliedGParam(gctx->cctx, gparam);
}

ZL_IntParam ZL_Graph_getLocalIntParam(const ZL_Graph* gctx, int intParamId)
{
    ZL_ASSERT_NN(gctx);
    return LP_getLocalIntParam(&gctx->dgd->localParams, intParamId);
}

ZL_RefParam ZL_Graph_getLocalRefParam(const ZL_Graph* gctx, int refParamId)
{
    ZL_ASSERT_NN(gctx);
    return LP_getLocalRefParam(&gctx->dgd->localParams, refParamId);
}

const ZL_LocalParams* GCTX_getAllLocalParams(const ZL_Graph* gctx)
{
    ZL_ASSERT_NN(gctx);
    ZL_ASSERT_NN(gctx->dgd);
    return &gctx->dgd->localParams;
}

const void* GCTX_getPrivateParam(ZL_Graph* gctx)
{
    return gctx->privateParam;
}

bool ZL_Graph_isNodeSupported(const ZL_Graph* gctx, ZL_NodeID nodeid)
{
    ZL_ASSERT_NN(gctx);
    return CCTX_isNodeSupported(gctx->cctx, nodeid);
}

/* actions */

void* ZL_Graph_getScratchSpace(ZL_Graph* gctx, size_t size)
{
    WAYPOINT(on_ZL_Graph_getScratchSpace, gctx, size);
    return ALLOC_Arena_malloc(gctx->graphArena, size);
}

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runMultiInputNode(
        ZL_Edge* inputCtxs[],
        size_t nbInputs,
        ZL_NodeID nodeid)
{
    return ZL_Edge_runMultiInputNode_withParams(
            inputCtxs, nbInputs, nodeid, NULL);
}

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runMultiInputNode_withParams(
        ZL_Edge* inputCtxs[],
        size_t nbInputs,
        ZL_NodeID nodeid,
        const ZL_LocalParams* localParams)
{
    ZL_DLOG(SEQ,
            "ZL_Edge_runMultiInputNode (nodeid=%u, nbInputs=%zu)",
            nodeid.nid,
            nbInputs);

    ZL_ASSERT_GE(nbInputs, 1);
    ZL_ASSERT_NN(inputCtxs);
    ZL_ASSERT_NN(inputCtxs[0]);
    ZL_Graph* const gctx = inputCtxs[0]->gctx;
    ZL_ASSERT_NN(gctx);
    Arena* const allocator = gctx->graphArena;
    ZL_ASSERT_NN(allocator);

#define ALLOC_ARRAY(type, name, nb) \
    ALLOC_ARENA_MALLOC_CHECKED_T(type, name, nb, allocator, ZL_EdgeList)

    // Check input doesn't already have a set successor
    ALLOC_ARRAY(DG_StreamCtx*, inDGSCtxs, nbInputs);
    ALLOC_ARRAY(const ZL_Data*, inStreams, nbInputs);
    ALLOC_ARRAY(RTStreamID, rtsids, nbInputs);

    for (size_t n = 0; n < nbInputs; n++) {
        ZL_ASSERT_NN(inputCtxs[n]);
        inDGSCtxs[n] = &VECTOR_AT(gctx->streamCtxs, inputCtxs[n]->scHandle);
        ZL_RET_T_IF_NE(
                ZL_EdgeList,
                successor_alreadySet,
                inDGSCtxs[n]->dest_set,
                sds_unassigned);
        inStreams[n] = ZL_codemodInputAsData(ZL_Edge_getData(inputCtxs[n]));
        rtsids[n]    = inDGSCtxs[n]->rtsid;
    }

    // run Node
    ZL_CCtx* const cctx = gctx->cctx;
    RTNodeID rtnid;
    ZL_Report const trStatus = CCTX_runNodeID_wParams(
            cctx, &rtnid, inStreams, rtsids, nbInputs, nodeid, localParams);

    // check node execution status
    ZL_RET_T_IF_ERR(ZL_EdgeList, trStatus);
    size_t const nbOuts = ZL_validResult(trStatus);

    // Set Input Streams as processed
    for (size_t n = 0; n < nbInputs; n++) {
        inDGSCtxs[n]->dest_set = sds_processed;
    }

    // collect outputs
    ALLOC_ARRAY(ZL_Edge*, outStreamCtxs, nbOuts);
    ALLOC_ARRAY(ZL_Edge, outSCtxArray, nbOuts);
    size_t const oldNbStreams = VECTOR_SIZE(gctx->streamCtxs);
    size_t const newNbStreams = oldNbStreams + nbOuts;
    size_t const reservedSize =
            VECTOR_RESIZE_UNINITIALIZED(gctx->streamCtxs, newNbStreams);
    ZL_RET_T_IF_GT(ZL_EdgeList, allocation, newNbStreams, reservedSize);

    ZL_DLOG(SEQ, "node %u created %zu outputs", nodeid.nid, nbOuts);
    for (size_t n = 0; n < nbOuts; n++) {
        const RTStreamID rtosid =
                RTGM_getOutStreamID(gctx->rtgraph, rtnid, (int)n);
        ZL_DLOG(SEQ,
                "output %zu (RTStreamID=%u) pushed as handle %zu",
                n,
                rtosid.rtsid,
                oldNbStreams + n);
        VECTOR_AT(gctx->streamCtxs, oldNbStreams + n) =
                (DG_StreamCtx){ .rtsid = rtosid };
        outSCtxArray[n] =
                (ZL_Edge){ .gctx     = gctx,
                           .scHandle = (ZL_IDType)(oldNbStreams + n) };
        outStreamCtxs[n] = &outSCtxArray[n];
    }

    // return result
    ZL_EdgeList const result = {
        .nbEdges = nbOuts,
        .edges   = outStreamCtxs,
    };
    return ZL_RESULT_WRAP_VALUE(ZL_EdgeList, result);
#undef ALLOC_ARRAY
}

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runNode(ZL_Edge* inputCtx, ZL_NodeID nodeid)
{
    ZL_ASSERT_NN(inputCtx);
    ZL_DLOG(SEQ,
            "ZL_Edge_runNode (nodeid=%u, inputHandle=%u)",
            nodeid.nid,
            inputCtx->scHandle);

    return ZL_Edge_runMultiInputNode(&inputCtx, 1, nodeid);
}

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runNode_withParams(
        ZL_Edge* input,
        ZL_NodeID nid,
        const ZL_LocalParams* localParams)
{
    ZL_DLOG(SEQ, "ZL_Edge_runNode_withParams (node id=%u)", nid.nid);
    return ZL_Edge_runMultiInputNode_withParams(&input, 1, nid, localParams);
}

ZL_CONST_FN
ZL_OperationContext* ZL_Graph_getOperationContext(ZL_Graph* gctx)
{
    if (gctx == NULL) {
        return NULL;
    }
    return ZL_CCtx_getOperationContext(gctx->cctx);
}

ZL_CONST_FN
ZL_OperationContext* ZL_Edge_getOperationContext(ZL_Edge* sctx)
{
    if (sctx == NULL) {
        return NULL;
    }
    return ZL_Graph_getOperationContext(sctx->gctx);
}

static ZL_Report ZL_transferRuntimeGraphParams_stage2(
        Arena* arena,
        ZL_RuntimeGraphParameters* rgp)
{
    ZL_ASSERT_NN(arena);
    ZL_ASSERT_NN(rgp);
    if (rgp->localParams) {
        ALLOC_ARENA_MALLOC_CHECKED(ZL_LocalParams, lparamsCopy, 1, arena);
        *lparamsCopy = *rgp->localParams;
        ZL_RET_R_IF_ERR(LP_transferLocalParams(arena, lparamsCopy));
        rgp->localParams = lparamsCopy;
    }
    if (rgp->nbCustomGraphs > 0) {
        ZL_ASSERT_NN(rgp->customGraphs);
        ALLOC_ARENA_MALLOC_CHECKED(
                ZL_GraphID, optGidCopy, rgp->nbCustomGraphs, arena);
        ZL_memcpy(
                optGidCopy,
                rgp->customGraphs,
                sizeof(ZL_GraphID) * rgp->nbCustomGraphs);
        rgp->customGraphs = optGidCopy;
    }
    if (rgp->nbCustomNodes > 0) {
        ZL_ASSERT_NN(rgp->customNodes);
        ALLOC_ARENA_MALLOC_CHECKED(
                ZL_NodeID, optNidCopy, rgp->nbCustomNodes, arena);
        ZL_memcpy(
                optNidCopy,
                rgp->customNodes,
                sizeof(ZL_NodeID) * rgp->nbCustomNodes);
        rgp->customNodes = optNidCopy;
    }
    return ZL_returnSuccess();
}

ZL_RuntimeGraphParameters* ZL_transferRuntimeGraphParams(
        Arena* arena,
        const ZL_RuntimeGraphParameters* rgp)
{
    if (rgp == NULL)
        return NULL;
    ZL_ASSERT_NN(arena);
    ZL_RuntimeGraphParameters* const rgpCopy =
            ALLOC_Arena_malloc(arena, sizeof(*rgpCopy));
    if (rgpCopy == NULL) {
        return NULL;
    }
    *rgpCopy          = *rgp;
    ZL_Report const r = ZL_transferRuntimeGraphParams_stage2(arena, rgpCopy);
    if (ZL_isError(r))
        return NULL;
    return rgpCopy;
}

ZL_Report ZL_Edge_setDestination(ZL_Edge* input, ZL_GraphID gid)
{
    ZL_DLOG(SEQ, "ZL_Edge_setDestination(for streamID=%u)", input->scHandle);
    return ZL_Edge_setParameterizedDestination(&input, 1, gid, NULL);
}

ZL_Report ZL_Edge_setParameterizedDestination(
        ZL_Edge* inputs[],
        size_t nbInputs,
        ZL_GraphID gid,
        const ZL_RuntimeGraphParameters* rGraphParams)
{
    ZL_ASSERT_NN(inputs);
    ZL_ASSERT_NN(inputs[0]);
    ZL_Graph* const gctx = inputs[0]->gctx;
    ZL_ASSERT_NN(gctx);
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx->cctx);
    ZL_DLOG(SEQ, "ZL_Edge_setDestination(%zu inputs => gid=%u)", nbInputs, gid);

    // === Phase 1: Basic Input Sanitization ===
    ZL_ERR_IF_NULL(
            nbInputs,
            successor_invalidNumInputs,
            "A Graph Successor must have at least 1 Input.");

    // === Phase 2: Input Descriptor Lookup ===
    typedef struct {
        const char* name;
        size_t numInputs;
        bool lastInputIsVariable;
    } InputGraphDesc;

    const ZL_Compressor* const compressor = CCTX_getCGraph(gctx->cctx);
    ZL_DLOG(SEQ,
            "CGRAPH_graphType(compressor, gid) = %i",
            CGRAPH_graphType(compressor, gid));
    InputGraphDesc inputGD;
    if (CGRAPH_graphType(compressor, gid) == gt_segmenter) {
        const ZL_SegmenterDesc* const segd =
                CGRAPH_getSegmenterDesc(compressor, gid);
        ZL_ASSERT_NN(segd);
        inputGD = (InputGraphDesc){
            .name                = segd->name,
            .numInputs           = segd->numInputs,
            .lastInputIsVariable = segd->lastInputIsVariable,
        };
    } else {
        const ZL_FunctionGraphDesc* const fgd =
                CGRAPH_getMultiInputGraphDesc(compressor, gid);
        ZL_ERR_IF_NULL(fgd, graph_invalid);
        inputGD = (InputGraphDesc){
            .name                = fgd->name,
            .numInputs           = fgd->nbInputs,
            .lastInputIsVariable = fgd->lastInputIsVariable,
        };
    };

    // === Phase 3: Validate number of inputs ===
    if (inputGD.lastInputIsVariable) {
        // Variable Input: last Input can be present [0-N] times
        // Must provide at least (required_inputs - 1) since last is optional
        ZL_ASSERT_GE(inputGD.numInputs, 1);
        ZL_ERR_IF_LT(
                nbInputs, inputGD.numInputs - 1, successor_invalidNumInputs);
    } else {
        // Only Singular Inputs: count must be exact
        ZL_ERR_IF_NE(
                nbInputs,
                inputGD.numInputs,
                successor_invalidNumInputs,
                "Graph '%s' should have received %zu Inputs (!= %zu)",
                STR_REPLACE_NULL(inputGD.name),
                inputGD.numInputs,
                nbInputs);
    }

    // === Phase 4: Process Each Input Edge ===
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_ASSERT_NN(inputs[n]);
        DG_StreamCtx* const sctx =
                &VECTOR_AT(gctx->streamCtxs, inputs[n]->scHandle);

        // Check input is still available
        if (sctx->dest_set != sds_unassigned) {
            gctx->status = ZL_REPORT_ERROR(successor_alreadySet);
            ZL_ERR(successor_alreadySet);
        }

        // Mark Stream as assigned
        sctx->dest_set = (n == 0) ? sds_destSet_trigger : sds_destSet_follow;
        ZL_ERR_IF_NOT(VECTOR_PUSHBACK(gctx->rtsids, sctx->rtsid), allocation);
        sctx->successionPos = VECTOR_SIZE(gctx->dstGraphDescs);
    }
    ZL_ASSERT_GE(VECTOR_SIZE(gctx->rtsids), nbInputs);

    // === Phase 5: Transfer Runtime Parameters to Session Memory ===
    rGraphParams =
            ZL_transferRuntimeGraphParams(gctx->chunkArena, rGraphParams);

    // === Phase 6: Create and Store Destination Graph Descriptor ===
    // This descriptor is stored for deferred execution - not used immediately
    // 1. When the current graph completes execution (in CCTX_runGraph_internal)
    // 2. GCTX_getSuccessors() will iterate through stored descriptors
    // 3. For each "trigger" stream, it extracts the stored descriptor
    // 4. The SuccessorInfo array is passed to CCTX_runSuccessors()
    DestGraphDesc const sd = {
        gid, rGraphParams, nbInputs, VECTOR_SIZE(gctx->rtsids) - nbInputs
    };
    ZL_ERR_IF_NOT(VECTOR_PUSHBACK(gctx->dstGraphDescs, sd), allocation);

    // note: Input Type compatibility is checked on starting the Successor Graph
    return ZL_returnSuccess();
}

ZL_IDType StreamCtx_getOutcomeID(const ZL_Edge* sctx)
{
    ZL_ASSERT_NN(sctx);
    ZL_Graph* const gctx = sctx->gctx;
    RTStreamID rtsid     = VECTOR_AT(gctx->streamCtxs, sctx->scHandle).rtsid;
    return RTGM_getOutcomeID_fromRtstream(gctx->rtgraph, rtsid);
}

const void* ZL_Graph_getOpaquePtr(const ZL_Graph* gctx)
{
    return gctx->dgd->opaque.ptr;
}

ZL_RESULT_OF(ZL_GraphPerformance)
ZL_Graph_tryMultiInputGraph(
        const ZL_Graph* gctx,
        const ZL_Input* inputs[],
        size_t numInputs,
        ZL_GraphID graphID,
        const ZL_RuntimeGraphParameters* params)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphPerformance, NULL);
    ZL_ERR_IF_EQ(numInputs, 0, graph_invalidNumInputs);

    return CCTX_tryGraph(
            gctx->cctx, inputs, numInputs, gctx->graphArena, graphID, params);
}

ZL_RESULT_OF(ZL_GraphPerformance)
ZL_Graph_tryGraph(
        const ZL_Graph* gctx,
        const ZL_Input* input,
        ZL_GraphID graphID,
        const ZL_RuntimeGraphParameters* params)
{
    return ZL_Graph_tryMultiInputGraph(gctx, &input, 1, graphID, params);
}
