// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graphmgr.h" // GraphsMgr interface and GM_* function declarations
#include "openzl/common/allocation.h" // ALLOC_Arena_malloc, ALLOC_HeapArena_create, arena memory management
#include "openzl/common/assertion.h" // ZL_ASSERT_* macros for runtime checks
#include "openzl/common/limits.h"    // ZL_ENCODER_GRAPH_LIMIT constant
#include "openzl/common/logging.h" // ZL_DLOG, STR_REPLACE_NULL for logging and debugging
#include "openzl/common/map.h" // ZL_DECLARE_PREDEF_MAP_TYPE, GraphMap for name-to-GraphID mapping
#include "openzl/common/opaque.h" // ZL_OpaquePtrRegistry for managing opaque pointers
#include "openzl/compress/cgraph.h" // CNODE_getName, CNode definitions, graph context functions
#include "openzl/compress/graph_registry.h" // ZL_PrivateStandardGraphID_end, GR_standardGraphs, InternalGraphDesc
#include "openzl/compress/implicit_conversion.h" // ICONV_isCompatible for type checking
#include "openzl/compress/localparams.h" // LP_transferLocalParams for parameter management
#include "openzl/compress/name.h" // ZL_Name_*, ZS2_Name_* for graph name handling
#include "openzl/shared/mem.h" // ZL_malloc, ZL_free, ZL_memcpy memory utilities
#include "openzl/shared/overflow.h" // ZL_overflowMulST for integer overflow checks
#include "openzl/zl_opaque_types.h" // Opaque type definitions used by the API
#include "openzl/zl_reflection.h" // ZL_MIGraphDesc and type reflection utilities

/* ===   State Management   === */

DECLARE_VECTOR_TYPE(Graph_Desc_internal)

ZL_DECLARE_PREDEF_MAP_TYPE(GraphMap, ZL_Name, ZL_GraphID);

struct GraphsMgr_s {
    VECTOR(Graph_Desc_internal) gdv;
    /// Contains a map from name -> graph for all standard & custom graphs
    GraphMap nameMap;
    Arena* allocator;
    const Nodes_manager* nmgr;
    ZL_OpaquePtrRegistry opaquePtrs;
    ZL_OperationContext* opCtx;
}; // note: typedef'd to GraphsMgr

static ZL_Report GM_fillStandardGraphsCallback(
        void* opaque,
        ZL_GraphID graph,
        const InternalGraphDesc* desc)
{
    GraphsMgr* gm      = opaque;
    const ZL_Name name = ZS2_Name_wrapStandard(desc->gdi.migd.name);
    GraphMap_Insert insert =
            GraphMap_insertVal(&gm->nameMap, (GraphMap_Entry){ name, graph });
    ZL_RET_R_IF(allocation, insert.badAlloc);
    ZL_ASSERT_EQ(
            insert.ptr->val.gid,
            graph.gid,
            "Two standard graphs share the name \"%s\"",
            ZL_Name_unique(&name));
    return ZL_returnSuccess();
}

static ZL_Report GM_fillStandardGraphs(GraphsMgr* gm)
{
    return GR_forEachStandardGraph(GM_fillStandardGraphsCallback, gm);
}

GraphsMgr* GM_create(const Nodes_manager* nmgr)
{
    GraphsMgr* const gm = ZL_calloc(sizeof(*gm));
    if (!gm)
        return NULL;
    ZL_OpaquePtrRegistry_init(&gm->opaquePtrs);
    gm->nmgr      = nmgr;
    gm->allocator = ALLOC_HeapArena_create();
    if (gm->allocator == NULL) {
        GM_free(gm);
        return NULL;
    }
    VECTOR_INIT(gm->gdv, ZL_ENCODER_GRAPH_LIMIT);
    gm->nameMap = GraphMap_create(ZL_ENCODER_GRAPH_LIMIT);
    if (ZL_isError(GM_fillStandardGraphs(gm))) {
        GM_free(gm);
        return NULL;
    }
    gm->opCtx = nmgr->opCtx;
    return gm;
}

void GM_free(GraphsMgr* gm)
{
    if (gm == NULL)
        return;
    ZL_OpaquePtrRegistry_destroy(&gm->opaquePtrs);
    VECTOR_DESTROY(gm->gdv);
    GraphMap_destroy(&gm->nameMap);
    ALLOC_Arena_freeArena(gm->allocator);
    ZL_free(gm);
}

/* ===   Indexing scheme   === */

// Split indexing
// Below ZL_PrivateStandardGraphID_end : standard graph
//       Note : for the time being, standard graphs can only have 1 output
// Above ZL_PrivateStandardGraphID_end : custom graph

static ZL_IDType GM_GraphID_to_lgid(ZL_GraphID gid)
{
    ZL_IDType const cid = gid.gid;
    ZL_ASSERT_GE(cid, ZL_PrivateStandardGraphID_end);
    return cid - ZL_PrivateStandardGraphID_end;
}

static ZL_GraphID GM_lgid_to_zgid(ZL_IDType lgid)
{
    return (ZL_GraphID){ lgid + ZL_PrivateStandardGraphID_end };
}

