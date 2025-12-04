// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <limits.h> // INT_MAX
#include <string.h> // memcpy

#include "openzl/common/allocation.h"      // Arena, ZL_calloc, ZL_free
#include "openzl/common/assertion.h"       // ZS_ASSERT_*
#include "openzl/common/buffer_internal.h" // ZL_RBuffer_fromVector
#include "openzl/common/errors_internal.h" // ZL_TRY_LET
#include "openzl/common/introspection.h" // WAYPOINT, ZL_CompressIntrospectionHooks
#include "openzl/common/limits.h"
#include "openzl/common/logging.h" // ZL_LOG
#include "openzl/common/operation_context.h"
#include "openzl/common/stream.h"               // STREAM_*
#include "openzl/common/vector.h"               // VECTOR_*
#include "openzl/compress/cctx.h"               // ZS2_CCtx_*
#include "openzl/compress/cgraph.h"             // CGRAPH_*
#include "openzl/compress/cnode.h"              // CNODE_*
#include "openzl/compress/dyngraph_interface.h" // GCtx
#include "openzl/compress/enc_interface.h"      // ENC_*
#include "openzl/compress/gcparams.h"           // GCParams
#include "openzl/compress/implicit_conversion.h" // ICONV_implicitConversionNodeID
#include "openzl/compress/private_nodes.h"       // ZL_GRAPH_SERIAL_STORE
#include "openzl/compress/rtgraphs.h"            // RTGraph, RTStreamID
#include "openzl/compress/segmenter.h"           // SEGM_*
#include "openzl/compress/trStates.h"            // TrStates
#include "openzl/zl_buffer.h"                    // ZL_RBuffer
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_input.h"
#include "openzl/zl_localParams.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_reflection.h"

// --------------------------
// Transform's private header
// --------------------------

typedef struct {
    VECTOR(uint8_t) stagingHeaderStream;
    VECTOR(uint8_t) sentHeaderStream;
} CCTX_TransformHeaders;

static ZL_Report appendToVector(VECTOR(uint8_t) * vector, ZL_RBuffer buffer)
{
    size_t const originalSize   = VECTOR_SIZE(*vector);
    size_t const neededCapacity = originalSize + buffer.size;
    ZL_RET_R_IF_GT(
            allocation,
            neededCapacity,
            ZL_ENCODER_TRANSFORM_HEADER_SIZE_LIMIT,
            "Refusing to allocate more header space");
    ZL_RET_R_IF_LT(
            allocation,
            VECTOR_RESIZE_UNINITIALIZED(*vector, neededCapacity),
            neededCapacity);
    if (buffer.size > 0) {
        memcpy(VECTOR_DATA(*vector) + originalSize, buffer.start, buffer.size);
    }
    return ZL_returnValue(originalSize);
}

/**
 * Stages the transform header @p buffer into @p headers.
 *
 * @returns The offset that @p buffer is written into @p
 * headers->stagingHeaderStream.
 */
static ZL_Report CCTX_TransformHeaders_stage(
        CCTX_TransformHeaders* headers,
        ZL_RBuffer buffer)
{
    return appendToVector(&headers->stagingHeaderStream, buffer);
}

/**
 * Sends the transfrom header @p buffer into @p headers. This buffer must come
 * from `stagingHeaderStream`.
 *
 * @returns The offset that @p buffer is written into @p
 * headers->sentHeaderStream.
 */
static ZL_Report CCTX_TransformHeaders_send(
        CCTX_TransformHeaders* headers,
        ZL_RBuffer buffer)
{
    return appendToVector(&headers->sentHeaderStream, buffer);
}

/**
 * Inits @p headers for a new cctx.
 */
static void CCTX_TransformHeaders_init(CCTX_TransformHeaders* headers)
{
    VECTOR_INIT(
            headers->stagingHeaderStream,
            ZL_ENCODER_TRANSFORM_HEADER_SIZE_LIMIT);
    VECTOR_INIT(
            headers->sentHeaderStream, ZL_ENCODER_TRANSFORM_HEADER_SIZE_LIMIT);
}

/**
 * Resets @p headers for a new compression by clearing the header streams.
 */
static void CCTX_TransformHeaders_reset(CCTX_TransformHeaders* headers)
{
    VECTOR_CLEAR(headers->stagingHeaderStream);
    VECTOR_CLEAR(headers->sentHeaderStream);
}

static void CCTX_TransformHeaders_destroy(CCTX_TransformHeaders* headers)
{
    VECTOR_DESTROY(headers->stagingHeaderStream);
    VECTOR_DESTROY(headers->sentHeaderStream);
}

// --------------------------
// CCtx Lifetime management
// --------------------------

// Note: typedef'd to ZL_CCtx within zs2_compress.h
struct ZL_CCtx_s {
    const ZL_Compressor* cgraph;
    ZL_Compressor* internal_cgraph;
    RTGraph rtgraph;
    CachedStates cachedCodecStates; // @note valid for single-thread only
    GCParams requestedGCParams;     // User selection, at CCtx level
    GCParams appliedGCParams;       // Employed at compression time;
                                    // CCtx > Compressor > default
    /// Comment to be added to the header. Is not added when size is 0.
    ZL_Comment comment;
    CCTX_TransformHeaders trHeaders;
    /* These Arenas presume single-thread execution.
     * For parallel execution, it will have to be replaced by Arena Pools */
    Arena* codecArena;   // Codec lifetime
    Arena* graphArena;   // Graph Lifetime
    Arena* chunkArena;   // Chunk Lifetime
    Arena* sessionArena; // Entire compression lifetime
    const ZL_TypedRef** inputs;
    unsigned nbInputs;
    int segmenterStarted;
    void* dstBuffer;         // where to write chunks
    size_t dstCapacity;      // capacity of dstBuffer
    size_t currentFrameSize; // already written into dstBuffer
    ZL_OperationContext opCtx;
    int inBackupMode; // tracks when graph is in backup mode, to avoid looping
};

static ZL_Report CCTX_init(ZL_CCtx* cctx)
{
    ZL_ASSERT_NN(cctx);
    ZL_RESULT_DECLARE_SCOPE_REPORT(cctx);

    ZL_OC_init(&cctx->opCtx);

    cctx->codecArena   = ALLOC_StackArena_create();
    cctx->graphArena   = ALLOC_StackArena_create();
    cctx->chunkArena   = ALLOC_StackArena_create();
    cctx->sessionArena = ALLOC_StackArena_create();
    ZL_ERR_IF(
            cctx->graphArena == NULL || cctx->codecArena == NULL
                    || cctx->chunkArena == NULL || cctx->sessionArena == NULL,
            allocation);

    ZL_ERR_IF_ERR(RTGM_init(&cctx->rtgraph));
    TRS_init(&cctx->cachedCodecStates);
    CCTX_TransformHeaders_init(&cctx->trHeaders);

    return ZL_returnSuccess();
}

ZL_CCtx* CCTX_create(void)
{
    ZL_CCtx* const cctx = ZL_calloc(sizeof(ZL_CCtx));
    if (cctx == NULL)
        return NULL;
    if (ZL_isError(CCTX_init(cctx))) {
        CCTX_free(cctx);
        return NULL;
    }
    ZL_OC_startOperation(&cctx->opCtx, ZL_Operation_compress);
    return cctx;
}

void CCTX_cleanChunk(ZL_CCtx* cctx)
{
    RTGM_reset(&cctx->rtgraph);
    CCTX_TransformHeaders_reset(&cctx->trHeaders);
    ALLOC_Arena_freeAll(cctx->chunkArena);
}

// clean context, in order to re-use it
void CCTX_clean(ZL_CCtx* cctx)
{
    CCTX_cleanChunk(cctx);
    ALLOC_Arena_freeAll(cctx->sessionArena);
    cctx->comment.size = 0;
    cctx->comment.data = NULL;
    ZL_ASSERT_EQ(ALLOC_Arena_memUsed(cctx->codecArena), 0);
    ZL_ASSERT_EQ(ALLOC_Arena_memUsed(cctx->graphArena), 0);
    ZL_ASSERT_EQ(ALLOC_Arena_memUsed(cctx->chunkArena), 0);
}

void CCTX_free(ZL_CCtx* cctx)
{
    if (cctx == NULL)
        return;
    TRS_destroy(&cctx->cachedCodecStates);
    ZL_Compressor_free(cctx->internal_cgraph);
    RTGM_destroy(&cctx->rtgraph);
    CCTX_TransformHeaders_destroy(&cctx->trHeaders);
    ALLOC_Arena_freeArena(cctx->codecArena);
    ALLOC_Arena_freeArena(cctx->graphArena);
    ALLOC_Arena_freeArena(cctx->chunkArena);
    ALLOC_Arena_freeArena(cctx->sessionArena);
    ZL_OC_destroy(&cctx->opCtx);
    ZL_free(cctx);
}

// --------------------------
// Public Accessors
// --------------------------

ZL_Report ZL_CCtx_setDataArena(ZL_CCtx* cctx, ZL_DataArenaType sat)
{
    ZL_ASSERT_NN(cctx);
    return RTGM_setStreamArenaType(&cctx->rtgraph, sat);
}

ZL_Report ZL_CCtx_attachIntrospectionHooks(
        ZL_CCtx* cctx,
        const ZL_CompressIntrospectionHooks* hooks)
{
    ZL_ASSERT_NN(cctx);
    ZL_RET_R_IF_NULL(allocation, hooks);
    ZL_OperationContext* oc = ZL_CCtx_getOperationContext(cctx);
    ZL_ASSERT_NN(oc);
    oc->introspectionHooks    = *hooks;
    oc->hasIntrospectionHooks = true;
    return ZL_returnSuccess();
}

