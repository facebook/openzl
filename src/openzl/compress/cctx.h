// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_CCTX_H
#define ZSTRONG_COMPRESS_CCTX_H

#include "openzl/compress/encode_frameheader.h" // EFH_FrameInfo, GraphInfo
#include "openzl/compress/rtgraphs.h"           // RTNodeID
#include "openzl/shared/portability.h"
#include "openzl/zl_compress.h" // ZL_CCtx, ZL_GraphFn, ZL_Report

ZL_BEGIN_C_DECLS

/**
 * @brief Create a new compression context.
 *
 * This function allocates and initializes a new compression context that can be
 * used for multiple compression sessions. The context manages compression
 * graphs, runtime state, and memory allocation during compression operations.
 *
 * @return A pointer to the newly created compression context, or NULL if
 * allocation failed
 *
 * @note The caller must check the return value and handle allocation failure
 * appropriately.
 * @note The returned context must be freed using CCTX_free() to prevent memory
 * leaks.
 * @note The context is initially empty and requires a compression graph to be
 * set before use.
 *
 * @see CCTX_free for destroying the context
 * @see ZL_CCtx_refCompressor for setting a compression graph
 */
ZL_CCtx* CCTX_create(void);

/**
 * @brief Free a compression context and all associated resources.
 *
 * This function deallocates a compression context created by CCTX_create() or
 * CCTX_createDerivedCCtx(), releasing all memory used by the context including
 * any runtime state, buffers, and internal data structures.
 *
 * @param cctx The compression context to free (may be NULL, in which case this
 * is a no-op)
 *
 * @note It's safe to pass NULL to this function.
 *
 * @see CCTX_create for creating compression contexts
 * @see CCTX_createDerivedCCtx for creating derived contexts
 */
void CCTX_free(ZL_CCtx* cctx);

/**
 * @brief Create a derived compression context that shares resources with an
 * existing context.
 *
 * Creates a new compression context that references the compression graph and
 * global parameters from an existing context. The derived context should behave
 * identically to the original context when used for compression operations, but
 * maintains its own runtime state and can be used independently.
 *
 * @param originalCCtx The source compression context to derive from (must be
 * non-NULL)
 *
 * @return A pointer to the newly created derived context, or NULL if allocation
 * failed
 *
 * @note The derived context references the original context and must be
 * destroyed before the original context is freed.
 * @note The original context must remain valid for the entire lifetime of the
 * derived context.
 * @note This is useful for parallel compression operations that need to share
 * the same graph.
 * @note The derived context must still be freed using CCTX_free() when no
 * longer needed.
 * @note (@cyan): this is not a great design, and should ideally be removed
 * before release. It's used in only one place (Selector_tryGraph).
 *
 * @see CCTX_free for destroying derived contexts
 */
ZL_CCtx* CCTX_createDerivedCCtx(const ZL_CCtx* originalCCtx);

/**
 * @brief Function pointer type for Graph2 compression graph generation
 * functions.
 *
 * This function type is used to define custom graph generation functions that
 * can create compression graphs dynamically based on custom parameters.
 *
 * @param cgraph The compressor instance to configure with the generated graph
 * @param customParams Custom parameters passed to the graph generation function
 *
 * @return The ID of the generated compression graph, or an error code on
 * failure
 */
typedef ZL_GraphID (
        *ZL_Graph2Fn)(ZL_Compressor* cgraph, const void* customParams);

/**
 * @brief Descriptor structure for Graph2-based compression graph configuration.
 *
 * This structure packages a graph generation function with its associated
 * custom parameters, providing a complete specification for dynamic graph
 * creation.
 */
typedef struct {
    ZL_Graph2Fn f; /**< Function pointer to the graph generation function */
    const void* customParams; /**< Custom parameters to pass to the generation
                                 function */
} ZL_Graph2Desc;