bool GM_isValidGraphID(const GraphsMgr* gm, ZL_GraphID gid)
{
    ZL_DLOG(SEQ, "GM_isValidGraphID(%u)", gid.gid);
    ZL_IDType const cid   = gid.gid;
    size_t const nbGraphs = VECTOR_SIZE(gm->gdv);
    return ZL_StandardGraphID_illegal < cid
            && cid < (ZL_PrivateStandardGraphID_end + nbGraphs);
}

/* ===   Registration   === */

#define GM_TRANSFER_ARRAY(_dgm, _arr, _size, _out)                      \
    do {                                                                \
        const void* _out_void;                                          \
        ZL_RET_R_IF_ERR(GM_transferBuffer(                              \
                (_dgm), (_arr), sizeof(*(_arr)), (_size), &_out_void)); \
        *(_out) = _out_void;                                            \
    } while (0)

// Forward declarations
static ZL_RESULT_OF(ZL_GraphID) GM_registerSegmenter_internal(
        GraphsMgr* gm,
        const ZL_SegmenterDesc* segDesc,
        ZL_GraphID originalGraphID,
        ZL_GraphType originalGraphType,
        const void* privateParam,
        size_t ppSize);

static ZL_Report GM_transferBuffer(
        GraphsMgr* gm,
        const void* buffer,
        size_t eltWidth,
        size_t nbElts,
        const void** out)
{
    *out = NULL;
    if (buffer == NULL)
        ZL_ASSERT_EQ(nbElts, 0);
    if (nbElts == 0) {
        return ZL_returnSuccess();
    }
    size_t nbBytes;
    if (ZL_overflowMulST(eltWidth, nbElts, &nbBytes)) {
        ZL_RET_R_ERR(
                allocation, "Integer overflow: %zu * %zu", eltWidth, nbElts);
    }
    void* const dst = ALLOC_Arena_malloc(gm->allocator, nbBytes);
    ZL_RET_R_IF_NULL(allocation, dst);
    ZL_memcpy(dst, buffer, nbBytes);
    *out = dst;
    return ZL_returnSuccess();
}

static ZL_Report GM_transferCustomGIDs(
        GraphsMgr* gm,
        const ZL_GraphID* gids,
        size_t nbGids,
        const ZL_GraphID** out)
{
    GM_TRANSFER_ARRAY(gm, gids, nbGids, out);
    return ZL_returnSuccess();
}

static ZL_Report GM_transferCustomNIDs(
        GraphsMgr* gm,
        const ZL_NodeID* nids,
        size_t nbNids,
        const ZL_NodeID** out)
{
    GM_TRANSFER_ARRAY(gm, nids, nbNids, out);
    return ZL_returnSuccess();
}

static ZL_Report GM_transferTypes(
        GraphsMgr* gm,
        const ZL_Type* types,
        size_t nbTypes,
        const ZL_Type** out)
{
    GM_TRANSFER_ARRAY(gm, types, nbTypes, out);
    return ZL_returnSuccess();
}

/* Note: @lp is updated to point at new memory location */
static ZL_Report GM_transferLocalParameters(GraphsMgr* gm, ZL_LocalParams* lp)
{
    return LP_transferLocalParams(gm->allocator, lp);
}

/// Finishes registering the graph
/// @note This must be a no-op if anything fails
static ZL_Report GM_finalizeGraphRegistration(
        GraphsMgr* gm,
        Graph_Desc_internal* gdi)
{
    const ZL_IDType lgid = (ZL_IDType)VECTOR_SIZE(gm->gdv);

    // Need to check the name before pushing into the vector
    ZL_Name name;
    ZL_RET_R_IF_ERR(ZL_Name_init(&name, gm->allocator, gdi->migd.name, lgid));

    // Update the name in the GDI
    gdi->migd.name = ZL_Name_unique(&name);
    gdi->maybeName = name;

    ZL_RET_R_IF_NOT(allocation, VECTOR_PUSHBACK(gm->gdv, *gdi));

    const ZL_GraphID gid = GM_lgid_to_zgid(lgid);
    GraphMap_Insert insert =
            GraphMap_insertVal(&gm->nameMap, (GraphMap_Entry){ name, gid });
    if (insert.badAlloc || !insert.inserted) {
        VECTOR_POPBACK(gm->gdv); // Rollback the state
        ZL_RET_R_IF(allocation, insert.badAlloc);
        ZL_ASSERT(name.isAnchor, "Non-anchor is guaranteed to be unique");
        ZL_RET_R_ERR(
                invalidName,
                "Graph anchor name \"%s\" is not unique!",
                ZL_Name_unique(&name));
    }

    return ZL_returnValue(lgid);
}