ZL_Report ZL_CCtx_detachAllIntrospectionHooks(ZL_CCtx* cctx)
{
    ZL_ASSERT_NN(cctx);
    ZL_OperationContext* oc = ZL_CCtx_getOperationContext(cctx);
    ZL_ASSERT_NN(oc);
    ZL_zeroes(&oc->introspectionHooks, sizeof(oc->introspectionHooks));
    oc->hasIntrospectionHooks = false;
    return ZL_returnSuccess();
}

ZL_Report ZL_CCtx_setParameter(ZL_CCtx* cctx, ZL_CParam gcparam, int value)
{
    ZL_ASSERT_NN(cctx);
    return GCParams_setParameter(&cctx->requestedGCParams, gcparam, value);
}

int ZL_CCtx_getParameter(const ZL_CCtx* cctx, ZL_CParam gcparam)
{
    ZL_ASSERT_NN(cctx);
    return GCParams_getParameter(&cctx->requestedGCParams, gcparam);
}

ZL_Report ZL_CCtx_selectStartingGraphID(
        ZL_CCtx* cctx,
        const ZL_Compressor* compressor,
        ZL_GraphID graphID,
        const ZL_RuntimeGraphParameters* rgp)
{
    ZL_ASSERT_NN(cctx);
    if (compressor)
        ZL_RET_R_IF_ERR(ZL_CCtx_refCompressor(cctx, compressor));
    return GCParams_setStartingGraphID(
            &cctx->requestedGCParams, graphID, rgp, cctx->sessionArena);
}

int CCTX_getAppliedGParam(const ZL_CCtx* cctx, ZL_CParam gcparam)
{
    ZL_ASSERT_NN(cctx);
    return GCParams_getParameter(&cctx->appliedGCParams, gcparam);
}

int CCTX_isGraphSet(const ZL_CCtx* cctx)
{
    return (cctx->cgraph != NULL);
}

ZL_Report ZL_CCtx_resetParameters(ZL_CCtx* cctx)
{
    // TODO (@Cyan) :
    // Why @return ZL_Report ? This operation *could* fail (if we wish so)
    // if there is a rule that requires setting to be done at "right" moment,
    // for example, not during an unfinished compression.
    // That being said, maybe that's unnecessary,
    // because ongoing compression uses .appliedGCParams, not
    // .requestedGCParams. Furthermore, maybe we could imagine cases where it's
    // necessary to "abandon" an ongoing compression session, though resetting
    // parameters alone won't be enough for this, so it needs to be paired with
    // something else. In `zstd`, there are different levels of reset
    // (parameters, session, or both). Maybe the same would be needed here ?
    ZL_zeroes(&cctx->requestedGCParams, sizeof(cctx->requestedGCParams));
    cctx->cgraph       = NULL;
    cctx->comment.size = 0;
    cctx->comment.data = NULL;
    ZL_Compressor_free(cctx->internal_cgraph);
    cctx->internal_cgraph = NULL;
    return ZL_returnSuccess();
}

// Note : while not supported yet, @compressor could be NULL,
//        when using only standard Graphs.
ZL_Report ZL_CCtx_refCompressor(ZL_CCtx* cctx, const ZL_Compressor* compressor)
{
    ZL_ASSERT_NN(cctx);
    ZL_RET_R_IF_EQ(
            graph_invalid,
            CGRAPH_getStartingGraphID(compressor).gid,
            0,
            "The cgraph's starting graph ID is not set, it must be set via "
            "ZL_Compressor_selectStartingGraphID() before it can be used.");
    cctx->cgraph = compressor;
    // Erase previously set advanced parameters
    return GCParams_resetStartingGraphID(&cctx->requestedGCParams);
}

ZL_Report CCTX_setLocalCGraph_usingGraph2Desc(
        ZL_CCtx* cctx,
        ZL_Graph2Desc graphDesc)
{
    ZL_LOG(FRAME, "CCTX_setLocalCGraph_usingGraph2Desc");
    ZL_ASSERT_NN(cctx);
    ZL_Compressor_free(cctx->internal_cgraph); // compatible with NULL
    cctx->internal_cgraph = ZL_Compressor_create();
    ZL_RET_R_IF_NULL(allocation, cctx->internal_cgraph);
    ZL_GraphID const startingNode =
            graphDesc.f(cctx->internal_cgraph, graphDesc.customParams);
    ZL_RET_R_IF_ERR(ZL_Compressor_selectStartingGraphID(
            cctx->internal_cgraph, startingNode));
    // Creation is all fine, let's finalize the reference
    return ZL_CCtx_refCompressor(cctx, cctx->internal_cgraph);
}

ZL_Report CCTX_setAppliedParameters(ZL_CCtx* cctx)
{
    ZL_DLOG(FRAME, "CCTX_setAppliedParameters");
    ZL_ASSERT_NN(cctx);
    GCParams* const dst      = &cctx->appliedGCParams;
    const GCParams* const p1 = &cctx->requestedGCParams;
    ZL_ASSERT_NN(cctx->cgraph);
    const GCParams* const p2 = CGRAPH_getGCParams(cctx->cgraph);
    ZL_ASSERT_NN(p2);
    const GCParams* const p3 = &GCParams_default;

    *dst = *p1;
    GCParams_applyDefaults(dst, p2);
    GCParams_applyDefaults(dst, p3);

    return GCParams_finalize(dst);
}

void CCTX_setDst(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        size_t writtenSize)
{
    ZL_DLOG(BLOCK,
            "CCTX_setDst: set dst buffer of capacity %zu (with %zu written)",
            dstCapacity,
            writtenSize);
    ZL_ASSERT_NN(cctx);
    ZL_ASSERT_NN(dst);
    ZL_ASSERT_LE(writtenSize, dstCapacity);
    cctx->dstBuffer        = dst;
    cctx->dstCapacity      = dstCapacity;
    cctx->currentFrameSize = writtenSize;
}

// @return a read stream by its RTStreamID.
// Note: ID **must** be valid.
static const ZL_Data* CCTX_getRStream(const ZL_CCtx* cctx, RTStreamID rtsid)
{
    return RTGM_getRStream(&cctx->rtgraph, rtsid);
}

ZL_Report CCTX_sendTrHeader(ZL_CCtx* cctx, RTNodeID rtnodeid, ZL_RBuffer trh)
{
    ZL_ASSERT_NN(cctx);
    ZL_TRY_LET_R(headerPos, CCTX_TransformHeaders_stage(&cctx->trHeaders, trh));
    RTGM_setNodeHeaderSegment(
            &cctx->rtgraph,
            rtnodeid,
            (NodeHeaderSegment){ headerPos, trh.size });
    return ZL_returnSuccess();
}

// --------------------------
// Actions
// --------------------------

/* @return nb of output Streams created
 * and @rtnid the created RTNode.*/
static ZL_Report CCTX_runCNode_wParams(
        ZL_CCtx* cctx,
        ZL_NodeID nodeid,
        RTNodeID* rtnid,
        const ZL_Data* inputs[],
        const RTStreamID irtsids[],
        size_t nbInputs,
        const CNode* cnode,
        const ZL_LocalParams* lparams)
{
    ZL_DLOG(TRANSFORM,
            "CCTX_runCNode_wParams (nbInputs=%u, lparams=%p)",
            nbInputs,
            lparams);
    ZL_ASSERT_NN(cctx);
    ZL_ASSERT_NN(cnode);
    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);

    // Check inputs
    ZL_RET_R_IF_NOT(
            node_invalid_input, CNODE_isNbInputsCompatible(cnode, nbInputs));
    // This check also takes care of versions<=15, which only support 1 input.
    ZL_RET_R_IF_GT(
            node_versionMismatch,
            nbInputs,
            ZL_runtimeNodeInputLimit(cctx->appliedGCParams.formatVersion),
            "Too many inputs (%u) for format version %u (max=%u)",
            nbInputs,
            cctx->appliedGCParams.formatVersion,
            ZL_runtimeNodeInputLimit(cctx->appliedGCParams.formatVersion));
    ZL_ASSERT_NN(inputs);
    for (ZL_IDType n = 0; n < nbInputs; n++) {
        ZL_RET_R_IF_NE(
                node_unexpected_input_type,
                ZL_Data_type(inputs[n]),
                CNODE_getInputType(cnode, n));
    }

    CNODE_FormatInfo const cnfi = CNODE_getFormatInfo(cnode);
    int const reqFormat = CCTX_getAppliedGParam(cctx, ZL_CParam_formatVersion);
    ZL_RET_R_IF(
            node_versionMismatch,
            reqFormat < (int)cnfi.minFormatVersion
                    || (int)cnfi.maxFormatVersion < reqFormat,
            "Node %s (versions[%u-%u]) is incompatible with requested format version (%i)",
            CNODE_getName(cnode),
            cnfi.minFormatVersion,
            cnfi.maxFormatVersion,
            reqFormat);

    // Note: this action registers @cnode without its (optional) new @lparams,
    // but it's fine, since local parameters won't be requested again from there
    {
        ZL_TRY_LET_T(
                RTNodeID,
                tmp,
                RTGM_createNode(&cctx->rtgraph, cnode, irtsids, nbInputs));
        *rtnid = tmp;
    }

    ZL_Report const nbOuts = ENC_runTransform(
            &cnode->transformDesc,
            inputs,
            nbInputs,
            nodeid,
            *rtnid,
            cnode,
            lparams,
            cctx,
            cctx->codecArena,
            &cctx->cachedCodecStates);

    ZL_RET_R_IF_ERR(
            nbOuts,
            "Node '%s' failed: %s(%u)",
            CNODE_getName(cnode),
            ZL_ErrorCode_toString(ZL_errorCode(nbOuts)),
            ZL_errorCode(nbOuts));
    ZL_RET_R_IF_ERR(CCTX_checkOutputCommitted(cctx, *rtnid));

    // Free input streams _if allowed_, since they have been processed.
    // This is typically possible for internal outputs of internal Transforms
    // within a Dynamic Graph.
    // Note: this operation properly takes care of complex scenarios
    // where input Streams are still referenced into,
    // or when an Input must remain available for later decision update.
    for (size_t n = 0; n < nbInputs; n++) {
        RTGM_clearRTStream(&cctx->rtgraph, irtsids[n], /* protectRank*/ 0);
    }

    return nbOuts;
}