/**
 * @brief Set the compression graph using a Graph2Desc descriptor.
 *
 * This function configures the compression context to use a specific
 * compression graph defined by a Graph2Desc structure. The descriptor contains
 * a function pointer and custom parameters that will be used to generate or
 * configure the compression graph.
 *
 * @param cctx The compression context to configure (must be non-NULL)
 * @param graphDesc Descriptor containing the graph function and custom
 * parameters
 *
 * @return ZL_SUCCESS on success, or an error code if the graph setup failed
 *
 * @note This function is exposed for unit testing in compress2.c
 * @note The operation can fail due to invalid parameters or graph construction
 * errors
 * @note Always check the return value with ZL_isError() before proceeding
 * @note The graph must be successfully set before compression operations can
 * begin
 *
 * @see ZL_Graph2Desc for the descriptor structure definition
 * @see ZL_isError for error checking
 */
ZL_Report CCTX_setLocalCGraph_usingGraph2Desc(
        ZL_CCtx* cctx,
        ZL_Graph2Desc graphDesc);

/**
 * @brief set @p cctx to be able to write Chunks into @p dst buffer.
 *
 * @note all parameters must be set and valid:
 *      @p dst must be allocated and non-NULL,
 *      @p writtenSize must be <= @p dstCapacity
 */
void CCTX_setDst(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        size_t writtenSize);

/**
 * @brief Finalize global parameter values for the current compression session.
 *
 * This function resolves the final values for all global compression parameters
 * by merging values from multiple sources in priority order. The parameter
 * resolution follows this precedence: context requested parameters override
 * compression graph defaults, which override system defaults.
 *
 * @param cctx The compression context to configure (must be non-NULL with a
 * graph set)
 *
 * @return ZL_SUCCESS on success, or an error code if parameter resolution
 * failed
 *
 * @note This is the first compression stage, called after the compression graph
 * is set.
 * @note Parameter priority order is: cctx.RequestedParams > CGraph.params >
 * default values
 * @note This function must be called before compression operations begin.
 * @note Parameter conflicts or invalid combinations will result in an error.
 *
 * @see CCTX_getAppliedGParam for retrieving finalized parameter values
 * @see ZL_CCtx_setParameter for setting requested parameters
 */
ZL_Report CCTX_setAppliedParameters(ZL_CCtx* cctx);

/**
 * @brief Get the finalized value of a soecific global compression parameter.
 *
 * This function retrieves the current value of a global compression parameter
 * after parameter resolution has been completed. The returned value reflects
 * the final parameter value that will be used during compression.
 *
 * @param cctx The compression context (must be non-NULL with parameters
 * applied)
 * @param gcparam The global compression parameter to query
 *
 * @return The current value of the specified parameter
 *
 * @note Parameters must be finalized with CCTX_setAppliedParameters() before
 * calling this.
 * @note The returned value reflects the resolved parameter after priority
 * merging.
 * @note This function is used to inspect the actual compression configuration.
 * @note (@cyan) What happens if the request fails, for example is `gcparam`
 * doesn't exist ? Following the code, it returns 0, but this is not obvious and
 * should be defined clearly.
 *
 * @see CCTX_setAppliedParameters for parameter finalization
 * @see ZL_CParam for available global compression parameters
 */
int CCTX_getAppliedGParam(const ZL_CCtx* cctx, ZL_CParam gcparam);

/**
 * @brief Add transform header data to the compression context.
 *
 * This function adds header information for a specific transform to the
 * compression context's header stream. Transform headers contain metadata and
 * configuration information that will be stored in the compressed output to
 * enable proper decompression.
 *
 * @param cctx The compression context (must be non-NULL)
 * @param rtnodeid The runtime node ID of the transform requiring header data
 * @param trh Buffer containing the transform header data to add
 *
 * @return ZL_SUCCESS on success, or an error code if the operation failed
 *
 * @note Only one header per transform is allowed; subsequent calls will fail.
 * @note Header data must be provided before the transform completes execution.
 * @note The header data will be included in the final compressed output.
 * @note This function is called by the engine's wrapper, not user code.
 *
 * @see ZL_RBuffer for the buffer structure definition
 * @see RTNodeID for runtime node identification
 */
ZL_Report CCTX_sendTrHeader(ZL_CCtx* cctx, RTNodeID rtnodeid, ZL_RBuffer trh);

