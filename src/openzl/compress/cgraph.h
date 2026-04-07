// Copyright (c) Meta Platforms, Inc. and affiliates.

/* Internal API for cgraph */

#ifndef ZSTRONG_COMPRESS_CGRAPH_H
#define ZSTRONG_COMPRESS_CGRAPH_H

#include "openzl/common/wire_format.h"      // TransformID
#include "openzl/compress/cnode.h"          // CNode
#include "openzl/compress/compress_types.h" // NodeType_e, CNode, GraphID_List
#include "openzl/compress/gcparams.h"       // GCParams
#include "openzl/shared/portability.h"
#include "openzl/zl_compressor.h" // ZL_Compressor
#include "openzl/zl_graph_api.h"  // ZL_FunctionGraphDesc
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_segmenter.h"

ZL_BEGIN_C_DECLS

/* =====   General Accessors   ===== */

ZL_GraphID CGRAPH_getStartingGraphID(const ZL_Compressor* cgraph);

const GCParams* CGRAPH_getGCParams(const ZL_Compressor* cgraph);

/* =====   Accessors on GraphID   ===== */

/*
 * CGRAPH_checkGraphIDExists:
 * Checks if a GraphID represents an existing sub-graph in the CGraph.
 * Main usage is validation and range testing.
 */
bool CGRAPH_checkGraphIDExists(const ZL_Compressor* cgraph, ZL_GraphID graphid);

typedef enum { gt_illegal, gt_store, gt_miGraph, gt_segmenter } GraphType_e;

GraphType_e CGRAPH_graphType(const ZL_Compressor* cgraph, ZL_GraphID graphid);

const ZL_FunctionGraphDesc* CGRAPH_getMultiInputGraphDesc(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid);

const ZL_SegmenterDesc* CGRAPH_getSegmenterDesc(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid);

const void* CGRAPH_graphPrivateParam(
        const ZL_Compressor* cgraph,
        ZL_GraphID graphid);

/* =====   Accessors on NodeID   ===== */

/* Note: narrow contract:
 * invoking these accessor functions *must* be successful,
 * meaning @nodeid must be valid */

const CNode* CGRAPH_getCNode(const ZL_Compressor* cgraph, ZL_NodeID nodeid);

/* invoked from cgraph_validation.c */
NodeType_e CGRAPH_getNodeType(const ZL_Compressor* cgraph, ZL_NodeID nodeid);

/* =====   Private actions on Graph   ===== */

/* invoked from encode_splitByStruct_binding */
ZL_NodeID CGraph_registerStandardVOTransform(
        ZL_Compressor* cgraph,
        const ZL_VOEncoderDesc* votd,
        unsigned minFormatVersion,
        unsigned maxFormatVersion);

ZL_NodeID CGraph_registerStandardMITransform(
        ZL_Compressor* cgraph,
        const ZL_MIEncoderDesc* mitd,
        unsigned minFormatVersion,
        unsigned maxFormatVersion);

/* =====   Dict accessors   ===== */

/**
 * @returns the materialized dictionary object at position @p dictOffset
 * within the compressor's loaded bundle, or NULL if no bundle is loaded.
 * @pre dictOffset < bundle->numDicts
 */
const void* CGRAPH_getDictObj(const ZL_Compressor* cgraph, size_t dictOffset);

/* =====   Dict index resolution   ===== */

/**
 * For each CNode that declares a dictID, resolve its positional index within
 * the compressor's loaded bundle and write it into CNode.maybeDictIndex.
 * CNodes without a dictID keep maybeDictIndex == ZL_DICT_INDEX_NONE.
 * @returns success, or an error if a required dict is missing from the bundle.
 */
ZL_Report CGraph_resolveDictIndices(ZL_Compressor* cgraph);

/* =====   Private actions on Compressor   ===== */

/**
 * Warning: This is part of experimental API for compressor mutation.
 *
 * Requires that:
 * @p graph is a parameterized graph registered in @p compressor
 *
 * Replaces the parameters of @p graph with @p gp.
 * @note: This function does not validate there are no dependency cycles within
 * the compressor.
 */
ZL_Report ZL_Compressor_overrideGraphParams(
        ZL_Compressor* compressor,
        ZL_GraphID graph,
        const ZL_GraphParameters* gp);

/**
 * Look up a previously loaded dict by its ZL_DictID.
 * @param matDesc must match the materializer used when the dict was loaded.
 * @returns the dict, or NULL if no dict with this ID has been loaded.
 */
const ZL_Dict* CGRAPH_findDict(
        const ZL_Compressor* cgraph,
        const ZL_DictID* id,
        const ZL_MaterializerDesc2* matDesc);

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_CGRAPH_H