/* currently invoked from: ZL_Edge_runMultiInputNode
 * @return nb of output Streams created
 * and @rtnid the created RTNode.*/
ZL_Report CCTX_runNodeID_wParams(
        ZL_CCtx* cctx,
        RTNodeID* rtnid,
        const ZL_Data* inputs[],
        const RTStreamID irtsids[],
        size_t nbInputs,
        ZL_NodeID nodeid,
        const ZL_LocalParams* lparams)
{
    ZL_DLOG(BLOCK, "CCTX_runNodeID_wParams (nbInputs=%u)", nbInputs);
    ZL_RET_R_IF_EQ(
            node_invalid, ZL_NODE_ILLEGAL.nid, nodeid.nid, "Node is illegal");
    ZL_ASSERT_NN(cctx);
    const CNode* const cnode = CGRAPH_getCNode(cctx->cgraph, nodeid);
    ZL_RET_R_IF_NULL(
            node_invalid, cnode, "NodeID %u does not exist", nodeid.nid);
    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);
    return CCTX_runCNode_wParams(
            cctx, nodeid, rtnid, inputs, irtsids, nbInputs, cnode, lparams);
}

static ZL_Report
CCTX_storeStream(ZL_CCtx* cctx, const RTStreamID* isids, size_t nbIS)
{
    ZL_ASSERT_EQ(nbIS, 1); // single-stream only
    RTGM_storeStream(&cctx->rtgraph, isids[0]);
    return ZL_returnValue(0); // no output
}

// Wrapper to capture ZL_Report errors
static ZL_Report CCTX_convertInputs_internal(
        ZL_CCtx* cctx,
        const RTStreamID* rtsid,
        RTStreamID* outRtsid,
        const ZL_Data* input,
        ZL_Type const inType,
        ZL_Type const portTypeMask)
{
    ZL_NodeID conversion = ICONV_implicitConversionNodeID(inType, portTypeMask);
    ZL_RET_R_IF_NOT(
            inputType_unsupported,
            ZL_NodeID_isValid(conversion),
            "cannot find an implicit conversion (%x => %x)",
            inType,
            portTypeMask);

    RTNodeID rtnodeid;
    ZL_RET_R_IF_ERR(CCTX_runNodeID_wParams(
            cctx, &rtnodeid, &input, rtsid, 1, conversion, NULL));
    // Implicit conversions are currently single-output only
    ZL_ASSERT_EQ(RTGM_getNbOutStreams(&cctx->rtgraph, rtnodeid), 1);
    *outRtsid = RTGM_getOutStreamID(&cctx->rtgraph, rtnodeid, 0);
    return ZL_returnSuccess();
}

/* batch conversion operation
 * write the updated rtsids into @converted_rtsids.
 * @converted_rtsids must be already allocated and have the right size.
 * If any conversion operation fails, @returns an error. */
static ZL_Report CCTX_convertInputs(
        ZL_CCtx* cctx,
        RTStreamID converted_rtsids[],
        const RTStreamID orig_rtsids[],
        size_t nbInputs,
        const ZL_Type* dstPortMasks,
        size_t nbPorts)
{
    ZL_ASSERT_GE(nbInputs, 1);
    for (size_t n = 0; n < nbInputs; n++) {
        const ZL_Data* input = CCTX_getRStream(cctx, orig_rtsids[n]);
        ZL_Type const inType = ZL_Data_type(input);
        // If the destination Graph supports Variable Inputs,
        // the last Port can be used multiple times.
        // Therefore @nbInputs can be > nbPorts,
        // but all @inputs n >= (nbPorts-1) lead to the same (last) Port
        size_t const portN         = (n >= nbPorts - 1) ? nbPorts - 1 : n;
        ZL_Type const portTypeMask = dstPortMasks[portN];
        if (inType & portTypeMask) {
            // Direct support available: no conversion needed
            converted_rtsids[n] = orig_rtsids[n];
            continue;
        }
        // Type not directly supported by Port => attempt conversion
        ZL_Report res = CCTX_convertInputs_internal(
                cctx,
                &orig_rtsids[n],
                &converted_rtsids[n],
                input,
                inType,
                portTypeMask);
        WAYPOINT(
                on_cctx_convertOneInput,
                cctx,
                input,
                inType,
                portTypeMask,
                res);
        ZL_RET_R_IF_ERR(res);
    }
    return ZL_returnSuccess();
}

static ZL_Report GCTX_checkSuccessors(ZL_Graph* gctx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    ZL_ASSERT_NN(gctx);
    size_t const nbStreams = VECTOR_SIZE(gctx->streamCtxs);
    for (size_t n = 0; n < nbStreams; n++) {
        if (VECTOR_AT(gctx->streamCtxs, n).dest_set == sds_unassigned) {
            ZL_TernaryParam const backupMode =
                    (ZL_TernaryParam)CCTX_getAppliedGParam(
                            gctx->cctx, ZL_CParam_permissiveCompression);
            if (backupMode != ZL_TernaryParam_enable) {
                ZL_RET_R_ERR(successor_invalid);
            }
        }
    }
    return ZL_returnSuccess();
}

/* note: presumed successful */
static size_t GCTX_getNbSuccessors(const ZL_Graph* gctx)
{
    ZL_ASSERT_NN(gctx);
    size_t const nbStreams         = VECTOR_SIZE(gctx->streamCtxs);
    size_t nbStreamsWithSuccessors = 0;
    for (size_t n = 0; n < nbStreams; n++) {
        if (VECTOR_AT(gctx->streamCtxs, n).dest_set == sds_destSet_trigger)
            nbStreamsWithSuccessors++;
        if (VECTOR_AT(gctx->streamCtxs, n).dest_set == sds_unassigned) {
            // If there are still unassigned Streams when calling this
            // function, it can only mean that permissive mode is enabled
            ZL_ASSERT_EQ(
                    (ZL_TernaryParam)CCTX_getAppliedGParam(
                            gctx->cctx, ZL_CParam_permissiveCompression),
                    ZL_TernaryParam_enable);
            nbStreamsWithSuccessors++;
        }
    }
    return nbStreamsWithSuccessors;
}

typedef struct {
    ZL_GraphID graphID;
    const ZL_RuntimeGraphParameters* rgp;
    const RTStreamID* rtInputs;
    size_t nbInputs;
} SuccessorInfo;

/* Implementation notes :
 * - @successorsArray is allocated and owned by the caller,
 *   currently CCTX_runGraph_internal().
 *   GCTX_getSuccessors() just fills the array.
 * - @successorsArray *must* be sized properly, using GCTX_getNbSuccessors()
 * - Given these requirements, this function does not fail
 */
static void GCTX_getSuccessors(
        const ZL_Graph* gctx,
        SuccessorInfo* successorsArray,
        size_t nbSuccessors)
{
    ZL_DLOG(BLOCK, "GCTX_getSuccessors (nbSuccessors=%zu)", nbSuccessors);
    size_t const nbStreams = VECTOR_SIZE(gctx->streamCtxs);
    ZL_ASSERT_EQ(nbSuccessors, GCTX_getNbSuccessors(gctx));
    size_t successorIdx = 0;
    for (size_t n = 0; n < nbStreams; n++) {
        const DG_StreamCtx* const sctx = &VECTOR_AT(gctx->streamCtxs, n);
        if (sctx->dest_set == sds_destSet_trigger) {
            size_t const sDescPos = sctx->successionPos;
            ZL_ASSERT_LT(sDescPos, VECTOR_SIZE(gctx->dstGraphDescs));
            DestGraphDesc const sd = VECTOR_AT(gctx->dstGraphDescs, sDescPos);
            ZL_ASSERT_LT(sd.rtiStartIdx, VECTOR_SIZE(gctx->rtsids));
            const RTStreamID* rtsids = &VECTOR_AT(gctx->rtsids, sd.rtiStartIdx);
            ZL_ASSERT_LT(successorIdx, nbSuccessors);
            successorsArray[successorIdx++] = (SuccessorInfo){
                .graphID  = sd.destGid,
                .rgp      = sd.rGraphParams,
                .rtInputs = rtsids,
                .nbInputs = sd.nbInputs,
            };
        }
        // In permissive mode, assign a default Graph to any unassigned
        // Stream
        if (sctx->dest_set == sds_unassigned) {
            ZL_ASSERT_EQ(
                    (ZL_TernaryParam)CCTX_getAppliedGParam(
                            gctx->cctx, ZL_CParam_permissiveCompression),
                    ZL_TernaryParam_enable);
            successorsArray[successorIdx++] = (SuccessorInfo){
                .graphID  = ZL_GRAPH_COMPRESS_GENERIC,
                .rgp      = NULL,
                .rtInputs = &VECTOR_AT(gctx->streamCtxs, n).rtsid,
                .nbInputs = 1,
            };
        }
    }
}

/* Invoked from CCTX_runGraph_internal() */
static ZL_Report CCTX_runSuccessors(
        ZL_CCtx* cctx,
        const SuccessorInfo* successorArray,
        size_t nbSuccessors,
        unsigned depth)
{
    ZL_DLOG(SEQ, "CCTX_runSuccessors on %zu successors", nbSuccessors);
    for (size_t n = 0; n < nbSuccessors; n++) {
        const SuccessorInfo* const si = successorArray + n;
        ZL_RET_R_IF_ERR(CCTX_runSuccessor(
                cctx,
                si->graphID,
                si->rgp,
                si->rtInputs,
                si->nbInputs,
                depth + 1));
    }
    return ZL_returnSuccess();
}