/**
 * @brief Start the compression process with the provided input data.
 *
 * Initiates the compression process.
 * This is blocking call, it will return when compression is completed.
 *
 * @param cctx The compression context, with the following conditions:
 *   - must be non-NULL
 *   - a compressor is set
 * @param inputs Array of inputs to compress (must be non-NULL)
 * @param numInputs number of inputs (must match graph requirements)
 *
 * @return ZL_SUCCESS on successful compression, or an error code if:
 *     - No compressor is set
 *     - Input validation fails
 *     - Runtime environment setup fails
 *
 * @note A compressor must be set before calling this function
 * @note The number and types of inputs must match the graph's requirements
 *
 * @see ZL_CCtx_refCompressor for setting a compressor
 * @see ZL_Data for input stream structure
 */
ZL_Report
CCTX_startCompression(ZL_CCtx* cctx, const ZL_Data* inputs[], size_t numInputs);

/**
 * @brief Execute a transform node with specified parameters and track outputs.
 *
 * This function runs a specific transform node from the compression graph using
 * the provided input streams and optional local parameters. It creates and
 * tracks all output streams generated by the transform, returning the number of
 * outputs created and the runtime node ID for the executed transform.
 *
 * @param cctx The compression context (must be non-NULL)
 * @param rtnid Output parameter to receive the created runtime node ID
 * @param inputs Array of input data streams for the transform (may be NULL if
 * nbInputs is 0)
 * @param irtsids Array of input runtime stream IDs
 * @param nbInputs The number of input streams provided
 * @param nodeid The node ID of the transform to execute (must host a valid
 * transform)
 * @param lparams Optional local parameters for the transform (may be NULL)
 *
 * @return On success, returns the number of output streams created by the
 * transform. On error, returns an error code if the transform execution failed.
 *
 * @note The specified node ID must be a valid registered encoder
 * @note Local parameters override any default parameters defined in the
 * transform.
 * @note All output streams are automatically tracked by the compression
 * context.
 * @note The returned runtime node ID can be used to reference this transform
 * execution.
 *
 * @see ZL_NodeID for transform node identification
 * @see ZL_LocalParams for transform parameter structure
 * @see RTNodeID for runtime node identification
 */
ZL_Report CCTX_runNodeID_wParams(
        ZL_CCtx* cctx,
        RTNodeID* rtnid,
        const ZL_Data* inputs[],
        const RTStreamID irtsids[],
        size_t nbInputs,
        ZL_NodeID nodeid,
        const ZL_LocalParams* lparams);

/**
 * @brief Create a new serial stream and return a writable pointer to its
 * buffer.
 *
 * This function performs a combined operation: creates a new output stream
 * buffer for serial data, attaches it to the specified runtime node, and
 * returns a direct pointer to the writable buffer area. This provides efficient
 * access for transforms that need to write sequential data directly to memory.
 *
 * @param cctx The compression context (must be non-NULL)
 * @param rtnodeid The runtime node ID that will produce this stream
 * @param outStreamIndex The index of the output stream within the transform's
 * outputs (0-based)
 * @param eltWidth The width of each element in bytes (must be > 0)
 * @param eltCount The number of elements to allocate capacity for
 *
 * @return A pointer to the writable buffer area, or NULL if allocation failed
 *
 * @note This operation can fail due to memory allocation errors.
 * @note Memory alignment follows standard malloc() guarantees.
 * @note This interface is specifically designed for ZL_Type_serial streams
 * only.
 * @note The stream must be committed with the actual written size before
 * transform completion.
 *
 * @see CCTX_getNewStream for a more general stream creation interface
 * @see CCTX_setOutBufferSizes for committing the actual written size
 */
void* CCTX_getWPtrFromNewStream(
        ZL_CCtx* cctx,
        RTNodeID rtnodeid,
        int outStreamIndex,
        size_t eltWidth,
        size_t eltCount);

/**
 * @brief Create a new output stream for a transform node and return a handle to
 * it.
 *
 * This function creates a new output stream for the specified transform node
 * and allocates a buffer for it. The stream type is determined by the
 * transform's output specification.
 *
 * @param cctx The compression context (must be non-NULL and have a graph set)
 * @param rtnodeid The runtime node ID of the transform that will produce this
 * stream
 * @param outStreamIndex The index of the output stream within the transform's
 * outputs (0-based)
 * @param eltWidth The width of each element in bytes (must be > 0 for most
 * stream types)
 * @param eltCount The number of elements to allocate capacity for
 *
 * @return A handle to the newly created stream, or NULL if allocation failed
 *
 * @note This function is thread-safe only within the context of a single
 * compression session.
 * @note The returned stream must be committed using ZL_Data_commit() before the
 * transform completes.
 * @note Memory for the stream is allocated from the session arena and will be
 * freed when the compression session ends.
 *
 * @see CCTX_refContentIntoNewStream for creating streams that reference
 * existing data
 * @see CCTX_setOutBufferSizes for committing multiple output streams at once
 */