static ZL_RESULT_OF(ZL_GraphID) GM_registerInternalGraph(
        GraphsMgr* gm,
        const ZL_FunctionGraphDesc* migd,
        ZL_GraphID originalGraphID,
        ZL_GraphType originalGraphType,
        const void* privateParam,
        size_t ppSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, gm->opCtx);

    ZL_DLOG(BLOCK, "GM_registerInternalGraph");
    ZL_ASSERT_NN(gm);
    ZL_ASSERT_NN(migd);
    ZL_ASSERT_NN(migd->graph_f);
    ZL_ASSERT_NULL(
            migd->opaque.freeFn,
            "Must already be registered with ZL_OpaquePtrRegistry");
    if (privateParam == NULL)
        ZL_ASSERT_EQ(ppSize, 0);

    ZL_ERR_IF_GE(
            VECTOR_SIZE(gm->gdv),
            ZL_ENCODER_GRAPH_LIMIT,
            temporaryLibraryLimitation,
            "Too many graphs registered");

    // Validate custom graphs
    for (size_t i = 0; i < migd->nbCustomGraphs; ++i) {
        // TODO(T219759022): Should this be allowed?
        if (migd->customGraphs[i].gid == ZL_GRAPH_ILLEGAL.gid) {
            continue;
        }
        ZL_ERR_IF_NOT(
                GM_isValidGraphID(gm, migd->customGraphs[i]),
                graph_invalid,
                "Custom GraphID at idx=%zu is invalid!",
                i);
    }

    // Validate custom nodes
    // TODO(T219759022): Should ZL_NODE_ILLEGAL be allowed?
    // It currently is, because NM_getCNode() returns non-null.
    for (size_t i = 0; i < migd->nbCustomNodes; ++i) {
        const CNode* cnode = NM_getCNode(gm->nmgr, migd->customNodes[i]);
        ZL_ERR_IF_NULL(
                cnode,
                graph_invalid,
                "Custom NodeID at idx=%zu is invalid!",
                i);
    }

    Graph_Desc_internal gdi;
    gdi.baseGraphID       = originalGraphID;
    gdi.originalGraphType = originalGraphType;
    gdi.migd              = *migd;
    ZL_ERR_IF_ERR(GM_transferTypes(
            gm,
            migd->inputTypeMasks,
            migd->nbInputs,
            &gdi.migd.inputTypeMasks));
    ZL_ERR_IF_ERR(GM_transferCustomGIDs(
            gm,
            migd->customGraphs,
            migd->nbCustomGraphs,
            &gdi.migd.customGraphs));
    ZL_ERR_IF_ERR(GM_transferCustomNIDs(
            gm, migd->customNodes, migd->nbCustomNodes, &gdi.migd.customNodes));
    ZL_ERR_IF_ERR(GM_transferLocalParameters(gm, &gdi.migd.localParams));

    if (ppSize == 0) {
        // No need to transfer, just copy the pointer
        // We use this for graph duplication, because its already stable
        gdi.privateParam = privateParam;
    } else {
        ZL_ERR_IF_ERR(GM_transferBuffer(
                gm, privateParam, 1, ppSize, &gdi.privateParam));
    }

    ZL_TRY_LET_CONST(size_t, lgid, GM_finalizeGraphRegistration(gm, &gdi));
    ZL_DLOG(SEQ,
            "Completed Graph registration at local ID %zu (global:%zu)",
            lgid,
            GM_lgid_to_zgid((ZL_IDType)lgid));
    return ZL_WRAP_VALUE(GM_lgid_to_zgid((ZL_IDType)lgid));
}

ZL_RESULT_OF(ZL_GraphID)
GM_registerMultiInputGraph(GraphsMgr* gm, const ZL_FunctionGraphDesc* migd)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, gm->opCtx);
    ZL_ERR_IF_ERR(ZL_OpaquePtrRegistry_register(&gm->opaquePtrs, migd->opaque));

    ZL_FunctionGraphDesc clone = *migd;
    clone.opaque.freeFn        = NULL;
    return GM_registerInternalGraph(
            gm, &clone, ZL_GRAPH_ILLEGAL, ZL_GraphType_multiInput, NULL, 0);
}

ZL_RESULT_OF(ZL_GraphID)
GM_registerTypedSelectorGraph(GraphsMgr* gm, const ZL_SelectorDesc* tsd)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, gm->opCtx);
    ZL_ERR_IF_ERR(ZL_OpaquePtrRegistry_register(&gm->opaquePtrs, tsd->opaque));

    /* Type control */
    for (size_t n = 0; n < tsd->nbCustomGraphs; n++) {
        ZL_GraphID const successorID = tsd->customGraphs[n];

        ZL_ERR_IF_NE(
                GM_getGraphNbInputs(gm, successorID),
                1,
                graph_invalid,
                "Candidate Successor '%s' (%u) must have a single input (detected %u)",
                GM_getGraphName(gm, successorID),
                successorID.gid,
                GM_getGraphNbInputs(gm, successorID));

        ZL_Type const successorInputMask =
                GM_getGraphInput0Mask(gm, successorID);

        ZL_ERR_IF_NOT(
                ICONV_isCompatible(tsd->inStreamType, successorInputMask),
                graph_invalid,
                "Candidate Successor '%s' (%u) input mask (%x) is not compatible with Selector '%s' input mask (%x)",
                GM_getGraphName(gm, successorID),
                successorID.gid,
                successorInputMask,
                tsd->name,
                tsd->inStreamType);
    }

    // All checks completed
    GR_SelectorFunction const sfs = { tsd->selector_f };

    ZL_FunctionGraphDesc const migd = {
        .name           = tsd->name,
        .graph_f        = GR_selectorWrapper,
        .inputTypeMasks = (const ZL_Type[]){ tsd->inStreamType },
        .nbInputs       = 1,
        .customGraphs   = tsd->customGraphs,
        .nbCustomGraphs = tsd->nbCustomGraphs,
        .localParams    = tsd->localParams,
        .opaque         = { .ptr = tsd->opaque.ptr },
    };
    return GM_registerInternalGraph(
            gm,
            &migd,
            ZL_GRAPH_ILLEGAL,
            ZL_GraphType_selector,
            &sfs,
            sizeof(sfs));
}

