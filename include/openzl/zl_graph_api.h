// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_ZS2_DYNGRAPH_H
#define ZSTRONG_ZS2_DYNGRAPH_H

#include <stdbool.h> // bool
#include <stddef.h>  // size_t

#include "openzl/zl_common_types.h"
#include "openzl/zl_compress.h" // ZL_CParam
#include "openzl/zl_errors.h"   // ZL_Report
#include "openzl/zl_input.h"
#include "openzl/zl_localParams.h"  // ZL_LocalParams
#include "openzl/zl_materializer.h" // ZL_MaterializerDesc
#include "openzl/zl_opaque_types.h" // ZL_GraphID
#include "openzl/zl_portability.h"  // ZL_NOEXCEPT_FUNC_PTR

#if defined(__cplusplus)
extern "C" {
#endif

// --------------------------------------------
// Function Graph registration
// --------------------------------------------

/* Function Graphs are user defined functions executed at compression time
 * which decides how to compress Inputs.
 * Function Graphs consume their input(s) entirely.
 * Their internals are abstracted away from the caller, so they can change
 * anytime. Function Graphs are allowed to expose a custom set of parameters for
 * the caller to influence its behavior. Function Graphs can create and insert
 * new Nodes at runtime, and decide Successor Graphs for produced Edges, using
 * the API provided below.
 *
 * The last Input in the list can be marked as "Variable",
 * meaning it can be present any number of times, including 0.
 * All inputs before the last one are considered "singular",
 * meaning they must present, once each.
 * The actual nb of inputs is provided at runtime.
 * Every Input must be processed or receive a Successor for the Graph's
 * processing to be considered successful.
 * It's up to the caller (another Function Graph) to populate Inputs
 * correctly. As a consequence, FunctionGraph's Inputs should be well
 * documented in all cases.
 *
 * Note: Function Graph is (currently) the only way to deal with multiple
 * Inputs.
 */
typedef struct ZL_Graph_s ZL_Graph;
typedef struct ZL_Edge_s ZL_Edge;
typedef struct ZL_FunctionGraphDesc ZL_FunctionGraphDesc;
/**
 * The function signature for function graphs.
 *
 * @param graph The graph object containing the graph context
 * @param inputs The inputs passed into the function graph to compress
 * @param nbInputs The number of inputs in @p inputs
 */
typedef ZL_Report (*ZL_FunctionGraphFn)(
        ZL_Graph* graph,
        ZL_Edge* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR;

/* A validation function is optional.
 * It is invoked at registration time, to ensure that the Function Graph
 * descriptor is correctly setup (notably its parameters). If the validation
 * function fails (@return 0), the registration will fail, and return
 * ZS2_GRAPH_INVALID (can be checked with ZL_GraphID_isValid()). When no
 * validation function is provided, registration is always successful.
 */
typedef int (*ZL_FunctionGraphValidateFn)(
        const ZL_Compressor* compressor,
        const ZL_FunctionGraphDesc* dgd) ZL_NOEXCEPT_FUNC_PTR;

struct ZL_FunctionGraphDesc {
    const char* name; // optional
    ZL_FunctionGraphFn graph_f;
    ZL_FunctionGraphValidateFn validate_f; // optional
    const ZL_Type* inputTypeMasks;         // can support multiple Types
    size_t nbInputs;
    int lastInputIsVariable; // Last input can optionally be marked as variable,
                             // meaning it is allowed to be present multiple
                             // times (including 0).
    /* optional list of custom graphs, nodes and parameters
     * that the Graph function _may_ employ */
    const ZL_GraphID* customGraphs; // can be NULL when none employed
    size_t nbCustomGraphs;          // Must be zero when customGraphs==NULL
    const ZL_NodeID* customNodes;   // can be NULL when none employed
    size_t nbCustomNodes;           // Must be zero when customNodes==NULL
    ZL_LocalParams localParams;
    /**
     * Optional materializer descriptor for materialized local params.
     * If both materializeFn and dematerializeFn are non-null, the materializer
     * will be used to create materialized objects from local params.
     */
    ZL_MaterializerDesc materializer;
    /**
     * Optionally an opaque pointer that can be queried with
     * ZL_Graph_getOpaquePtr().
     * OpenZL unconditionally takes ownership of this pointer, even if
     * registration fails, and it lives for the lifetime of the compressor.
     */
    ZL_OpaquePtr opaque;
};

/**
 * Registers a function graph given the @p desc.
 *
 * @note This is a new variant of @ref ZL_Compressor_registerFunctionGraph that
 * reports errors using OpenZL's ZL_Report error system.
 *
 * @param desc The description of the graph, must be non-null.
 *
 * @return The new graph ID, or an error.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_registerFunctionGraph2(
        ZL_Compressor* compressor,
        const ZL_FunctionGraphDesc* dgd);

ZL_GraphID ZL_Compressor_registerFunctionGraph(
        ZL_Compressor* compressor,
        const ZL_FunctionGraphDesc* dgd);

/**
 * Registration might fail if the Descriptor is incorrectly filled,
 * Any further operation attempted with such a Graph will also fail.
 * Such an outcome can be tested with ZL_GraphID_isValid().
 * Note: this is mostly for debugging purposes,
 * once a Descriptor is valid, registration can be assumed to be successful.
 */
int ZL_GraphID_isValid(ZL_GraphID graphid);

// *************************************
// API for Function Graph
// *************************************

/* Accessors */
/* --------- */

// Access list of authorized custom GraphIDs and NodeIDs
ZL_GraphIDList ZL_Graph_getCustomGraphs(const ZL_Graph* gctx);
ZL_NodeIDList ZL_Graph_getCustomNodes(const ZL_Graph* gctx);

// Request parameters
/* Consultation request for Global parameters */
int ZL_Graph_getCParam(const ZL_Graph* gctx, ZL_CParam gparam);
/* Consultation requests for Local parameters */
ZL_IntParam ZL_Graph_getLocalIntParam(const ZL_Graph* gctx, int intParamId);
ZL_RefParam ZL_Graph_getLocalRefParam(const ZL_Graph* gctx, int refParamId);

/**
 * Determines whether @nodeid is supported given the applied global parameters
 * for the compression.
 * Notably the ZL_CParam_formatVersion parameter can determine if a node is
 * valid for the given encoding version.
 */
bool ZL_Graph_isNodeSupported(const ZL_Graph* gctx, ZL_NodeID nodeid);

const void* ZL_Graph_getOpaquePtr(const ZL_Graph* graph);

/**
 * @brief Query the current graph execution depth.
 *
 * Returns the depth at which the current graph is executing.
 * Depth 1 is the root graph; each successor level increments by 1.
 * This can be used to detect runaway graph growth.
 *
 * @param gctx  Graph context, must be non-NULL.
 * @return Current graph execution depth (>= 1).
 */
unsigned ZL_Graph_getDepth(const ZL_Graph* gctx);

/* access the content of an Edge */
const ZL_Input* ZL_Edge_getData(const ZL_Edge* sctx);

/**
 * Gets the error context for a given ZL_Report. This context is useful for
 * debugging and for submitting bug reports to Zstrong developers.
 *
 * @param report The report to get the error context for
 *
 * @returns A verbose error string containing context about the error that
 * occurred.
 *
 * @note: This string is stored within the @p graph and may only be valid for
 * the lifetime of the @p graph.
 */
const char* ZL_Graph_getErrorContextString(
        const ZL_Graph* graph,
        ZL_Report report);

/**
 * See ZL_Graph_getErrorContextString()
 *
 * @param error: The error to get the context for
 */
const char* ZL_Graph_getErrorContextString_fromError(
        const ZL_Graph* graph,
        ZL_Error error);

/* Actions */
/* ------- */

typedef struct ZL_GraphParameters_s ZL_RuntimeGraphParameters;

/* Scratch space allocation:
 * When the function graph function needs some temporary space for some
 * operation, it can requests such space from the Graph Engine. The Function
 * Graph can request multiple buffers of any size. Returned buffers are not
 * initialized, and cannot be freed individually. All scratch buffers are
 * automatically released at end of Graph's execution.
 * */
void* ZL_Graph_getScratchSpace(ZL_Graph* gctx, size_t size);

/**
 * A measurement of graph performance.
 * Currently this is compressed size, but it is expected to be expanded to
 * include speed.
 */
typedef struct {
    /// The compressed size of the graph on the given input(s)
    size_t compressedSize;
} ZL_GraphPerformance;
ZL_RESULT_DECLARE_TYPE(ZL_GraphPerformance);

/**
 * @brief Attempt compression using a graph and return the performance.
 *
 * This API allows the user to simulate the execution of a given @p graphID
 * on an input to measure its performance. This API is wasteful in CPU and
 * memory and should only be used when there is no better choice.
 *
 * @param input The input to try compress on
 * @param graphID The GraphID to use to compress the @p input
 * @param params The runtime parameters for the @p graphID, or `NULL` to not
 * parameterize the graph.
 *
 * @returns If the compression failed it returns a non-fatal error. Otherwise,
 * it returns the performance of the @p graphID on the @p input.
 */
ZL_RESULT_OF(ZL_GraphPerformance)
ZL_Graph_tryGraph(
        const ZL_Graph* gctx,
        const ZL_Input* input,
        ZL_GraphID graphID,
        const ZL_RuntimeGraphParameters* params);

/**
 * @brief Attempt compression using a graph and return the performance.
 *
 * The same as @ref ZL_Graph_tryGraph except it accepts multiple inputs.
 */
ZL_RESULT_OF(ZL_GraphPerformance)
ZL_Graph_tryMultiInputGraph(
        const ZL_Graph* gctx,
        const ZL_Input* inputs[],
        size_t numInputs,
        ZL_GraphID graphID,
        const ZL_RuntimeGraphParameters* params);

/* Run a Node,
 * and collect the produced Edges.
 * All produced Edges must be either processed or assigned a Successor.
 * A single "dangling" Edge is enough to qualify the entire DynGraph
 * processing as erroneous (unless permissive mode is set).
 * */
/* Note on API design:
 * This API introduces a ZL_RESULT_OF(T) as a public type
 * that users (implementers of Function Graph) *must* manipulate.
 * It essentially requires usage of macros such as ZL_TRY_LET_T() and
 * equivalent.
 * While *we* may be used to these (so far internal) macros, this is the first
 * time it's exposed to 3rd party users and represents a substantial pattern
 * shift, especially if it only happens for a single symbol.
 * If deemed problematic for API consistency, other potential alternatives
 * include:
 * - return a ZL_EdgeList, use .nbStreams==0 as an error signal
 *   + but it would make it impossible for transforms to produce zero Edge
 * - return a ZL_EdgeList, use .nbStreams==SOME_CONSTANT as an error signal
 * - Add a ZL_Report object inside the returned ZL_EdgeList
 */

typedef struct {
    union {
        ZL_Edge** edges;
        ZL_Edge** streams; // older name, for transition period only
    };
    union {
        size_t nbEdges;
        size_t nbStreams; // older name, for transition period only
    };
} ZL_EdgeList;
ZL_RESULT_DECLARE_TYPE(ZL_EdgeList);

/* invoke a transform operating on a single input */
ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runNode(ZL_Edge* input, ZL_NodeID nid);

/* run a Node with runtime-defined parameters */
ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runNode_withParams(
        ZL_Edge* input,
        ZL_NodeID nid,
        const ZL_LocalParams* localParams);

/* multi-inputs transforms */
ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runMultiInputNode(ZL_Edge* inputs[], size_t nbInputs, ZL_NodeID nid);

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runMultiInputNode_withParams(
        ZL_Edge* inputs[],
        size_t nbInputs,
        ZL_NodeID nid,
        const ZL_LocalParams* localParams);

/** @brief Sets the int metadata for the edge to @p mValue
 *
 * @param mId The identifier for the stream metadata on the edge to set metadata
 * on
 * @param mValue The value to set stream metadata
 */
ZL_Report ZL_Edge_setIntMetadata(ZL_Edge* edge, int mId, int mValue);

// Direct an Edge towards a Graph supporting only a single Input
ZL_Report ZL_Edge_setDestination(ZL_Edge* edge, ZL_GraphID gid);

/**
 * @brief Sets the destination of the provided edges to the provided graph ID,
 * overriding its behavior with the provided parameters.
 *
 * @param edges Array of edges to direct towards the successor graph.
 * @param nbInputs The number of edges in the provided array.
 * @param gid The ID of the successor graph.
 * @param rGraphParams The parameters to use for the successor graph. NULL means
 * don't override.
 */
ZL_Report ZL_Edge_setParameterizedDestination(
        ZL_Edge* edges[],
        size_t nbInputs,
        ZL_GraphID gid,
        const ZL_RuntimeGraphParameters* rGraphParams);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_DYNGRAPH_H