/* Implementation note:
 * CCTX_runGraph_internal(), invoked by CCTX_runGraph(), features multiple
 * exit points. This 2-stages design ensures that the final _clean action in
 * the outer CCTX_runGraph() cannot be skipped.
 */
static ZL_Report CCTX_runGraph_internal(
        ZL_CCtx* cctx,
        ZL_Graph* const gctx,
        const ZL_GraphID graphid,
        ZL_Edge* inputs[],
        size_t nbInputs,
        unsigned depth)
{
    (void)graphid; // required only for waypoints
    // All streams created after this index will be created by the dynamic
    // graph
    WAYPOINT(
            on_migraphEncode_start,
            gctx,
            CCTX_getCGraph(cctx),
            graphid,
            inputs,
            nbInputs);
    ZL_Report const graphExecutionReport =
            GCTX_runMultiInputGraph(gctx, inputs, nbInputs);
    IF_WAYPOINT_ENABLED(on_migraphEncode_end, gctx)
    {
        if (ZL_isError(graphExecutionReport)) {
            WAYPOINT(on_migraphEncode_end, gctx, NULL, 0, graphExecutionReport);
        } else {
            size_t nbSuccs = VECTOR_SIZE(gctx->dstGraphDescs);
            DECLARE_VECTOR_TYPE(ZL_GraphID);
            VECTOR(ZL_GraphID) succGids;
            VECTOR_INIT(succGids, nbSuccs);
            for (size_t i = 0; i < nbSuccs; i++) {
                bool pushbackSuccess = VECTOR_PUSHBACK(
                        succGids, VECTOR_AT(gctx->dstGraphDescs, i).destGid);
                ZL_RET_R_IF_NOT(
                        allocation,
                        pushbackSuccess,
                        "Unable to append to the waypoint succGids vector");
            }
            WAYPOINT(
                    on_migraphEncode_end,
                    gctx,
                    VECTOR_DATA(succGids),
                    nbSuccs,
                    ZL_returnSuccess());
            VECTOR_DESTROY(succGids);
        }
    }
    ALLOC_Arena_freeAll(cctx->graphArena);
    ZL_RET_R_IF_ERR(graphExecutionReport);

    // If an error happened during Dynamic Graph, but was not checked and
    // then not reported by the Dynamic Graph function, it's caught here.
    ZL_ASSERT_NN(gctx);
    ZL_RET_R_IF_ERR(gctx->status);

    // Check if some streams have no Successors
    // Error out, or set them to default backup if permissive mode
    ZL_RET_R_IF_ERR(GCTX_checkSuccessors(gctx));
    // After that point, if there are unassigned Streams but the check was
    // successful, it means that permissive mode is enabled. Consequently,
    // permissive mode is considered active for the rest of the function.

    // Store successors (local array)
    size_t const nbSuccessors = GCTX_getNbSuccessors(gctx);
    // Implementation Note: cannot use Graph's Arena for successors, because
    // CCTX_runSuccessors() will start Graphs that will reset Graph Arena.
    // An alternative could be to use Session Arena,
    // but in this case, memory will only be reclaimed at end of
    // compression. That being said, it may not be such a big deal if memory
    // used is low.
    SuccessorInfo* const successors =
            ZL_malloc(nbSuccessors * sizeof(successors[0]));
    ZL_RET_R_IF_NULL(allocation, successors);
    GCTX_getSuccessors(gctx, successors, nbSuccessors);

    // Run successors
    ZL_Report const rsr =
            CCTX_runSuccessors(cctx, successors, nbSuccessors, depth);
    ZL_free(successors);
    return rsr;
}

static ZL_Graph GCTX_init(ZL_CCtx* cctx, const ZL_FunctionGraphDesc* dgd)
{
    return (ZL_Graph){
        .cctx          = cctx,
        .rtgraph       = &cctx->rtgraph,
        .streamCtxs    = VECTOR_EMPTY(ZL_ENCODER_GRAPH_LIMIT),
        .dstGraphDescs = VECTOR_EMPTY(ZL_ENCODER_GRAPH_LIMIT),
        .rtsids        = VECTOR_EMPTY(ZL_ENCODER_GRAPH_LIMIT),
        .status        = ZL_returnSuccess(),
        .dgd           = dgd,
        .graphArena    = cctx->graphArena,
        .chunkArena    = cctx->chunkArena,
    };
}

const ZL_Input* ZL_Edge_getData(const ZL_Edge* sctx)
{
    ZL_ASSERT_NN(sctx);
    ZL_ASSERT_NN(sctx->gctx);
    return ZL_codemodDataAsInput(CCTX_getRStream(
            sctx->gctx->cctx,
            VECTOR_AT(sctx->gctx->streamCtxs, sctx->scHandle).rtsid));
}

ZL_Report ZL_Edge_setIntMetadata(ZL_Edge* edge, int mId, int mValue)
{
    ZL_CCtx* cctx = edge->gctx->cctx;
    RTStreamID rtstreamid =
            VECTOR_AT(edge->gctx->streamCtxs, edge->scHandle).rtsid;
    const RTGraph* rtgraph = &cctx->rtgraph;
    ZL_Data* s = VECTOR_AT(rtgraph->streams, rtstreamid.rtsid).stream;
    return ZL_Data_setIntMetadata(s, mId, mValue);
}

static ZL_Report CCTX_runSegmenter(
        ZL_CCtx* cctx,
        ZL_GraphID graphid,
        const ZL_RuntimeGraphParameters* rgp,
        const RTStreamID* rtsids,
        size_t nbInputs)
{
    ZL_ASSERT_NN(cctx);
    ZL_ASSERT_NN(cctx->cgraph);
    ZL_ASSERT_EQ(CGRAPH_graphType(cctx->cgraph, graphid), gt_segmenter);
    ZL_DLOG(BLOCK,
            "CCTX_runSegmenter '%s'(id=%u) with %zu inputs",
            ZL_Compressor_Graph_getName(cctx->cgraph, graphid),
            graphid.gid,
            nbInputs);
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_DLOG(SEQ, "RTStreamID: %u", rtsids[n].rtsid);
    }

    // Check version
    ZL_RET_R_IF_LT(
            formatVersion_unsupported,
            cctx->appliedGCParams.formatVersion,
            ZL_CHUNK_VERSION_MIN,
            "Segmenter is supported starting wire format version %u > %u (requested)",
            ZL_CHUNK_VERSION_MIN,
            cctx->appliedGCParams.formatVersion);

    // Check Input Types
    ALLOC_ARENA_MALLOC_CHECKED(ZL_Type, inTypes, nbInputs, cctx->sessionArena);
    ZL_RET_R_IF_NULL(allocation, inTypes);
    const ZL_SegmenterDesc* segDesc =
            CGRAPH_getSegmenterDesc(cctx->cgraph, graphid);
    size_t const nbPorts = segDesc->numInputs;
    ZL_ASSERT_GE(nbPorts, 1);
    int needConversion = 0;
    for (size_t n = 0; n < nbInputs; n++) {
        inTypes[n]          = ZL_Data_type(CCTX_getRStream(cctx, rtsids[n]));
        size_t outn         = (n >= nbPorts) ? nbPorts - 1 : n;
        ZL_Type outTypeMask = segDesc->inputTypeMasks[outn];
        needConversion |= !(inTypes[n] & outTypeMask);
    }

    ZL_RET_R_IF(
            temporaryLibraryLimitation,
            needConversion,
            "Conversion of Input types not supported by Segmenters");
    // @note not strictly impossible, but requires some attention:
    // we don't want to create Nodes in front of the Segmenter.

    // Insert runtime parameters if needed
    if (rgp) {
        ALLOC_ARENA_MALLOC_CHECKED(
                ZL_SegmenterDesc, migd, 1, cctx->sessionArena);
        *migd = *segDesc;
        if (rgp->localParams)
            migd->localParams = *rgp->localParams;
        if (rgp->customGraphs) {
            migd->customGraphs    = rgp->customGraphs;
            migd->numCustomGraphs = rgp->nbCustomGraphs;
        }
        segDesc = migd;
    }

    cctx->segmenterStarted           = 1;
    ZL_Segmenter* const segmenterCtx = SEGM_init(
            segDesc,
            nbInputs,
            cctx,
            &cctx->rtgraph,
            cctx->sessionArena,
            cctx->chunkArena);
    WAYPOINT(on_segmenterEncode_start, segmenterCtx, /* placeholder */ NULL);
    const ZL_Report r = SEGM_runSegmenter(segmenterCtx);
    WAYPOINT(on_segmenterEncode_end, segmenterCtx, r);
    return r;
}

/* Invoked from: CCTX_runSupervisedGraph, CCTX_implicitConvert
 * note: at this point, the Graph is expected to be validated (correct
 * definition, correct Inputs) */
static ZL_Report CCTX_runGraphDesc(
        ZL_CCtx* cctx,
        const ZL_FunctionGraphDesc* migd,
        const ZL_GraphID graphid,
        const void* privateParam,
        const RTStreamID* rtsids,
        size_t nbInputs,
        unsigned depth)
{
    ZL_ASSERT_NN(cctx);
    ZL_DLOG(BLOCK,
            "CCTX_runGraphDesc on graph '%s(%zu)' with %zu inputs",
            STR_REPLACE_NULL(migd->name),
            graphid.gid,
            nbInputs);
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_DLOG(SEQ, "RTStreamID %u", rtsids[n].rtsid);
    }

    // create context elements
    ALLOC_ARENA_MALLOC_CHECKED(
            ZL_Edge*, inputsPtrs, nbInputs, cctx->graphArena);
    ALLOC_MALLOC_CHECKED(ZL_Edge, inputsArray, nbInputs);

    ZL_Graph graphCtx     = GCTX_init(cctx, migd);
    graphCtx.privateParam = privateParam;

    for (unsigned n = 0; n < nbInputs; n++) {
        const ZL_Report ret =
                SCTX_initInput(inputsArray + n, &graphCtx, rtsids[n]);
        if (ZL_isError(ret)) {
            ZL_free(inputsArray);
            GCTX_destroy(&graphCtx);
            ZL_RET_R(ret);
        }
        inputsPtrs[n] = inputsArray + n;
    }

    // run dynamic graph
    ZL_Report const dgr = CCTX_runGraph_internal(
            cctx, &graphCtx, graphid, inputsPtrs, nbInputs, depth);

    // clean up context elements
    // Note: graphArena was already reset within CCTX_runGraph_internal
    ZL_free(inputsArray);
    GCTX_destroy(&graphCtx);

    return dgr;
}