ZL_Data* CCTX_getNewStream(
        ZL_CCtx* cctx,
        RTNodeID rtnodeid,
        int outStreamIndex,
        size_t eltWidth,
        size_t eltCount);

/**
 * @brief Create a new stream that references a slice of an existing stream.
 *
 * Similar to CCTX_getNewStream(), but instead of allocating new memory, this
 * function creates a stream that contains a read-only reference to a portion of
 * an existing stream. This is useful for transforms that want to expose part of
 * their input as output without copying the data.
 *
 * @param cctx The compression context (must be non-NULL and have a graph set)
 * @param rtnodeid The runtime node ID of the transform that will produce this
 * stream
 * @param outcomeID The index of the output stream within the transform's
 * outputs (0-based)
 * @param eltWidth The width of each element in bytes in the new stream view
 * @param eltCount The number of elements the new stream should contain
 * @param src The source stream to reference (must be committed and contain
 * sufficient data)
 * @param offsetBytes Byte offset into the source stream where the reference
 * should start
 *
 * @return A handle to the newly created reference stream, or NULL if the
 * operation failed
 *
 * @note The source stream must remain valid for the lifetime of the returned
 * stream.
 * @note The referenced data range [offsetBytes, offsetBytes + eltWidth *
 * eltCount) must be entirely within the bounds of the source stream.
 * @note The returned stream is automatically committed and should not be
 * written to.
 * @note This is more efficient than copying data when you only need to expose a
 * subset of existing data.
 *
 * @see CCTX_getNewStream for creating streams with newly allocated buffers
 */
ZL_Data* CCTX_refContentIntoNewStream(
        ZL_CCtx* cctx,
        RTNodeID rtnodeid,
        int outcomeID,
        size_t eltWidth,
        size_t eltCount,
        ZL_Data const* src,
        size_t offsetBytes);

/**
 * @brief Commit the actual sizes of output streams produced by a transform
 * node.
 *
 * This function finalizes the output streams created by a transform by setting
 * their actual produced sizes. It must be called after a transform has written
 * data to its output streams but before the transform execution completes.
 *
 * @param cctx The compression context (must be non-NULL)
 * @param rtnodeid The runtime node ID of the transform that produced the
 * streams (must be valid)
 * @param writtenSizes Array containing the actual number of bytes written to
 * each output stream. The array must contain exactly `nbOutStreams` elements,
 * and each element must not exceed the capacity allocated for the corresponding
 * stream.
 * @param nbOutStreams The number of output streams to commit (must be ≤ the
 * actual number of output streams created by the transform)
 *
 * @return ZL_SUCCESS on success, or an error code if:
 *         - Any stream commitment fails (e.g., size exceeds capacity)
 *         - The runtime node ID is invalid
 *         - A stream has already been committed
 *
 * @note this is only used for the splitTransform wrapper.
 * @note This function must be called exactly once, at end of transform.
 * @note The order of sizes in `writtenSizes` must match the order of stream
 * creation.
 * @note Once committed, streams become read-only and cannot be modified.
 * @note This function is called by the engine, not user code.
 *
 * @see CCTX_getNewStream for creating output streams
 * @see CCTX_checkOutputCommitted for verifying all streams were committed
 */
ZL_Report CCTX_setOutBufferSizes(
        ZL_CCtx* cctx,
        RTNodeID rtnodeid,
        const size_t writtenSizes[],
        size_t nbOutStreams);

