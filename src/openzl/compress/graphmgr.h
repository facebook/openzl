// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_GRAPHMGR_H
#define ZSTRONG_COMPRESS_GRAPHMGR_H

#include "openzl/compress/cgraph.h" // GraphType_e
#include "openzl/compress/nodemgr.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_opaque_types.h" // ZL_NodeID, ZL_GraphID
#include "openzl/zl_reflection.h"
#include "openzl/zl_selector.h"

ZL_BEGIN_C_DECLS

typedef struct GraphsMgr_s GraphsMgr;
// note: may need an update to support custom allocator
GraphsMgr* GM_create(const Nodes_manager* nmgr);
void GM_free(GraphsMgr* gm);

/*   registration actions   */

ZL_RESULT_OF(ZL_GraphID)
GM_registerMultiInputGraph(GraphsMgr* gm, const ZL_FunctionGraphDesc* desc);

ZL_RESULT_OF(ZL_GraphID)
GM_registerTypedSelectorGraph(GraphsMgr* gm, const ZL_SelectorDesc* desc);

ZL_RESULT_OF(ZL_GraphID)
GM_registerStaticGraph(GraphsMgr* gm, const ZL_StaticGraphDesc* desc);

ZL_RESULT_OF(ZL_GraphID)
GM_registerParameterizedGraph(
        GraphsMgr* gm,
        const ZL_ParameterizedGraphDesc* desc);

ZL_RESULT_OF(ZL_GraphID)
GM_registerSegmenter(GraphsMgr* gm, const ZL_SegmenterDesc* desc);

/*   accessors   */

/**
 * @brief Retrieves the graph ID of the most recently registered custom graph.
 *
 * This function returns the ID of the last graph that was registered with the
 * graph manager through any of the registration functions
 * (GM_registerMultiInputGraph, GM_registerTypedSelectorGraph,
 * GM_registerStaticGraph, or GM_registerParameterizedGraph).
 *
 * @param gm The graph manager instance. Must not be NULL.
 * @returns The ZL_GraphID of the last registered graph, or ZL_GRAPH_ILLEGAL if
 * no custom graphs have been registered yet.
 *
 * @note This function only considers custom graphs that have been registered,
 *       not standard graphs that are built into the system.
 * @note The returned ID corresponds to the graph that was added most recently
 * to the internal graph descriptor vector (gdv).
 */
ZL_GraphID GM_getLastRegisteredGraph(const GraphsMgr* gm);

ZL_GraphID GM_getGraphByName(const GraphsMgr* gm, const char* graph);

typedef struct {
    /// The original type of the graph
    ZL_GraphType graphType;
    /// For parameterized graphs, the id of the graph of which this is a
    /// modification.
    ZL_GraphID baseGraphID;
    ZL_Name name;
    const ZL_Type* inputTypeMasks;
    size_t nbInputs;
    int lastInputIsVariable;
    /// For static graphs: The successor graphs
    /// For other graphs: The custom graphs
    const ZL_GraphID* customGraphs;
    size_t nbCustomGraphs;
    /// For static graphs: The singular head node
    /// For selector graphs: Empty
    /// For other graphs: The custom nodes
    const ZL_NodeID* customNodes;
    size_t nbCustomNodes;
    ZL_LocalParams localParams;
} GM_GraphMetadata;

/**
 * @pre GM_isValidGraphID(gm, gid)
 * @returns The GM_GraphMetadata for the given graph.
 */
GM_GraphMetadata GM_getGraphMetadata(const GraphsMgr* gm, ZL_GraphID gid);

/**
 * @returns true if @p gid exists in the graph manager.
 */
bool GM_isValidGraphID(const GraphsMgr* gmgr, ZL_GraphID gid);

const char* GM_getGraphName(const GraphsMgr* gmgr, ZL_GraphID graphid);

/* note: gt_miGraph, gt_segmenter or gt_store (or invalid) */
GraphType_e GM_graphType(const GraphsMgr* gmgr, ZL_GraphID graphid);

/* note: returns 0 if invalid */
size_t GM_getGraphNbInputs(const GraphsMgr* gmgr, ZL_GraphID graphid);

/* Warning: only for Graphs with Single Input! */
ZL_Type GM_getGraphInput0Mask(const GraphsMgr* gmgr, ZL_GraphID graphid);

// Invoking this function is guaranteed to be successful
// if @graphid corresponds to a valid Graph,
// which can be checked with GM_graphType (@return == gt_miGraph).
// Otherwise it returns NULL.
const ZL_FunctionGraphDesc* GM_getMultiInputGraphDesc(
        const GraphsMgr* gm,
        ZL_GraphID graphid);

// Invoking this function is guaranteed to be successful
// if @graphid corresponds to a valid Segmenter,
// which can be checked with GM_graphType (@return == gt_segmenter).
// Otherwise it returns NULL.
const ZL_SegmenterDesc* GM_getSegmenterDesc(
        const GraphsMgr* compressor,
        ZL_GraphID graphid);

const void* GM_getPrivateParam(const GraphsMgr* gmgr, ZL_GraphID graphid);

/// @see ZL_Compressor_forEachGraph
ZL_Report GM_forEachGraph(
        const GraphsMgr* gmgr,
        ZL_Compressor_ForEachGraphCallback callback,
        void* opaque,
        const ZL_Compressor* compressor);

// Warning: This is part of experimental API for graph mutation on the
// compressor.
//
// Replaces all the parameters of the target graph with @p gp. If there is a
// cycle in the graph as a result of this operation, it is UB.
ZL_Report GM_overrideGraphParams(
        GraphsMgr* const gm,
        ZL_GraphID targetGraph,
        const ZL_GraphParameters* gp);

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_GRAPHMGR_H