ZL_RESULT_OF(ZL_GraphID)
GM_registerStaticGraph(GraphsMgr* gm, const ZL_StaticGraphDesc* sgDesc)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, gm->opCtx);

    ZL_ASSERT_NN(sgDesc);
    ZL_DLOG(BLOCK,
            "GM_registerStaticGraph '%s' (%zu successors)",
            STR_REPLACE_NULL(sgDesc->name),
            sgDesc->nbGids);

    // Start by validating that the registration order is valid
    ZL_ERR_IF_NOT(
            ZL_NodeID_isValid(sgDesc->headNodeid),
            graph_invalid,
            "the starting Node of the static Graph is not valid");

    const CNode* const cnode = NM_getCNode(gm->nmgr, sgDesc->headNodeid);
    ZL_ERR_IF_NULL(
            cnode, graph_invalid, "Bad NodeID %u", sgDesc->headNodeid.nid);

    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);
    const ZL_MIGraphDesc* const mitcDesc = &cnode->transformDesc.publicDesc.gd;
    const ZL_GraphID* const successors   = sgDesc->successor_gids;
    size_t const nbSuccessors            = sgDesc->nbGids;
    size_t const nbSingletons            = mitcDesc->nbSOs;
    size_t const nbVOs                   = mitcDesc->nbVOs;
    size_t const nbOutcomes              = nbSingletons + nbVOs;
    // Ensure that definition of Successors is valid
    ZL_ERR_IF_NE(
            nbOutcomes,
            nbSuccessors,
            graph_invalid,
            "nb of outcomes (%zu) is incorrect for node '%s' (%zu)",
            nbSuccessors,
            CNODE_getName(cnode),
            nbOutcomes);
    for (size_t n = 0; n < nbSuccessors; n++) {
        ZL_ERR_IF_NOT(
                ZL_GraphID_isValid(successors[n]),
                graph_invalid,
                "Successor %zu is illegal",
                n);
        ZL_ERR_IF_NE(
                GM_getGraphNbInputs(gm, successors[n]),
                1,
                graph_invalid,
                "Successor must have a single input (detected %u)",
                GM_getGraphNbInputs(gm, successors[n]));
        // Check type compatibility for each outcome
        ZL_Type const origType = n < nbSingletons
                ? mitcDesc->soTypes[n]
                : mitcDesc->voTypes[n - nbSingletons];
        ZL_Type const dstTypes = GM_getGraphInput0Mask(gm, successors[n]);
        ZL_ERR_IF_NOT(
                ICONV_isCompatible(origType, dstTypes),
                graph_invalid,
                "Creation of Static Graph '%s': "
                "the successor %zu of Node '%s', which is Graph `%s`(id:%u) "
                "requires an incompatible stream type (orig:%x != %x:dst)",
                STR_REPLACE_NULL(sgDesc->name),
                n,
                CNODE_getName(cnode),
                GM_getGraphName(gm, successors[n]),
                successors[n].gid,
                origType,
                dstTypes);
    }

    // All checks successful => now register
    ZL_FunctionGraphFn dg_f = nbVOs ? GR_VOGraphWrapper : GR_staticGraphWrapper;

    ZL_FunctionGraphDesc migd = {
        .name                = sgDesc->name,
        .graph_f             = dg_f,
        .inputTypeMasks      = mitcDesc->inputTypes,
        .nbInputs            = mitcDesc->nbInputs,
        .lastInputIsVariable = mitcDesc->lastInputIsVariable,
        .customNodes         = &sgDesc->headNodeid,
        .nbCustomNodes       = 1,
        .customGraphs        = successors,
        .nbCustomGraphs      = nbSuccessors,
        .localParams         = sgDesc->localParams
                        ? sgDesc->localParams[0]
                        : cnode->transformDesc.publicDesc.localParams,
    };
    unsigned const nsParam = (unsigned)nbSingletons;
    return GM_registerInternalGraph(
            gm,
            &migd,
            ZL_GRAPH_ILLEGAL,
            ZL_GraphType_static,
            &nsParam,
            sizeof(nsParam));
}