/* CCTX_runSupervisedGraphID_internal():
 * Control that the Graph can be invoked, and proceed to adaptations if need
 * be. Controls version, and Input Types. Triggers Implicit Type conversion
 * if need be. Route away `STORE` as a special operation. Then, if all good,
 * run GraphID. Note: Permissive Mode is currently triggered one level
 * above, in CCTX_runSuccessor()).
 * */
static ZL_Report CCTX_runSupervisedGraphID_internal(
        ZL_CCtx* cctx,
        ZL_GraphID graphid,
        const ZL_RuntimeGraphParameters* rgp,
        const RTStreamID* rtsids,
        size_t nbInputs,
        unsigned depth)
{
    // Ensure the graph exists
    ZL_ASSERT_NN(cctx);
    ZL_RET_R_IF_NOT(
            graph_invalid,
            CGRAPH_checkGraphIDExists(cctx->cgraph, graphid),
            "GraphID %u doesn't exist",
            graphid.gid);
    ZL_DLOG(BLOCK,
            "CCTX_runSupervisedGraphID_internal '%s'(id=%u) with %zu inputs",
            ZL_Compressor_Graph_getName(cctx->cgraph, graphid),
            graphid.gid,
            nbInputs);
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_DLOG(SEQ, "RTStreamID: %u", rtsids[n].rtsid);
    }

    // Check Input Types
    ALLOC_ARENA_MALLOC_CHECKED(ZL_Type, inTypes, nbInputs, cctx->graphArena);
    ZL_RET_R_IF_NULL(allocation, inTypes);
    const ZL_FunctionGraphDesc* dstGd =
            CGRAPH_getMultiInputGraphDesc(cctx->cgraph, graphid);
    size_t const nbPorts = dstGd->nbInputs;
    ZL_ASSERT_GE(nbPorts, 1);
    int needConversion = 0;
    for (size_t n = 0; n < nbInputs; n++) {
        inTypes[n]          = ZL_Data_type(CCTX_getRStream(cctx, rtsids[n]));
        size_t outn         = (n >= nbPorts) ? nbPorts - 1 : n;
        ZL_Type outTypeMask = dstGd->inputTypeMasks[outn];
        needConversion |= !(inTypes[n] & outTypeMask);
    }
    if (needConversion) {
        ZL_DLOG(SEQ,
                "running Graph %s requires conversion on some input(s)",
                STR_REPLACE_NULL(
                        ZL_Compressor_Graph_getName(cctx->cgraph, graphid)));
        ALLOC_ARENA_MALLOC_CHECKED(
                RTStreamID, newrtsids, nbInputs, cctx->graphArena);
        ZL_RET_R_IF_ERR(CCTX_convertInputs(
                cctx,
                newrtsids,
                rtsids,
                nbInputs,
                dstGd->inputTypeMasks,
                nbPorts));
        rtsids = newrtsids;
    }

    // Special case, it's the final store operation, which is single input
    if (CGRAPH_graphType(cctx->cgraph, graphid) == gt_store) {
        return CCTX_storeStream(cctx, rtsids, nbInputs);
    }

    // Now run the selected Graph, inserting runtime parameters if needed
    ZL_ASSERT_EQ(CGRAPH_graphType(cctx->cgraph, graphid), gt_miGraph);
    if (rgp) {
        ALLOC_ARENA_MALLOC_CHECKED(
                ZL_FunctionGraphDesc, migd, 1, cctx->graphArena);
        *migd = *dstGd;
        if (rgp->localParams)
            migd->localParams = *rgp->localParams;
        if (rgp->customGraphs) {
            migd->customGraphs   = rgp->customGraphs;
            migd->nbCustomGraphs = rgp->nbCustomGraphs;
        }
        if (rgp->customNodes) {
            migd->customNodes   = rgp->customNodes;
            migd->nbCustomNodes = rgp->nbCustomNodes;
        }
        dstGd = migd;
    }
    return CCTX_runGraphDesc(
            cctx,
            dstGd,
            graphid,
            CGRAPH_graphPrivateParam(cctx->cgraph, graphid),
            rtsids,
            nbInputs,
            depth);
}

/* Invoked from: CCTX_runSuccessor(), CCTX_triggerBackupMode()
 * Ensure graph-level memory arena is correctly freed,
 * even in early-exit scenarios (such as errors).
 * Implementation in CCTX_runSupervisedGraphID_internal().
 */
static ZL_Report CCTX_runSupervisedGraphID(
        ZL_CCtx* cctx,
        ZL_GraphID graphid,
        const ZL_RuntimeGraphParameters* rgp,
        const RTStreamID* rtsids,
        size_t nbInputs,
        unsigned depth)
{
    ZL_Report const r = CCTX_runSupervisedGraphID_internal(
            cctx, graphid, rgp, rtsids, nbInputs, depth);
    ALLOC_Arena_freeAll(cctx->graphArena);
    return r;
}

static ZL_Report CCTX_triggerBackupMode(
        ZL_CCtx* cctx,
        const RTStreamID* rtsids,
        size_t nbInputs,
        unsigned depth)
{
    ZL_DLOG(BLOCK, "CCTX_triggerBackupMode (nbInputs==%u)", nbInputs);
    ZL_ASSERT_EQ(cctx->inBackupMode, 0, "Recursive backup shouldn't happen");
    ZL_RET_R_IF_NE(
            logicError,
            cctx->inBackupMode,
            0,
            "Recursive backup shouldn't happen");
    cctx->inBackupMode = 1;
    ZL_Report outcome  = CCTX_runSupervisedGraphID(
            cctx, ZL_GRAPH_COMPRESS_GENERIC, NULL, rtsids, nbInputs, depth);
    if (!ZL_isError(outcome)) {
        cctx->inBackupMode = 0;
    }
    return outcome;
}

/* Implementation note:
 * CCTX_runSuccessor_internal(), invoked by CCTX_runSuccessor().
 * This 2-stages design ensures that Stream cleaning cannot be skipped despite
 * multiple exit points. Will invoke CCTX_runSupervisedGraph(). Is also in
 * charge of Permissive (backup) mode.
 */
static ZL_Report CCTX_runSuccessor_internal(
        ZL_CCtx* cctx,
        ZL_GraphID graphid,
        const ZL_RuntimeGraphParameters* rgp,
        const RTStreamID* rtsids,
        size_t nbInputs,
        unsigned depth)
{
    ZL_ASSERT_NN(cctx);
    ZL_SCOPE_GRAPH_CONTEXT(cctx, { .graphID = graphid });

    // Special: Single small Input get STORED immediately
    ZL_ASSERT_GT(nbInputs, 0);
    if (graphid.gid != ZL_GRAPH_SERIAL_STORE.gid && nbInputs == 1) {
        const ZL_Data* const s = RTGM_getRStream(&cctx->rtgraph, rtsids[0]);
        if (ZL_Data_type(s) != ZL_Type_string) {
            size_t const inSizeT = ZL_Data_contentSize(s);
            int const inputSize  = (inSizeT > INT_MAX) ? INT_MAX : (int)inSizeT;
            int const sizeLimit =
                    CCTX_getAppliedGParam(cctx, ZL_CParam_minStreamSize);
            if (inputSize < sizeLimit) {
                return CCTX_runSupervisedGraphID(
                        cctx, ZL_GRAPH_STORE1, NULL, rtsids, nbInputs, depth);
            }
        }
    }

    // save for backup
    size_t const nbNodesBefore = RTGM_getNbNodes(&cctx->rtgraph);

    ZL_Report outcome = CCTX_runSupervisedGraphID(
            cctx, graphid, rgp, rtsids, nbInputs, depth);
    // return on success
    if (!ZL_isError(outcome))
        return outcome;

    // Error ongoing : check if permissive mode is set
    ZL_TernaryParam const backupMode = (ZL_TernaryParam)CCTX_getAppliedGParam(
            cctx, ZL_CParam_permissiveCompression);
    ZL_DLOG(BLOCK, "node just failed : permissiveMode = %u", backupMode);
    if (backupMode != ZL_TernaryParam_enable || cctx->inBackupMode) {
        ZL_RET_R(outcome);
    }

    ZL_E_log(ZL_RES_error(outcome), ZL_LOG_LVL_V);
    // Report the error as a warning
    ZL_RES_convertToWarning(cctx, outcome);

    // Clear the RTGraph from all Streams and Nodes created after that
    // point. Note : this algorithm acts on the RTGraphs storage manager
    // directly,
    // **it only works in a serial "depth first" strategy**,
    // so that all nodes and streams created after @nbNodesBefore
    // are necessarily descendants of current Successor.
    // If the scanning strategy changes (breadth first for example)
    // or if the engine wants to support multi-threaded compression,
    // a different solution will be required.
    ZL_DLOG(SEQ,
            "Reverting validated Nodes from %zu to %zu",
            RTGM_getNbNodes(&cctx->rtgraph),
            nbNodesBefore);
    RTGM_clearNodesFrom(&cctx->rtgraph, (unsigned)nbNodesBefore);

    // Now execute backup strategy
    return CCTX_triggerBackupMode(cctx, rtsids, nbInputs, depth);
}