/**
 * @brief Verify that all output streams of a transform node have been properly
 * committed.
 *
 * This validation function checks that every output stream created by the
 * specified transform has been committed with a valid size. It's used
 * internally to ensure transforms have properly finalized their outputs before
 * the compression pipeline continues.
 *
 * @param cctx The compression context (must be non-NULL)
 * @param rtnodeid The runtime node ID of the transform to validate (must be
 * valid)
 *
 * @return ZL_SUCCESS if all output streams are committed, or an error code if:
 *         - One or more output streams remain uncommitted
 *         - The runtime node ID is invalid
 *         - The transform execution failed to complete properly
 *
 * @note This function is called automatically by the compression engine after
 * each transform.
 * @note Uncommitted streams indicate a bug in the transform implementation.
 * @note The error message will identify which specific stream was not
 * committed.
 * @note This function is for internal validation and debugging, not typical
 * user code.
 *
 * @see CCTX_setOutBufferSizes for committing output streams
 * @see CCTX_getNewStream for creating output streams
 */
ZL_Report CCTX_checkOutputCommitted(const ZL_CCtx* cctx, RTNodeID rtnodeid);

/**
 * @brief Enumerate all data buffers that need to be stored in the compressed
 * frame.
 *
 * This function provides information about all the data buffers that will be
 * included in the final compressed output. It returns both transform header
 * data and stream data that resulted from the compression pipeline execution.
 * The buffers are listed in the order they will appear in the compressed frame.
 *
 * @param cctx The compression context (must be non-NULL and have completed
 * compression)
 * @param rba Output array to receive buffer information. Each element describes
 * one buffer to be stored, including its memory location and size.
 * @param rbaCapacity The maximum number of buffer entries that can be stored in
 * `rba`. Must be ≥ 1 to accommodate at least the transform header buffer.
 *
 * @return On success, returns the actual number of buffers to store (≤
 * `rbaCapacity`). On error, returns an error code if the context is invalid or
 * buffers cannot be enumerated (> `rbaCapacity`)
 *
 * @note The first buffer (rba[0]) always contains transform header information.
 * @note The remaining buffers contain compressed stream data in dependency
 * order.
 * @note The returned buffers are read-only and must not be modified.
 * @note Buffer contents are only valid until the next compression session
 * starts.
 * @note If `rbaCapacity` is too small, returns an error.
 *
 * @warning Caller must ensure `rba` has space for at least `rbaCapacity`
 * elements.
 *
 * @see ZL_RBuffer for buffer descriptor structure
 * @see CCTX_getFinalGraph for getting complete compression metadata
 */
ZL_Report CCTX_listBuffersToStore(
        const ZL_CCtx* cctx,
        ZL_RBuffer* rba,
        size_t rbaCapacity);

/**
 * @brief Check if a Compressor has been set in the compression context.
 *
 * This function determines whether a compression graph (compressor) has been
 * associated with the compression context. A compressor must be set before
 * compression can begin.
 *
 * @param cctx The compression context to check (must be non-NULL)
 *
 * @return 1 if a graph is set, 0 if no graph is set
 *
 * @note It's possible to reference a new graph even if one is already set.
 * @note To unset a graph, use ZL_CCtx_resetParameters().
 * @note This function is typically used for validation before starting
 * compression.
 *
 * @see ZL_CCtx_refCompressor for setting a compression graph
 * @see CCTX_getCGraph for retrieving the current graph
 * @see ZL_CCtx_resetParameters for clearing the graph
 */
int CCTX_isGraphSet(const ZL_CCtx* cctx);

/**
 * @brief Get the compression graph associated with the compression context.
 *
 * This function retrieves the current compression graph (cgraph) that defines
 * the compression pipeline and available transforms. The graph contains all
 * the nodes, transforms, and routing information needed for compression.
 *
 * @param cctx The compression context (must be non-NULL)
 *
 * @return Pointer to the compression graph, or NULL if no graph is set
 *
 * @note The returned pointer is owned by the context and should not be freed.
 * @note The graph remains valid until the context is destroyed or reset.
 * @note Use CCTX_isGraphSet() to check if a graph is available before calling
 * this.
 *
 * @see CCTX_isGraphSet for checking if a graph is set
 * @see ZL_CCtx_refCompressor for setting a compression graph
 * @see ZL_Compressor for the compression graph structure
 */
const ZL_Compressor* CCTX_getCGraph(const ZL_CCtx* cctx);