ZL_Report GM_overrideGraphParams(
        GraphsMgr* const gm,
        ZL_GraphID targetGraph,
        const ZL_GraphParameters* gp)
{
    ZL_RESULT_DECLARE_SCOPE(size_t, gm->opCtx);
    ZL_ASSERT_NN(gm);

    ZL_ERR_IF(
            GR_isStandardGraph(targetGraph),
            graph_invalid,
            "Cannot replace standard graph");

    ZL_IDType const lid = GM_GraphID_to_lgid(targetGraph);
    ZL_ERR_IF_GE(lid, VECTOR_SIZE(gm->gdv), internalBuffer_tooSmall);
    // Check that the graphs is a parameterized graph
    ZL_ERR_IF_NE(
            VECTOR_AT(gm->gdv, lid).originalGraphType,
            ZL_GraphType_parameterized,
            graph_invalid);
    ZL_FunctionGraphDesc* const migd = &VECTOR_AT(gm->gdv, lid).migd;

    // Validate custom graphs
    for (size_t i = 0; i < gp->nbCustomGraphs; ++i) {
        // TODO(T219759022): Should this be allowed?
        if (gp->customGraphs[i].gid == ZL_GRAPH_ILLEGAL.gid) {
            continue;
        }
        ZL_ERR_IF_NOT(
                GM_isValidGraphID(gm, gp->customGraphs[i]),
                graph_invalid,
                "Custom GraphID at idx=%zu is invalid!",
                i);
    }

    // Validate custom nodes
    // TODO(T219759022): Should ZL_NODE_ILLEGAL be allowed?
    // It currently is, because NM_getCNode() returns non-null.
    for (size_t i = 0; i < gp->nbCustomNodes; ++i) {
        const CNode* cnode = NM_getCNode(gm->nmgr, gp->customNodes[i]);
        ZL_ERR_IF_NULL(
                cnode,
                graph_invalid,
                "Custom NodeID at idx=%zu is invalid!",
                i);
    }

    if (gp->nbCustomGraphs > 0) {
        ZL_ERR_IF_ERR(GM_transferCustomGIDs(
                gm, gp->customGraphs, gp->nbCustomGraphs, &migd->customGraphs));
        migd->nbCustomGraphs = gp->nbCustomGraphs;
    }
    if (gp->nbCustomNodes > 0) {
        ZL_ERR_IF_ERR(GM_transferCustomNIDs(
                gm, gp->customNodes, gp->nbCustomNodes, &migd->customNodes));
        migd->nbCustomNodes = gp->nbCustomNodes;
    }
    if (gp->localParams) {
        migd->localParams = *gp->localParams;
        ZL_ERR_IF_ERR(GM_transferLocalParameters(gm, &migd->localParams));
    }
    if (gp->name) {
        ZL_ERR(parameter_invalid, "Cannot replace the name of a graph");
    }
    return ZL_returnSuccess();
}

ZL_RESULT_OF(ZL_GraphID)
GM_registerParameterizedGraph(
        GraphsMgr* gm,
        const ZL_ParameterizedGraphDesc* desc)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, gm->opCtx);
    ZL_ASSERT_NN(gm);
    ZL_ASSERT_NN(desc);
    ZL_DLOG(SEQ,
            "GM_registerParameterizedGraph (name=%s)",
            STR_REPLACE_NULL(desc->name));

    // Check if the base graph is a segmenter and handle it separately
    GM_GraphMetadata baseMeta = GM_getGraphMetadata(gm, desc->graph);
    if (baseMeta.graphType == ZL_GraphType_segmenter) {
        const ZL_SegmenterDesc* segDescPtr =
                GM_getSegmenterDesc(gm, desc->graph);
        ZL_ERR_IF_NULL(segDescPtr, graph_invalid);

        ZL_SegmenterDesc segDesc = *segDescPtr;

        if (desc->localParams) {
            segDesc.localParams = *desc->localParams;
        }
        if (desc->nbCustomGraphs > 0) {
            segDesc.customGraphs    = desc->customGraphs;
            segDesc.numCustomGraphs = desc->nbCustomGraphs;
        }
        if (desc->name != NULL) {
            segDesc.name = desc->name;
        } else {
            segDesc.name = ZL_Name_prefix(&baseMeta.name);
        }

        // Keep originalGraphType as segmenter, use baseGraphID to indicate
        // parameterization
        return GM_registerSegmenter_internal(
                gm,
                &segDesc,
                desc->graph,
                ZL_GraphType_segmenter,
                GM_getPrivateParam(gm, desc->graph),
                0 /* No need to transfer private param */);
    }

    const ZL_FunctionGraphDesc* miDescPtr =
            GM_getMultiInputGraphDesc(gm, desc->graph);
    ZL_ERR_IF_NULL(miDescPtr, graph_invalid);

    ZL_FunctionGraphDesc miDesc = *miDescPtr;

    if (desc->localParams) {
        miDesc.localParams = *desc->localParams;
    }
    if (desc->nbCustomGraphs > 0) {
        miDesc.customGraphs   = desc->customGraphs;
        miDesc.nbCustomGraphs = desc->nbCustomGraphs;
    }
    if (desc->nbCustomNodes > 0) {
        miDesc.customNodes   = desc->customNodes;
        miDesc.nbCustomNodes = desc->nbCustomNodes;
    }
    if (desc->name != NULL) {
        miDesc.name = desc->name;
    } else {
        // Use the name prefix rather than the unique name, because this graph
        // needs a new non-anchor name.
        ZL_Name name = GM_getGraphMetadata(gm, desc->graph).name;
        miDesc.name  = ZL_Name_prefix(&name);
    }

    return GM_registerInternalGraph(
            gm,
            &miDesc,
            desc->graph,
            ZL_GraphType_parameterized,
            GM_getPrivateParam(gm, desc->graph),
            0 /* No need to transfer private param */);
}

