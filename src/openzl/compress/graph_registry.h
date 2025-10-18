// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_GRAPH_REGISTRY_H
#define ZSTRONG_COMPRESS_GRAPH_REGISTRY_H

#include "openzl/compress/name.h"
#include "openzl/compress/private_nodes.h" // ZL_PrivateStandardGraphID_end
#include "openzl/shared/portability.h"
#include "openzl/zl_graph_api.h"    // ZL_FunctionGraphDesc
#include "openzl/zl_opaque_types.h" // ZL_NodeID, ZL_GraphID, ZL_GraphIDList
#include "openzl/zl_reflection.h"
#include "openzl/zl_segmenter.h" // ZL_SegmenterDesc
#include "openzl/zl_selector.h"  // ZL_SelectorFn

ZL_BEGIN_C_DECLS

typedef enum {
    GR_illegal = 0,
    GR_store,
    GR_dynamicGraph,
    GR_segmenter
} GraphFunctionType_e;

typedef struct {
    union {
        ZL_FunctionGraphDesc migd;
        ZL_SegmenterDesc segDesc;
    };
    ZL_GraphType originalGraphType;
    const void* privateParam;
    /// Standard graphs leave this empty, all other graphs set this.
    /// When set ZL_Name_unique(&maybeName) == migd.name
    ZL_Name maybeName;
    /// In order for a compressor to be serializable, we must be able to
    /// reconstruct functionally identical copies of all the sub-graphs. Some
    /// graphs effectively exist a priori: standard graphs, obviously, as
    /// well as the graphs that result from registering a custom graph
    /// component. It's the engine's or the user's responsibility to make
    /// these graphs available under the same name on the new compressor.
    ///
    /// Some graphs are wholly serializable, such as graphs that are
    /// produced by composing an existing node on top of one or more existing
    /// graphs. We can just describe how to reconstruct them from those
    /// components.
    ///
    /// The final kind of graph though is produced by modifying an existing
    /// graph, changing its parameters, successors, or custom nodes. Graphs
    /// of this type must record what that base graph is, so that the
    /// serialization framework can recreate the graph by looking up that
    /// base graph and applying the same modifications to it.
    ///
    /// This field records that reference to the graph from which this graph
    /// was created. Set to ZL_GRAPH_ILLEGAL when there is no such graph.
    ZL_GraphID baseGraphID;
} Graph_Desc_internal;

typedef struct {
    GraphFunctionType_e type;
    Graph_Desc_internal gdi;
} InternalGraphDesc;

extern const InternalGraphDesc GR_standardGraphs[ZL_PrivateStandardGraphID_end];

/**
 * @returns 1 if @graphid corresponds to a Standard Graph
 */
int GR_isStandardGraph(ZL_GraphID gid);

/**
 * @returns The number of valid graph IDs, including ZL_StandardGraphID_store.
 * note: invoked from VersionTestInterfaceABI.cpp and fuzz_graph.cpp
 */
size_t GR_getNbStandardGraphs(void);

/**
 * Fills @graphs with all the valid graph IDs, including
 * ZL_StandardGraphID_store. note : this capability is for testing purposes.
 *
 * @pre: @graphsSize must be at least @GR_getNbStandardGraphs().
 */
void GR_getAllStandardGraphIDs(ZL_GraphID* graphs, size_t graphsSize);

typedef ZL_Report (*GR_StandardGraphsCallback)(
        void* opaque,
        ZL_GraphID graph,
        const InternalGraphDesc* desc);

/**
 * Calls @p cb on every standard graph, and short-circuits if it returns an
 * error.
 */
ZL_Report GR_forEachStandardGraph(GR_StandardGraphsCallback cb, void* opaque);

/**
 * Checks validity of the content of Standard Graphs array (debug mode)
 * Note: will intentionally crash if it detects an error
 */
void GR_validate(void);

/**
 * ***   Wrappers   ***
 */

// Wrapper for Static Graphs starting with a TypedTransform
// Note: GR_staticGraphWrapper only supports nbInputs==1
ZL_Report
GR_staticGraphWrapper(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs);

// Version for Static Graphs starting with a VO Transform
// Note: this Dynamic Graph wrapper requires a Private Parameter:
// unsigned* nbSingletons
// Note2: GR_VOGraphWrapper only supports nbInputs==1
ZL_Report GR_VOGraphWrapper(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs);

// Note: GR_selectorWrapper requires GR_SelectorFunction as Private Parameter
// Note2: GR_selectorWrapper only supports nbInputs==1
typedef struct {
    ZL_SelectorFn selector_f;
} GR_SelectorFunction;
ZL_Report
GR_selectorWrapper(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs);

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_GRAPH_REGISTRY_H