/* Invoked from: CCTX_startGraph(), CCTX_runSuccessors()
 * Upper echelon, acts as a graph type dispatcher,
 * routing between segmenter and normal graphs.
 * Is also in charge of Stream cleaning */
ZL_Report CCTX_runSuccessor(
        ZL_CCtx* cctx,
        ZL_GraphID graphid,
        const ZL_RuntimeGraphParameters* rgp,
        const RTStreamID* rtInputs,
        size_t nbInputs,
        unsigned depth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(cctx);
    ZL_DLOG(BLOCK, "CCTX_runSuccessor (graphid=%u)", graphid.gid);
    int const isSegmentable =
            (rtInputs[0].rtsid == 0 && nbInputs == cctx->nbInputs
             && !cctx->segmenterStarted);

    // Segmenter
    if (CGRAPH_graphType(cctx->cgraph, graphid) == gt_segmenter) {
        if (isSegmentable) {
            return CCTX_runSegmenter(cctx, graphid, rgp, rtInputs, nbInputs);
        }
        ZL_ERR(graph_invalid, "Segmenter can only be used on full input");
    }

    // Normal Graph
    for (size_t n = 0; n < nbInputs; n++) {
        RTGM_guardRTStream(&cctx->rtgraph, rtInputs[n], depth);
    }
    ZL_Report const r = CCTX_runSuccessor_internal(
            cctx, graphid, rgp, rtInputs, nbInputs, depth);
    if (!isSegmentable) {
        for (size_t n = 0; n < nbInputs; n++) {
            RTGM_clearRTStream(&cctx->rtgraph, rtInputs[n], depth);
        }
    }
    return r;
}

/* Expectation :
 * - cctx is non null
 * - a compressor is set
 * - applied parameters set
 */
ZL_Report
CCTX_startCompression(ZL_CCtx* cctx, const ZL_Data* inputs[], size_t nbInputs)
{
    ZL_DLOG(FRAME,
            "CCTX_startCompression (%zu inputs; input[0].size = %zu)",
            nbInputs,
            ZL_Data_contentSize(inputs[0]));
    ZL_ASSERT_NN(cctx);
    ZL_ASSERT_NN(inputs);
    ZL_RESULT_DECLARE_SCOPE_REPORT(cctx);

    /* Current library limitation :
     * Compression requires attaching a Compressor.
     * So this section should only be reached after a Compressor is set.
     * In the future, it will be possible to start compression
     * without setting a Compressor, by employing standard Graphs only.
     */
    if (cctx->cgraph == NULL) {
        ZL_ERR(graph_invalid);
    }

    // Check that tmp buffers are empty
    // Note: sessionArena can be already in use to store parameters
    ZL_ASSERT_EQ(ALLOC_Arena_memUsed(cctx->chunkArena), 0);
    ZL_ASSERT_EQ(ALLOC_Arena_memUsed(cctx->graphArena), 0);
    ZL_ASSERT_EQ(ALLOC_Arena_memUsed(cctx->codecArena), 0);
    ZL_ASSERT_EQ(ALLOC_Arena_memUsed(cctx->rtgraph.rtsidsArena), 0);
    ZL_ASSERT_EQ(ALLOC_Arena_memUsed(cctx->rtgraph.streamArena), 0);
    ZL_ASSERT_EQ(VECTOR_SIZE(cctx->trHeaders.stagingHeaderStream), 0);
    ZL_ASSERT_EQ(VECTOR_SIZE(cctx->trHeaders.sentHeaderStream), 0);

    // Map inputs
    cctx->inputs = ZL_codemodDatasAsInputs(inputs);
    ZL_ERR_IF_LT(nbInputs, 1, successor_invalidNumInputs);
    ZL_ASSERT_LT(nbInputs, INT_MAX);
    cctx->nbInputs         = (unsigned)nbInputs;
    cctx->segmenterStarted = 0;
    ALLOC_ARENA_MALLOC_CHECKED(
            RTStreamID, rtsids, nbInputs, cctx->sessionArena);
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_TRY_LET(RTStreamID, rtsid, RTGM_refInput(&cctx->rtgraph, inputs[n]));
        rtsids[n] = rtsid;
    }

    // Retrieve the starting Graph
    ZL_GraphID starting_graphid = CGRAPH_getStartingGraphID(cctx->cgraph);
    const ZL_RuntimeGraphParameters* starting_graph_params = NULL;
    if (GCParams_explicitStartSet(&cctx->appliedGCParams)) {
        starting_graphid      = GCParams_explicitStart(&cctx->appliedGCParams);
        starting_graph_params = GCParams_startParams(&cctx->appliedGCParams);
    }

    // Run the starting Graph on the Inputs
    // This is depth 1, which is the highest level of protection,
    // allowing the Graph to make redirection decisions if need be.
    // Note: depth==0 means "unprotected"
    ZL_ERR_IF_ERR(CCTX_runSuccessor(
            cctx,
            starting_graphid,
            starting_graph_params,
            rtsids,
            nbInputs,
            /* depth */ 1));

    if (cctx->segmenterStarted == 0) {
        /* no segmenter -> only one chunk */
        ZL_ERR_IF_ERR(CCTX_flushChunk(cctx, inputs, nbInputs));
    }

    // Frame footer
    if (CCTX_getAppliedGParam(cctx, ZL_CParam_formatVersion)
        >= ZL_CHUNK_VERSION_MIN) {
        // Append end-of-frame marker
        ZL_ASSERT_LE(cctx->currentFrameSize, cctx->dstCapacity);
        ZL_ERR_IF_LT(
                cctx->dstCapacity - cctx->currentFrameSize,
                1,
                dstCapacity_tooSmall);
        ZL_write8((char*)cctx->dstBuffer + cctx->currentFrameSize, 0);
        cctx->currentFrameSize += 1;
    }
    ZL_DLOG(FRAME, "Final compressed size: %zu", cctx->currentFrameSize);

    return ZL_returnValue(cctx->currentFrameSize);
}

void* CCTX_getWPtrFromNewStream(
        ZL_CCtx* cctx,
        RTNodeID rtnodeid,
        int outStreamIdx,
        size_t eltWidth,
        size_t nbElt)
{
    ZL_DLOG(BLOCK,
            "CCTX_getWPtrFromNewStream (for rtnodeid = %u)",
            rtnodeid.rtnid);
    return ZL_Data_wPtr(
            CCTX_getNewStream(cctx, rtnodeid, outStreamIdx, eltWidth, nbElt));
}

ZL_Data* CCTX_getNewStream(
        ZL_CCtx* cctx,
        RTNodeID rtnodeid,
        int outcomeID,
        size_t eltWidth,
        size_t nbElt)
{
    ZL_DLOG(BLOCK, "CCTX_getNewStream (for rtnodeid = %u)", rtnodeid.rtnid);
    // Combined operation :
    // - create (and register) a new buffer for a new stream
    // - attach the stream to the rtnode
    // - return the stream

    const CNode* const cnode = RTGM_getCNode(&cctx->rtgraph, rtnodeid);
    int const isVO           = CNODE_isVO(cnode, outcomeID);

    // Create a new stream
    ZL_RESULT_OF(RTStreamID)
    const wrappedNewRTStreamID = RTGM_addStream(
            &cctx->rtgraph,
            rtnodeid,
            outcomeID,
            isVO,
            CNODE_getOutStreamType(cnode, outcomeID),
            eltWidth,
            nbElt);
    if (ZL_RES_isError(wrappedNewRTStreamID)) {
        return NULL; // TODO: bubble up exact error in the future
    }
    RTStreamID const newRTStreamID = ZL_RES_value(wrappedNewRTStreamID);
    return RTGM_getWStream(&cctx->rtgraph, newRTStreamID);
}

ZL_Data* CCTX_refContentIntoNewStream(
        ZL_CCtx* cctx,
        RTNodeID rtnodeid,
        int outcomeID,
        size_t eltWidth,
        size_t nbElts,
        ZL_Data const* src,
        size_t offsetBytes)
{
    ZL_DLOG(BLOCK,
            "CCTX_refContentIntoNewStream (rtnodeid = %u)",
            rtnodeid.rtnid);

    // Retrieve the stream type

    // Create a new stream
    const CNode* const cnode = RTGM_getCNode(&cctx->rtgraph, rtnodeid);
    ZL_Type const stype      = CNODE_getOutStreamType(cnode, outcomeID);
    int const isVO           = CNODE_isVO(cnode, outcomeID);
    ZL_RESULT_OF(RTStreamID)
    const wrappedNewRTStreamID = RTGM_refContentIntoNewStream(
            &cctx->rtgraph,
            rtnodeid,
            outcomeID,
            isVO,
            stype,
            eltWidth,
            nbElts,
            src,
            offsetBytes);
    if (ZL_RES_isError(wrappedNewRTStreamID)) {
        return NULL; // TODO: bubble up error in the future
    }
    RTStreamID const newRTStreamID = ZL_RES_value(wrappedNewRTStreamID);
    return RTGM_getWStream(&cctx->rtgraph, newRTStreamID);
}

ZL_Report CCTX_setOutBufferSizes(
        ZL_CCtx* cctx,
        RTNodeID rtnodeid,
        const size_t writtenSizes[],
        size_t nbOutStreams)
{
    ZL_DLOG(BLOCK,
            "CCTX_setOutBufferSizes (node id %u => %zu buffs)",
            rtnodeid.rtnid,
            nbOutStreams);

    ZL_ASSERT_NN(cctx);
    ZL_ASSERT_LT(nbOutStreams, INT_MAX);
    for (int n = 0; n < (int)nbOutStreams; n++) {
        RTStreamID const rtstreamid =
                RTGM_getOutStreamID(&cctx->rtgraph, rtnodeid, n);
        ZL_RET_R_IF_ERR(ZL_Data_commit(
                RTGM_getWStream(&cctx->rtgraph, rtstreamid), writtenSizes[n]));
    }
    return ZL_returnSuccess();
}