static ZL_RESULT_OF(ZL_GraphID) GM_registerSegmenter_internal(
        GraphsMgr* gm,
        const ZL_SegmenterDesc* segDesc,
        ZL_GraphID originalGraphID,
        ZL_GraphType originalGraphType,
        const void* privateParam,
        size_t ppSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, gm->opCtx);

    ZL_DLOG(BLOCK, "GM_registerInternalGraph");
    ZL_ASSERT_NN(gm);
    ZL_ASSERT_NN(segDesc);
    ZL_ASSERT_NN(segDesc->segmenterFn);
    ZL_ASSERT_NULL(
            segDesc->opaque.freeFn,
            "Must already be registered with ZL_OpaquePtrRegistry");
    if (privateParam == NULL)
        ZL_ASSERT_EQ(ppSize, 0);

    ZL_ERR_IF_GE(
            VECTOR_SIZE(gm->gdv),
            ZL_ENCODER_GRAPH_LIMIT,
            temporaryLibraryLimitation,
            "Too many graphs registered");

    // Validate custom graphs
    for (size_t i = 0; i < segDesc->numCustomGraphs; ++i) {
        // TODO(T219759022): Should this be allowed?
        if (segDesc->customGraphs[i].gid == ZL_GRAPH_ILLEGAL.gid) {
            continue;
        }
        ZL_ERR_IF_NOT(
                GM_isValidGraphID(gm, segDesc->customGraphs[i]),
                graph_invalid,
                "Custom GraphID at idx=%zu is invalid!",
                i);
    }

    Graph_Desc_internal gdi;
    gdi.baseGraphID       = originalGraphID;
    gdi.originalGraphType = originalGraphType;
    gdi.segDesc           = *segDesc;
    ZL_ERR_IF_ERR(GM_transferTypes(
            gm,
            segDesc->inputTypeMasks,
            segDesc->numInputs,
            &gdi.segDesc.inputTypeMasks));
    ZL_ERR_IF_ERR(GM_transferCustomGIDs(
            gm,
            segDesc->customGraphs,
            segDesc->numCustomGraphs,
            &gdi.segDesc.customGraphs));
    ZL_ERR_IF_ERR(GM_transferLocalParameters(gm, &gdi.segDesc.localParams));

    if (ppSize == 0) {
        // No need to transfer, just copy the pointer
        // We use this for graph duplication, because its already stable
        gdi.privateParam = privateParam;
    } else {
        ZL_ERR_IF_ERR(GM_transferBuffer(
                gm, privateParam, 1, ppSize, &gdi.privateParam));
    }

    ZL_TRY_LET_CONST(size_t, lgid, GM_finalizeGraphRegistration(gm, &gdi));
    ZL_DLOG(SEQ,
            "Completed Graph registration at local ID %zu (global:%zu)",
            lgid,
            GM_lgid_to_zgid((ZL_IDType)lgid));
    return ZL_WRAP_VALUE(GM_lgid_to_zgid((ZL_IDType)lgid));
}

ZL_RESULT_OF(ZL_GraphID)
GM_registerSegmenter(GraphsMgr* gm, const ZL_SegmenterDesc* desc)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, gm->opCtx);
    ZL_ERR_IF_ERR(ZL_OpaquePtrRegistry_register(&gm->opaquePtrs, desc->opaque));

    ZL_SegmenterDesc clone = *desc;
    clone.opaque.freeFn    = NULL;
    return GM_registerSegmenter_internal(
            gm, &clone, ZL_GRAPH_ILLEGAL, ZL_GraphType_segmenter, NULL, 0);
}

/* ===   Accessors   === */

ZL_GraphID GM_getLastRegisteredGraph(const GraphsMgr* gm)
{
    ZL_DLOG(FRAME,
            "GM_getLastRegisteredGraph (vector size=%zu)",
            VECTOR_SIZE(gm->gdv));
    ZL_ASSERT_NN(gm);
    if (VECTOR_SIZE(gm->gdv) == 0) {
        // Note(@Cyan): this scenario only happens when no custom graph has been
        // registered yet. Another option here could be to return the most
        // generic standard graph instead.
        return ZL_GRAPH_ILLEGAL;
    }
    // The last registered graph is the last element in the vector
    return GM_lgid_to_zgid((ZL_IDType)(VECTOR_SIZE(gm->gdv) - 1));
}

ZL_GraphID GM_getGraphByName(const GraphsMgr* gm, const char* graph)
{
    const ZL_Name key           = ZL_Name_wrapKey(graph);
    const GraphMap_Entry* entry = GraphMap_find(&gm->nameMap, &key);
    if (entry != NULL) {
        return entry->val;
    } else {
        return ZL_GRAPH_ILLEGAL;
    }
}

