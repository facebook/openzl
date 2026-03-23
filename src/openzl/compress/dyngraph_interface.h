// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_DYNGRAPH_INTERFACE_H
#define ZSTRONG_COMPRESS_DYNGRAPH_INTERFACE_H

#include "openzl/common/allocation.h" // Arena
#include "openzl/common/vector.h"     // VECTOR_*
#include "openzl/compress/rtgraphs.h" // RTStreamID
#include "openzl/shared/portability.h"
#include "openzl/zl_graph_api.h" // ZL_Graph
#include "openzl/zl_localParams.h"
#include "openzl/zl_opaque_types.h" // ZL_IDType

ZL_BEGIN_C_DECLS

/**
 * @brief Destination status for Edges within Graphs.
 *
 * This enum tracks the lifecycle state of Edges within a dynamic graph,
 * ensuring proper validation of the graph execution flow.
 */
typedef enum {
    sds_unassigned = 0,  /**< Stream doesn't have any successor set yet.
                              Unless permissive mode is set, this is an invalid
                              state on exiting graph execution. */
    sds_destSet_trigger, /**< Stream has a successor, instantiation will be
                              triggered on reaching this stream (first input).
                          */
    sds_destSet_follow,  /**< Stream has a successor, but doesn't trigger
                              successor's instantiation. Typically the 2+
                              input of a multi-input graph. */
    sds_processed        /**< Stream has been consumed by a transform.
                              It doesn't need a successor. */
} StreamDestStatus_e;

/**
 * @brief Context information for a stream within a dynamic graph.
 *
 * This structure tracks the runtime state of individual streams,
 * linking them to their runtime stream IDs and destination status.
 */
typedef struct {
    RTStreamID rtsid;            /**< Runtime stream identifier */
    StreamDestStatus_e dest_set; /**< Current destination status */
    size_t successionPos;        /**< Position in succession list
                                      (valid when dest_set == sds_destSet_trigger) */
} DG_StreamCtx;
DECLARE_VECTOR_TYPE(DG_StreamCtx)

/**
 * @brief Edge representation in the dynamic graph system.
 *
 * An edge represents a data stream connection between nodes or graphs.
 * It provides the interface for operations like running nodes, setting
 * destinations, and accessing stream data.
 */
struct ZL_Edge_s {
    ZL_Graph* gctx;     /**< Parent graph context */
    ZL_IDType scHandle; /**< Handle to DG_StreamCtx in streamCtxs vector */
};

/**
 * @brief Descriptor for destination graphs in multi-input scenarios.
 *
 * This structure describes how streams should be routed to destination
 * graphs, including parameter passing and input organization.
 */
typedef struct {
    ZL_GraphID destGid; /**< Destination graph ID */
    const ZL_RuntimeGraphParameters*
            rGraphParams; /**< Runtime parameters for the graph */
    size_t nbInputs;      /**< Number of input streams */
    size_t rtiStartIdx;   /**< Start index in rtsids vector for this graph's
                             inputs */
} DestGraphDesc;
DECLARE_VECTOR_TYPE(DestGraphDesc)
DECLARE_VECTOR_TYPE(RTStreamID)

/**
 * @brief Main graph context for dynamic graph execution.
 *
 * This structure maintains the complete state of a dynamic graph during
 * execution, including stream tracking, destination management, and
 * memory allocation contexts.
 */
struct ZL_Graph_s {
    ZL_CCtx* cctx;    /**< Parent compression context */
    RTGraph* rtgraph; /**< Runtime graph for querying stream IDs */
    const ZL_FunctionGraphDesc* dgd; /**< Graph descriptor */
    const void* privateParam;        /**< Private parameters for the graph */
    VECTOR(DG_StreamCtx)
    streamCtxs; /**< Stream contexts created by this graph */
    VECTOR(DestGraphDesc) dstGraphDescs; /**< Destination graph descriptors */
    VECTOR(RTStreamID)
    rtsids;           /**< Runtime stream IDs for destination routing */
    ZL_Report status; /**< Error status during graph execution */
    unsigned depth;   /**< Current graph execution depth */

    /** @name Memory Allocators
     *  Allocators with specified lifetime durations
     * @{
     */
    Arena* graphArena; /**< Graph-duration allocator */
    Arena* chunkArena; /**< to transfer runtime parameters between graphs */
    /** @} */
};

/* ===== Public Graph Management Functions ===== */

/**
 * @brief Destroy a graph context and free associated resources.
 * @param gctx Graph context to destroy
 */
void GCTX_destroy(ZL_Graph* gctx);

/**
 * @brief Initialize an input edge for a graph context.
 * @param sctx Edge to initialize (output parameter)
 * @param gctx Graph context
 * @param irtsid Runtime stream ID for the input
 * @return ZL_Report indicating success or failure
 */
ZL_Report SCTX_initInput(ZL_Edge* sctx, ZL_Graph* gctx, RTStreamID irtsid);

/**
 * @brief Destroy an edge context.
 * @param sctx Edge context to destroy
 */
void SCTX_destroy(ZL_Edge* sctx);

/* ===== Private Implementation Functions ===== */

/**
 * @brief Execute a multi-input graph with the provided inputs.
 *
 * This function is the core execution entry point for dynamic graphs,
 * calling the graph's function with the prepared input contexts.
 *
 * @param gctx Graph context
 * @param inputs Array of input edge contexts
 * @param nbInputs Number of input edges
 * @return ZL_Report indicating execution result
 */
ZL_Report
GCTX_runMultiInputGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs);

/**
 * @brief Get all local parameters associated with a graph.
 * @param gctx Graph context
 * @return Pointer to local parameters structure
 */
const ZL_LocalParams* GCTX_getAllLocalParams(const ZL_Graph* gctx);

/**
 * @brief Get opaque private parameter associated with a graph.
 * @param gctx Graph context
 * @return Pointer to private parameter
 * @note used by Graph wrappers (engine)
 */
const void* GCTX_getPrivateParam(ZL_Graph* gctx);

/**
 * @brief Get the outcome ID for a stream context.
 *
 * Maps from an edge's runtime stream ID to its outcome identifier,
 * which is used for result tracking and validation.
 *
 * @param sctx Edge context
 * @return Outcome ID for the stream
 */
ZL_IDType StreamCtx_getOutcomeID(const ZL_Edge* sctx);

/**
 * @brief Transfer runtime graph parameters to session-duration memory.
 *
 * Creates a deep copy of runtime graph parameters in the provided arena,
 * ensuring they remain valid for the duration of the compression session.
 *
 * @param arena Memory arena for allocation
 * @param rgp Runtime graph parameters to transfer
 * @return Pointer to transferred parameters, or NULL on failure
 */
ZL_RuntimeGraphParameters* ZL_transferRuntimeGraphParams(
        Arena* arena,
        const ZL_RuntimeGraphParameters* rgp);

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_DYNGRAPH_INTERFACE_H