/**
 * @brief Get the runtime graph manager from the compression context.
 *
 * The runtime graph manager tracks the execution state of the compression
 * pipeline, including all active nodes, streams, and their relationships. It
 * provides the runtime representation of the graph during execution.
 *
 * @param cctx The compression context (must be non-NULL)
 *
 * @return Pointer to the runtime graph manager, or NULL if compression hasn't
 * started
 *
 * @note The returned pointer is owned by the context and should not be freed.
 * @note The runtime graph is built during compression and reflects the actual
 * execution.
 * @note This is primarily used for introspection and debugging of the
 * compression pipeline.
 * @note The runtime graph becomes available once compression begins.
 *
 * @see RTGraph for the runtime graph structure
 * @see CCTX_getCGraph for getting the static compression graph
 */
const RTGraph* CCTX_getRTGraph(const ZL_CCtx* cctx);

/**
 * @brief Check if a compression node is supported by the current format
 * version.
 *
 * This function validates whether the specified node ID is compatible with the
 * current format version setting in the compression context. Each transform
 * node has minimum and maximum format version requirements that must be
 * satisfied.
 *
 * @param cctx The compression context (must be non-NULL with format version
 * set)
 * @param nodeid The node ID to validate (must be a valid node ID)
 *
 * @return true if the node is supported, false if:
 *         - The node ID is invalid
 *         - The current format version is below the node's minimum supported
 * version
 *         - The current format version is above the node's maximum supported
 * version
 *
 * @note The format version must be set via ZL_CCtx_setParameter() before
 * calling this function.
 * @note This function is typically used in dynamic graph construction to filter
 * available transforms.
 * @note Deprecated nodes may have restricted maximum format versions.
 *
 * @see CCTX_isGraphSupported for checking graph compatibility
 * @see ZL_CCtx_setParameter for setting the format version
 * @see ZL_Compressor_Node_getMinVersion for node version requirements
 */
bool CCTX_isNodeSupported(const ZL_CCtx* cctx, ZL_NodeID nodeid);

/**
 * @brief Get the current memory usage of all streams managed by the compression
 * context.
 *
 * This function calculates the total amount of memory currently allocated for
 * all runtime streams in the compression context. This includes input streams,
 * intermediate streams created by transforms, and output streams. The memory is
 * allocated from the context's stream arena.
 *
 * @param cctx The compression context (must be non-NULL)
 *
 * @return The total number of bytes currently allocated for streams
 *
 * @note The returned value reflects dynamic memory usage that grows during
 * compression.
 * @note Memory usage will be 0 before compression starts and after context
 * cleanup.
 * @note This function is useful for monitoring memory consumption during
 * compression.
 * @note The memory is automatically freed when the compression session ends.
 *
 * @see RTGM_streamMemory for the underlying implementation
 * @see CCTX_clean for stream memory cleanup
 */
size_t CCTX_streamMemory(const ZL_CCtx* cctx);

/**
 * @brief runs a Graph and all its sub-graphs within cctx.
 *
 * This will populate the RT Manager, which tracks creation of Nodes and
 * Streams.
 *
 * @param cctx The compression context (must be non-NULL)
 *
 * @return Success, or an error
 *
 * @see CCTX_flushChunk
 */
ZL_Report CCTX_runSuccessor(
        ZL_CCtx* cctx,
        ZL_GraphID graphid,
        const ZL_RuntimeGraphParameters* rgp,
        const RTStreamID* rtInputs,
        size_t nbInputs,
        unsigned depth);

/**
 * @brief Generate final compression metadata and buffer information.
 *
 * This function constructs comprehensive metadata about the completed
 * compression session, including information about all transforms used, their
 * parameters, input/output relationships, and the final data buffers to be
 * stored. This metadata is essential for creating the compressed frame header.
 *
 * @param cctx The compression context (must be non-NULL and have completed
 * compression)
 * @param gip Output structure to receive the compression metadata. Contains
 * pointers into the context's internal arrays, so it must not outlive the
 * context.
 *
 * @return ZL_SUCCESS on success, or an error code if:
 *         - The compression session hasn't completed successfully
 *         - Format version limits are exceeded (too many
 * transforms/streams/inputs)
 *         - Internal metadata collection fails
 *
 * @note This function modifies internal arrays within the context during
 * metadata collection.
 * @note The GraphInfo structure contains pointers into the context, so its
 * lifetime must not exceed the context's lifetime.
 * @note Transform information is listed in decoding order (reverse)
 * @note This function must be called before finalizing the compressed output.
 *
 * @warning The returned GraphInfo structure becomes invalid when the context is
 * destroyed or when another compression session begins.
 *
 * @see GraphInfo for the metadata structure definition
 * @see CCTX_listBuffersToStore for buffer enumeration details
 */