static GM_GraphMetadata GM_getSegmenterMetadata(
        const GraphsMgr* gm,
        ZL_GraphID gid)
{
    ZL_ASSERT(GM_isValidGraphID(gm, gid));
    GM_GraphMetadata meta;
    ZL_DLOG(SEQ, "GM_getSegmenterMetadata (graphid=%u)", gid.gid);

    // graphType
    if (!GR_isStandardGraph(gid)) {
        ZL_IDType const lgid = GM_GraphID_to_lgid(gid);
        ZL_ASSERT_EQ(
                VECTOR_AT(gm->gdv, lgid).originalGraphType,
                ZL_GraphType_segmenter);
    }
    meta.graphType = ZL_GraphType_segmenter;

    const ZL_SegmenterDesc* desc = GM_getSegmenterDesc(gm, gid);
    ZL_ASSERT_NN(desc);

    // baseGraphID
    if (!GR_isStandardGraph(gid)) {
        ZL_IDType const lgid = GM_GraphID_to_lgid(gid);
        meta.baseGraphID     = VECTOR_AT(gm->gdv, lgid).baseGraphID;
    } else {
        // this is not a parameterized graph, it's an original
        meta.baseGraphID = ZL_GRAPH_ILLEGAL;
    }

    // name
    ZL_ASSERT_NN(desc);
    if (GR_isStandardGraph(gid)) {
        meta.name = ZS2_Name_wrapStandard(desc->name);
    } else {
        ZL_IDType const lgid = GM_GraphID_to_lgid(gid);
        meta.name            = VECTOR_AT(gm->gdv, lgid).maybeName;
        ZL_ASSERT_EQ(
                strcmp(ZL_Name_unique(&meta.name), desc->name),
                0,
                "Name mismatch in %s",
                desc->name);
    }
    ZL_ASSERT(!ZL_Name_isEmpty(&meta.name));

    meta.inputTypeMasks      = desc->inputTypeMasks;
    meta.nbInputs            = desc->numInputs;
    meta.lastInputIsVariable = desc->lastInputIsVariable;
    meta.localParams         = desc->localParams;
    meta.customGraphs        = desc->customGraphs;
    meta.nbCustomGraphs      = desc->numCustomGraphs;
    // no custom Nodes for Segmenters
    meta.customNodes   = NULL;
    meta.nbCustomNodes = 0;

    return meta;
}

GM_GraphMetadata GM_getGraphMetadata(const GraphsMgr* gm, ZL_GraphID gid)
{
    ZL_ASSERT(GM_isValidGraphID(gm, gid));
    GM_GraphMetadata meta;
    ZL_DLOG(SEQ, "GM_getGraphMetadata (graphid=%u)", gid.gid);

    // graphType
    if (GR_isStandardGraph(gid)) {
        meta.graphType = ZL_GraphType_standard;
        if (GR_standardGraphs[gid.gid].type == GR_segmenter)
            meta.graphType = ZL_GraphType_segmenter;
    } else {
        ZL_IDType const lgid = GM_GraphID_to_lgid(gid);
        meta.graphType       = VECTOR_AT(gm->gdv, lgid).originalGraphType;
    }

    if (meta.graphType == ZL_GraphType_segmenter)
        return GM_getSegmenterMetadata(gm, gid);

    const ZL_FunctionGraphDesc* desc = GM_getMultiInputGraphDesc(gm, gid);

    // baseGraphID
    if (meta.graphType == ZL_GraphType_parameterized) {
        ZL_IDType const lgid = GM_GraphID_to_lgid(gid);
        meta.baseGraphID     = VECTOR_AT(gm->gdv, lgid).baseGraphID;
    } else {
        meta.baseGraphID = ZL_GRAPH_ILLEGAL;
    }

    // name
    ZL_ASSERT_NN(desc);
    if (GR_isStandardGraph(gid)) {
        meta.name = ZS2_Name_wrapStandard(desc->name);
    } else {
        ZL_IDType const lgid = GM_GraphID_to_lgid(gid);
        meta.name            = VECTOR_AT(gm->gdv, lgid).maybeName;
        ZL_ASSERT_EQ(
                strcmp(ZL_Name_unique(&meta.name), desc->name),
                0,
                "Name mismatch in %s",
                desc->name);
    }
    ZL_ASSERT(!ZL_Name_isEmpty(&meta.name));

    meta.inputTypeMasks      = desc->inputTypeMasks;
    meta.nbInputs            = desc->nbInputs;
    meta.lastInputIsVariable = desc->lastInputIsVariable;
    meta.localParams         = desc->localParams;
    if (meta.graphType != ZL_GraphType_standard) {
        meta.customGraphs   = desc->customGraphs;
        meta.nbCustomGraphs = desc->nbCustomGraphs;
        meta.customNodes    = desc->customNodes;
        meta.nbCustomNodes  = desc->nbCustomNodes;
    } else {
        meta.customGraphs   = NULL;
        meta.nbCustomGraphs = 0;
        meta.customNodes    = NULL;
        meta.nbCustomNodes  = 0;
    }

    if (meta.graphType == ZL_GraphType_standard) {
        ZL_ASSERT(
                !memcmp(&meta.localParams,
                        &(ZL_LocalParams){ 0 },
                        sizeof(ZL_LocalParams)));
    }
    if (meta.graphType == ZL_GraphType_selector) {
        ZL_ASSERT_EQ(meta.nbCustomNodes, 0);
    }
    if (meta.graphType == ZL_GraphType_static) {
        ZL_ASSERT_EQ(meta.nbCustomNodes, 1);
    }

    return meta;
}