ZL_Report CCTX_checkOutputCommitted(const ZL_CCtx* cctx, RTNodeID rtnodeid)
{
    ZL_ASSERT_NN(cctx);
    size_t const nbOutStreams = RTGM_getNbOutStreams(&cctx->rtgraph, rtnodeid);
    ZL_DLOG(BLOCK,
            "CCTX_checkOutputCommitted (nodeid %u => %zu output streams)",
            rtnodeid.rtnid,
            nbOutStreams);
    ZL_ASSERT_LT(nbOutStreams, INT_MAX);
    for (int n = 0; n < (int)nbOutStreams; n++) {
        RTStreamID const rtstreamid =
                RTGM_getOutStreamID(&cctx->rtgraph, rtnodeid, n);
        if (!STREAM_isCommitted(CCTX_getRStream(cctx, rtstreamid))) {
            ZL_RET_R_ERR(
                    transform_executionFailure,
                    "Error from Transform '%s'(%u): output stream %i/%zu was not committed",
                    CNODE_getName(RTGM_getCNode(&cctx->rtgraph, rtnodeid)),
                    CNODE_getTransformID(
                            RTGM_getCNode(&cctx->rtgraph, rtnodeid))
                            .trid,
                    n,
                    nbOutStreams);
        };
    }
    return ZL_returnSuccess();
}

ZL_Report CCTX_listBuffersToStore(
        const ZL_CCtx* cctx,
        ZL_RBuffer* rba,
        size_t rbaCapacity)
{
    ZL_ASSERT_NN(cctx);
    ZL_ASSERT_NN(rba);
    // start by Transforms' header stream
    ZL_ASSERT_GT(rbaCapacity, 1);
    rba[0] = ZL_RBuffer_fromVector(&cctx->trHeaders.sentHeaderStream);
    rbaCapacity--;
    ZL_TRY_LET_R(
            nbStreams,
            RTGM_listBuffersToStore(&cctx->rtgraph, rba + 1, rbaCapacity));
    ZL_ASSERT_LE(nbStreams, rbaCapacity);
    return ZL_returnValue(nbStreams + 1);
}

static ZL_Report CCTX_writeChunkHeader(
        const ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const GraphInfo* gi)
{
    ZL_DLOG(BLOCK, "CCTX_writeChunkHeader (%zu inputs)", gi->nbSessionInputs);

    uint32_t formatVersion =
            (uint32_t)CCTX_getAppliedGParam(cctx, ZL_CParam_formatVersion);
    ZL_ASSERT_NE(formatVersion, 0, "Format version should not be 0.");
    ZL_ASSERT(
            ZL_isFormatVersionSupported(formatVersion),
            "Format should already have been validated.");

    ZL_FrameProperties info = {
        .hasContentChecksum =
                CCTX_getAppliedGParam(cctx, ZL_CParam_contentChecksum)
                != ZL_TernaryParam_disable,
        .hasCompressedChecksum =
                CCTX_getAppliedGParam(cctx, ZL_CParam_compressedChecksum)
                != ZL_TernaryParam_disable,
    };
    return EFH_writeChunkHeader(dst, dstCapacity, &info, gi, formatVersion);
}

/**
 * @return amount of data written into dst, or an error
 */
ZL_Report
CCTX_flushChunk(ZL_CCtx* cctx, const ZL_Data* inputs[], size_t nbInputs)
{
    ZL_DLOG(BLOCK, "CCTX_flushChunk (%zu inputs)", nbInputs);
    ZL_RESULT_DECLARE_SCOPE_REPORT(cctx);

    GraphInfo gi;
    ZL_ERR_IF_ERR(CCTX_getFinalGraph(cctx, &gi));

    // Write chunk header
    void* const dst             = cctx->dstBuffer;
    size_t const capacity       = cctx->dstCapacity;
    size_t const startFrameSize = cctx->currentFrameSize;
    size_t frameSize            = startFrameSize;
    ZL_ASSERT_LE(frameSize, capacity);

    {
        ZL_TRY_LET(
                size_t,
                chhSize,
                CCTX_writeChunkHeader(
                        cctx,
                        (char*)dst + startFrameSize,
                        capacity - startFrameSize,
                        &gi));
        ZL_LOG(SEQ,
               "wrote %zu chunk header bytes into buffer of capacity %zu",
               chhSize,
               capacity - startFrameSize);
        ZL_ASSERT_LE(chhSize, capacity - startFrameSize);
        frameSize += chhSize;
    }

    // Copy final buffers(s)
    size_t const nbStoredBuffs = gi.nbStoredBuffs;
    for (size_t n = 0; n < nbStoredBuffs; n++) {
        size_t const lbsize = gi.storedBuffs[n].size;
        ZL_DLOG(FRAME, "writing buffer %zu of size %zu bytes", n, lbsize);
        ZL_ASSERT_LE(frameSize, capacity);
        ZL_ERR_IF_GT(lbsize, capacity - frameSize, dstCapacity_tooSmall);
        if (lbsize) { // no need to copy when size==0, allows NULL src ptrs
            ZL_ASSERT_NN(gi.storedBuffs[n].start);
            memcpy((char*)dst + frameSize, gi.storedBuffs[n].start, lbsize);
        }
        frameSize += lbsize;
    }

    // Block footer
    // Append block content checksum
    if (CCTX_getAppliedGParam(cctx, ZL_CParam_contentChecksum)
        != ZL_TernaryParam_disable) {
        ZL_ASSERT_EQ(
                CCTX_getAppliedGParam(cctx, ZL_CParam_contentChecksum),
                ZL_TernaryParam_enable);
        ZL_RET_R_IF_LT(dstCapacity_tooSmall, capacity - frameSize, 4);
        uint32_t const formatVersion =
                (uint32_t)CCTX_getAppliedGParam(cctx, ZL_CParam_formatVersion);
        ZL_TRY_LET(
                size_t,
                hashT,
                STREAM_hashLastCommit_xxh3low32(
                        inputs, nbInputs, formatVersion));
        ZL_writeCE32((char*)dst + frameSize, (uint32_t)hashT);
        ZL_DLOG(SEQ, "chunk content checksum: %08X", (uint32_t)hashT);
        frameSize += 4;
    }

    // Append block compressed checksum
    if (CCTX_getAppliedGParam(cctx, ZL_CParam_compressedChecksum)
        != ZL_TernaryParam_disable) {
        ZL_ASSERT_EQ(
                CCTX_getAppliedGParam(cctx, ZL_CParam_compressedChecksum),
                ZL_TernaryParam_enable);
        ZL_RET_R_IF_LT(dstCapacity_tooSmall, capacity - frameSize, 4);
        size_t startHashPosition = startFrameSize;
        if (CCTX_getAppliedGParam(cctx, ZL_CParam_formatVersion)
            < ZL_CHUNK_VERSION_MIN) {
            /* versions < ZL_CHUNK_VERSION_MIN checksum the entire frame */
            startHashPosition = 0;
        }
        ZL_ASSERT_LE(startHashPosition, frameSize);
        ZL_DLOG(SEQ,
                "compressed checksum from pos %zu to %zu",
                startHashPosition,
                frameSize);
        uint32_t const hash = (uint32_t)XXH3_64bits(
                (const char*)dst + startHashPosition,
                frameSize - startHashPosition);
        ZL_writeCE32((char*)dst + frameSize, hash);
        ZL_DLOG(SEQ, "chunk compressed checksum: %08X", hash);
        frameSize += 4;
    }

    // Update dest buffer info
    cctx->currentFrameSize = frameSize;

    return ZL_returnValue(frameSize - startFrameSize);
}