ZL_Report CCTX_getFinalGraph(ZL_CCtx* cctx, GraphInfo* gip);

/**
 * Output a chunk into destination buffer (previously referenced in @p cctx).
 * @p inputs is required to process content checksum.
 * @note (@cyan) an alternative could be to generate the content checksum at the
 * beginning, and store it inside @p cctx.
 */
ZL_Report
CCTX_flushChunk(ZL_CCtx* cctx, const ZL_Data* inputs[], size_t nbInputs);

/**
 * Clean temporary memory used while generating a compressed chunk.
 * Only session-level memory is preserved.
 */
void CCTX_cleanChunk(ZL_CCtx* cctx);

/**
 * @brief Clean up compression session state for context reuse.
 *
 * This internal function resets the compression context to a clean state,
 * freeing all temporary buffers and runtime state while preserving the graph
 * configuration and parameters. This allows the same context to be used for
 * multiple compression sessions without recreation.
 *
 * @param cctx The compression context to clean (must be non-NULL)
 *
 * @note This function is called automatically at the end of each compression
 * session.
 * @note All runtime streams and transform state are cleared.
 * @note Arena memory is freed but the arenas themselves are preserved.
 * @note Graph settings and global parameters are not reset by this method.
 *       See ZL_CCtx_resetParameters().
 *
 * @see CCTX_create for initial context creation
 * @see CCTX_free for complete context destruction
 */
void CCTX_clean(ZL_CCtx* cctx);

/**
 * @brief Perform complete compression with a pre-configured compression
 * context.
 *
 * This high-level function executes the entire compression pipeline using a
 * context that already has a compression graph attached. It handles the
 * complete workflow from input processing through final frame generation,
 * producing a compressed output buffer.
 *
 * @param cctx The compression context (must be non-NULL with a graph set)
 * @param dst Output buffer to receive the compressed data (must be non-NULL)
 * @param dstCapacity The size of the output buffer in bytes (must be > 0)
 * @param inputs Array of input data streams to compress (must be non-NULL with
 * valid streams)
 * @param nbInputs The number of input streams (must be ≥ 1 and match graph
 * requirements)
 *
 * @return On success, returns the number of bytes written to the output buffer.
 *         On error, returns an error code if:
 *         - No compression graph is set in the context
 *         - Input validation fails (invalid streams, count mismatch)
 *         - Compression pipeline execution fails
 *         - Output buffer is too small for the compressed data
 *
 * @note The compression graph must be set via ZL_CCtx_refCompressor() before
 * calling.
 * @note Global parameters should be configured before compression begins.
 * @note This function handles the complete compression workflow internally.
 * @note The context can be reused for subsequent compressions.
 *
 * @see ZL_CCtx_refCompressor for setting the compression graph
 * @see CCTX_startCompression for the core compression implementation
 * @see ZL_CCtx_setParameter for configuring compression parameters
 */
ZL_Report CCTX_compressInputs_withGraphSet(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const ZL_Data* inputs[],
        size_t nbInputs);

ZL_RESULT_OF(ZL_GraphPerformance)
CCTX_tryGraph(
        const ZL_CCtx* parentCCtx,
        const ZL_Input* inputs[],
        size_t numInputs,
        Arena* wkspArena,
        ZL_GraphID graph,
        const ZL_RuntimeGraphParameters* params);

/**
 * @return The comment stored in the cctx.
 */
ZL_Comment CCTX_getHeaderComment(const ZL_CCtx* cctx);

/**
 * Writes @p comment into a field of the cctx.
 */
ZL_Report
CCTX_setHeaderComment(ZL_CCtx* cctx, const void* comment, size_t commentSize);

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_CCTX_H