const ZL_FunctionGraphDesc* GM_getMultiInputGraphDesc(
        const GraphsMgr* gm,
        ZL_GraphID graphid)
{
    ZL_IDType const ggid = graphid.gid;
    ZL_DLOG(BLOCK, "GM_getMultiInputGraphDesc (graphid=%u)", ggid);
    if (GR_isStandardGraph(graphid)) {
        switch (GR_standardGraphs[ggid].type) {
            case GR_store:
            case GR_dynamicGraph:
                return &GR_standardGraphs[ggid].gdi.migd;
            case GR_illegal:
            case GR_segmenter:
            default:
                return NULL;
        }
    }
    ZL_IDType const lgid = GM_GraphID_to_lgid(graphid);
    ZL_ASSERT_NN(gm);
    if (lgid >= VECTOR_SIZE(gm->gdv)) {
        ZL_DLOG(ERROR,
                "requested graphid=%u is invalid (too large, >= %zu max)",
                ggid,
                VECTOR_SIZE(gm->gdv));
        return NULL;
    }
    if (VECTOR_AT(gm->gdv, lgid).originalGraphType == ZL_GraphType_segmenter)
        return NULL;
    return &VECTOR_AT(gm->gdv, lgid).migd;
}

const ZL_SegmenterDesc* GM_getSegmenterDesc(
        const GraphsMgr* gm,
        ZL_GraphID graphid)
{
    ZL_IDType const ggid = graphid.gid;
    ZL_DLOG(BLOCK, "GM_getSelectorDesc (graphid=%u)", ggid);
    if (GR_isStandardGraph(graphid)) {
        if (GR_standardGraphs[graphid.gid].type != GR_segmenter) {
            return NULL;
        }
        return &GR_standardGraphs[graphid.gid].gdi.segDesc;
    }
    ZL_IDType const lgid = GM_GraphID_to_lgid(graphid);
    ZL_ASSERT_NN(gm);
    if (lgid >= VECTOR_SIZE(gm->gdv))
        return NULL;
    if (VECTOR_AT(gm->gdv, lgid).originalGraphType != ZL_GraphType_segmenter)
        return NULL;
    return &VECTOR_AT(gm->gdv, lgid).segDesc;
}

GraphType_e GM_graphType(const GraphsMgr* gm, ZL_GraphID graphid)
{
    if (GR_isStandardGraph(graphid)) {
        switch (GR_standardGraphs[graphid.gid].type) {
            case GR_store:
                return gt_store;
            case GR_dynamicGraph:
                return gt_miGraph;
            case GR_segmenter:
                return gt_segmenter;
            case GR_illegal:
            default:
                return gt_illegal;
        }
    }
    if (GM_isValidGraphID(gm, graphid)) {
        ZL_IDType const lgid = GM_GraphID_to_lgid(graphid);
        ZL_GraphType gt      = VECTOR_AT(gm->gdv, lgid).originalGraphType;
        if (gt == ZL_GraphType_segmenter)
            return gt_segmenter;
        return gt_miGraph;
    }
    return gt_illegal;
}

const char* GM_getGraphName(const GraphsMgr* gm, ZL_GraphID graphid)
{
    const ZL_Name name = GM_getGraphMetadata(gm, graphid).name;
    return ZL_Name_unique(&name);
}

size_t GM_getGraphNbInputs(const GraphsMgr* gm, ZL_GraphID graphid)
{
    return GM_getGraphMetadata(gm, graphid).nbInputs;
}

ZL_Type GM_getGraphInput0Mask(const GraphsMgr* gm, ZL_GraphID graphid)
{
    const GM_GraphMetadata meta = GM_getGraphMetadata(gm, graphid);
    ZL_ASSERT_EQ(meta.nbInputs, 1);
    return meta.inputTypeMasks[0];
}

const void* GM_getPrivateParam(const GraphsMgr* gm, ZL_GraphID graphid)
{
    if (GR_isStandardGraph(graphid)) {
        ZL_ASSERT(GR_isStandardGraph(graphid));
        ZL_ASSERT_EQ(GR_standardGraphs[graphid.gid].type, GR_dynamicGraph);
        return GR_standardGraphs[graphid.gid].gdi.privateParam;
    }
    ZL_ASSERT_NN(gm);
    ZL_ASSERT(GM_isValidGraphID(gm, graphid));
    ZL_IDType const lid = GM_GraphID_to_lgid(graphid);
    return VECTOR_AT(gm->gdv, lid).privateParam;
}

ZL_Report GM_forEachGraph(
        const GraphsMgr* gmgr,
        ZL_Compressor_ForEachGraphCallback callback,
        void* opaque,
        const ZL_Compressor* compressor)
{
    for (size_t i = 0; i < VECTOR_SIZE(gmgr->gdv); ++i) {
        const ZL_GraphID gid = GM_lgid_to_zgid((ZL_IDType)i);
        ZL_RET_R_IF_ERR(callback(opaque, compressor, gid));
    }
    return ZL_returnSuccess();
}