ZL_Report CCTX_getFinalGraph(ZL_CCtx* cctx, GraphInfo* gip)
{
    ZL_ASSERT_NN(cctx);
    unsigned const formatVersion =
            (unsigned)CCTX_getAppliedGParam(cctx, ZL_CParam_formatVersion);
    unsigned const nbTransforms = (unsigned)RTGM_getNbNodes(&cctx->rtgraph);
    unsigned const nbStreamsMax = (unsigned)RTGM_getNbStreams(&cctx->rtgraph)
            + 1; // Reserve one extra slot for transforms' private headers
    unsigned const nbInputs = cctx->nbInputs;
    ZL_DLOG(FRAME,
            "CCTX_getFinalGraph (nbInputs=%u, nbNodes=%u)",
            nbInputs,
            nbTransforms);
    ZL_ASSERT_NN(gip);

    // Check format limitations
    ZL_RET_R_IF_GE(
            formatVersion_unsupported,
            nbTransforms,
            ZL_runtimeNodeLimit(formatVersion));
    ZL_RET_R_IF_GE(
            formatVersion_unsupported,
            nbStreamsMax,
            ZL_runtimeStreamLimit(formatVersion));
    ZL_RET_R_IF_GT(
            formatVersion_unsupported,
            nbInputs,
            ZL_runtimeInputLimit(formatVersion));

    // Allocation
    ALLOC_ARENA_MALLOC_CHECKED(
            PublicTransformInfo, trInfo, nbTransforms, cctx->chunkArena);
    ALLOC_ARENA_MALLOC_CHECKED(
            size_t, trHSizes, nbTransforms, cctx->chunkArena);
    ALLOC_ARENA_MALLOC_CHECKED(size_t, nbVOs, nbTransforms, cctx->chunkArena);
    ALLOC_ARENA_MALLOC_CHECKED(
            size_t, nbTrInputs, nbTransforms, cctx->chunkArena);
    ALLOC_ARENA_CALLOC_CHECKED(
            ZL_RBuffer, buffs, nbStreamsMax, cctx->chunkArena);
    ALLOC_ARENA_MALLOC_CHECKED(
            InputDesc, inputDescs, nbInputs, cctx->chunkArena);

    gip->trInfo      = trInfo;
    gip->trHSizes    = trHSizes;
    gip->nbVOs       = nbVOs;
    gip->nbTrInputs  = nbTrInputs;
    gip->storedBuffs = buffs;
    gip->inputDescs  = inputDescs;

    gip->nbSessionInputs = cctx->nbInputs;
    for (size_t n = 0; n < nbInputs; n++) {
        inputDescs[n].byteSize = ZL_Input_contentSize(cctx->inputs[n]);
        inputDescs[n].type     = ZL_Input_type(cctx->inputs[n]);
    }

    // We list transforms in reverse graph order
    // corresponding to the decoding order.
    // @note this is trivially correct, as all streams are consumed.
    // @note this order might be altered in the future.
    gip->nbTransforms  = nbTransforms;
    size_t nbDistances = 0;
    for (unsigned n = 0; n < nbTransforms; n++) {
        const RTNodeID rtnid     = { nbTransforms - 1 - n };
        const CNode* const cnode = RTGM_getCNode(&cctx->rtgraph, rtnid);
        trInfo[n]                = CNODE_getTransformID(cnode);
        const NodeHeaderSegment nhs =
                RTGM_nodeHeaderSegment(&cctx->rtgraph, rtnid);
        trHSizes[n] = nhs.len;
        ZL_RET_R_IF_LT(
                corruption,
                RTGM_getNbOutStreams(&cctx->rtgraph, rtnid),
                CNODE_getNbOut1s(cnode));
        nbVOs[n] = RTGM_getNbOutStreams(&cctx->rtgraph, rtnid)
                - CNODE_getNbOut1s(cnode);
        nbTrInputs[n] = RTGM_getNbInStreams(&cctx->rtgraph, rtnid);
        nbDistances += nbTrInputs[n];
        ZL_DLOG(BLOCK,
                "CCTX_getFinalGraph: stage %u uses Transform ID %u ",
                n,
                gip->trInfo[n].trid);
        // Copy header into final Transform's Header stream
        // in the order they will be consumed by the decoder.
        ZL_TRY_LET_T(
                ZL_RBuffer,
                buffer_slice,
                ZL_RBuffer_slice(
                        ZL_RBuffer_fromVector(
                                &cctx->trHeaders.stagingHeaderStream),
                        nhs.startPos,
                        nhs.len));
        ZL_RET_R_IF_ERR(
                CCTX_TransformHeaders_send(&cctx->trHeaders, buffer_slice));
    }
    ZL_ASSERT_GE(nbDistances, nbTransforms);

    ALLOC_ARENA_MALLOC_CHECKED(
            uint32_t, distances, nbDistances, cctx->chunkArena);
    gip->distances = distances;
    for (unsigned n = 0, d = 0; n < nbTransforms; n++) {
        RTNodeID const rtnid = { nbTransforms - 1 - n };
        for (int i = 0; i < (int)nbTrInputs[n]; i++) {
            distances[d++] = RTGM_getInputDistance(&cctx->rtgraph, rtnid, i);
        }
        if (n == nbTransforms - 1) {
            // All distances should be set at this point
            ZL_ASSERT_EQ(d, nbDistances);
        }
    }
    gip->nbDistances = nbDistances;
    // There may be unsent headers in the stream because a transform
    // was run, but then it failed, or we otherwise decided to not use
    // the output of that transform. In which case we have stored a header
    // but it won't be present in the final stream.
    ZL_ASSERT_LE(
            VECTOR_SIZE(cctx->trHeaders.sentHeaderStream),
            VECTOR_SIZE(cctx->trHeaders.stagingHeaderStream));

    // List Stored buffers
    size_t const nbBuffsMax = nbStreamsMax;
    ZL_TRY_LET_R(nbBuffs, CCTX_listBuffersToStore(cctx, buffs, nbBuffsMax));
    ZL_ASSERT_LE(nbBuffs, nbBuffsMax);
    gip->nbStoredBuffs = nbBuffs;

    return ZL_returnSuccess();
}

ZL_CCtx* CCTX_createDerivedCCtx(const ZL_CCtx* originalCCtx)
{
    ZL_CCtx* const cctx = CCTX_create();
    if (cctx == NULL)
        return NULL;
    if (ZL_isError(ZL_CCtx_refCompressor(cctx, originalCCtx->cgraph))) {
        ZL_CCtx_free(cctx);
        return NULL;
    }
    GCParams_copy(&cctx->requestedGCParams, &originalCCtx->requestedGCParams);
    return cctx;
}

/*   Accessors   */

const ZL_Compressor* CCTX_getCGraph(const ZL_CCtx* cctx)
{
    return cctx->cgraph;
}

const RTGraph* CCTX_getRTGraph(const ZL_CCtx* cctx)
{
    return &cctx->rtgraph;
}

bool CCTX_isNodeSupported(const ZL_CCtx* cctx, ZL_NodeID nodeid)
{
    if (!ZL_NodeID_isValid(nodeid))
        return false;
    uint32_t const formatVersion =
            (uint32_t)CCTX_getAppliedGParam(cctx, ZL_CParam_formatVersion);
    ZL_ASSERT_NE(formatVersion, 0, "format version is validated to be set");
    const CNode* const cnode   = CGRAPH_getCNode(cctx->cgraph, nodeid);
    const CNODE_FormatInfo nfi = CNODE_getFormatInfo(cnode);
    if (formatVersion < nfi.minFormatVersion) {
        return false;
    }
    if (formatVersion > nfi.maxFormatVersion) {
        return false;
    }
    return true;
}

ZL_CONST_FN
ZL_OperationContext* ZL_CCtx_getOperationContext(ZL_CCtx* cctx)
{
    if (cctx == NULL) {
        return NULL;
    }
    return &cctx->opCtx;
}

const char* ZL_CCtx_getErrorContextString(const ZL_CCtx* cctx, ZL_Report report)
{
    if (!ZL_isError(report)) {
        return NULL;
    }
    return ZL_OC_getErrorContextString(&cctx->opCtx, ZL_RES_error(report));
}

const char* ZL_CCtx_getErrorContextString_fromError(
        const ZL_CCtx* cctx,
        ZL_Error error)
{
    if (!ZL_E_isError(error)) {
        return NULL;
    }
    return ZL_OC_getErrorContextString(&cctx->opCtx, error);
}

ZL_Error_Array ZL_CCtx_getWarnings(ZL_CCtx const* cctx)
{
    return ZL_OC_getWarnings(&cctx->opCtx);
}

size_t CCTX_streamMemory(ZL_CCtx const* cctx)
{
    return RTGM_streamMemory(&cctx->rtgraph);
}

static ZL_RESULT_OF(ZL_GraphPerformance) CCTX_tryGraphInternal(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const ZL_Input* inputs[],
        size_t numInputs,
        ZL_GraphID graph,
        const ZL_RuntimeGraphParameters* params)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphPerformance, NULL);

    // We don't want to checksums in tryGraph
    ZL_ERR_IF_ERR(ZL_CCtx_setParameter(
            cctx, ZL_CParam_contentChecksum, ZL_TernaryParam_disable));
    ZL_ERR_IF_ERR(ZL_CCtx_setParameter(
            cctx, ZL_CParam_compressedChecksum, ZL_TernaryParam_disable));

    // Set the specific start graph with parameters set

    ZL_ASSERT(CCTX_isGraphSet(cctx)); // We only support cgraphs at the moment
    ZL_ERR_IF_ERR(ZL_CCtx_selectStartingGraphID(cctx, NULL, graph, params));

    ZL_TRY_LET(
            size_t,
            compressedSize,
            ZL_CCtx_compressMultiTypedRef(
                    cctx, dst, dstCapacity, inputs, numInputs));

    return ZL_WRAP_VALUE((ZL_GraphPerformance){ compressedSize });
}

ZL_RESULT_OF(ZL_GraphPerformance)
CCTX_tryGraph(
        const ZL_CCtx* parentCCtx,
        const ZL_Input* inputs[],
        size_t numInputs,
        Arena* wkspArena,
        ZL_GraphID graph,
        const ZL_RuntimeGraphParameters* params)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphPerformance, NULL);
    ZL_ERR_IF_EQ(numInputs, 0, graph_invalidNumInputs);

    size_t totalInputSize = 0;
    for (size_t i = 0; i < numInputs; ++i) {
        totalInputSize += ZL_Input_contentSize(inputs[i]);
        if (ZL_Input_type(inputs[i]) == ZL_Type_string) {
            totalInputSize += ZL_Input_numElts(inputs[i]) * sizeof(uint32_t);
        }
    }
    const size_t dstCapacity = ZL_compressBound(totalInputSize);
    void* const dst          = ALLOC_Arena_malloc(wkspArena, dstCapacity);
    ZL_ERR_IF_NULL(dst, allocation);

    ZL_CCtx* cctx = CCTX_createDerivedCCtx(parentCCtx);
    ZL_ERR_IF_NULL(cctx, allocation);

    ZL_RESULT_OF(ZL_GraphPerformance)
    result = CCTX_tryGraphInternal(
            cctx, dst, dstCapacity, inputs, numInputs, graph, params);

    CCTX_free(cctx);

    return result;
}

ZL_Report
CCTX_setHeaderComment(ZL_CCtx* cctx, const void* comment, size_t commentSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(cctx);
    ZL_ASSERT_NN(cctx);
    if (commentSize == 0) {
        cctx->comment.size = 0;
        return ZL_returnSuccess();
    }
    void* buff = ALLOC_Arena_malloc(cctx->sessionArena, commentSize);
    ZL_ERR_IF_NULL(buff, allocation);
    cctx->comment.size = commentSize;
    memcpy(buff, comment, commentSize);
    cctx->comment.data = buff;
    return ZL_returnSuccess();
}

ZL_Comment CCTX_getHeaderComment(const ZL_CCtx* cctx)
{
    return cctx->comment;
}
