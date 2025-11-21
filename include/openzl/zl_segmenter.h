// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_SEGMENTER_H
#define OPENZL_SEGMENTER_H

#include <stdbool.h> // bool
#include <stddef.h>  // size_t

#include "openzl/zl_common_types.h" // ZL_OpaquePtr
#include "openzl/zl_compress.h"     // ZL_CParam
#include "openzl/zl_graph_api.h"    // ZL_RuntimeGraphParameters
#include "openzl/zl_localParams.h"  // ZL_LocalParams
#include "openzl/zl_opaque_types.h" // ZL_GraphID

#if defined(__cplusplus)
extern "C" {
#endif

// --------------------------------------------
// Segmenter registration
// --------------------------------------------

/* The chunking operation is done by a dedicated object, the Segmenter,
 * similar to yet distinct from Graphs, with dedicated responsibilities.
 *
 * The Segmenter must be registered, like a Graph.
 * Since a Segmenter deals with user Inputs, it typically is the first operation
 * of a Compressor. Alternatively, it can follow any set of Selectors, as long
 * as none of these steps alter user Input in any way.
 *
 * The job of the Segmenter is to decide Chunk Boundaries,
 * determining a unit of work, and pass it down to a selected Successor Graph.
 * Each Chunk can receive a different successor Graph.
 *
 * @note: One downside of this approach is that any information gathered by the
 * Segmenter during its execution isn't automatically available for the
 * following Graph stage. Fortunately, it's still possible to pass it down using
 * runtime parameters, though it then creates a contract between 2 entities.
 *
 * Each Chunk is guaranteed to be processed and output in order.
 * At decompression time, each compressed Chunk is enough to decompress and
 * flush its content, thus allowing streaming during decompression.
 *
 * The Segmenter is allowed to consult Global Parameters and receive Local
 * Parameters that can influence its decisions.
 *
 * A Segmenter context remains valid for the entire compression.
 * Its memory is allocated using session lifetime arena allocator.
 * There is no limit in the number of buffers or structures that can be
 * allocated using this arena. They will all be released at the end of
 * compression. Segmenters are discouraged from using their own allocation, and
 * should use the provided arena allocator instead.
 *
 * @note: extension for Streaming mode (not implemented yet):
 * In streaming mode, a Segmenter is invoked in a loop, defining any number of
 * Chunks per invocation. It manages its own state, in order to resume from a
 * known state. The Input is not necessarily complete, and more data may be
 * provided in subsequent calls. The Segmenter is informed if Input is complete
 * or not. In accumulation mode, a Segmenter is allowed to not create a Chunk
 * with remaining input data, and store it instead, waiting for more data before
 * settling on a Chunk decision. Buffering is an explicit decision, and the
 * Segmenter may receive data from 2 separate buffers in subsequent calls.
 *
 * Occasionally, the segmenter may receive an explicit order to flush all
 * remaining data, in which case, it *must* generate one or more Chunks with
 * whatever data is left from Input.
 */

typedef struct ZL_Segmenter_s ZL_Segmenter;
typedef ZL_Report (*ZL_SegmenterFn)(ZL_Segmenter* sctx);

typedef struct {
    const char* name; // optional
    ZL_SegmenterFn segmenterFn;
    const ZL_Type* inputTypeMasks;
    size_t numInputs;
    bool lastInputIsVariable; // Last input can optionally be marked as
                              // variable, meaning it is allowed to be present
                              // multiple times (including 0).
    /* optional list of custom graphs and parameters */
    const ZL_GraphID* customGraphs; // can be NULL when none employed
    size_t numCustomGraphs;         // Must be zero when customGraphs==NULL
    ZL_LocalParams localParams;
    /**
     * Optionally an opaque pointer that can be queried with
     * ZL_Graph_getOpaquePtr().
     * OpenZL unconditionally takes ownership of this pointer, even if
     * registration fails, and it lives for the lifetime of the compressor.
     */
    ZL_OpaquePtr opaque;
} ZL_SegmenterDesc;

/**
 * @note: Registration can fail if the Descriptor is incorrectly filled.
 * Such an outcome can be tested with ZL_GraphID_isValid().
 */
ZL_GraphID ZL_Compressor_registerSegmenter(
        ZL_Compressor* compressor,
        const ZL_SegmenterDesc* segDesc);

/**
 * In this variant, success of the registration operation is part of the result
 * type.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_registerSegmenter2(
        ZL_Compressor* compressor,
        const ZL_SegmenterDesc* segDesc);

// *************************************
// API for Function Graph
// *************************************

/* Accessors */
/* --------- */

/**
 * @brief Retrieve the opaque pointer associated with this segmenter.
 *
 * Returns the opaque pointer that was provided during segmenter registration
 * via ZL_SegmenterDesc.opaque. This allows segmenters to access their
 * custom state or configuration data.
 *
 * @param segCtx The segmenter context
 * @return The opaque pointer, or NULL if none was provided during registration
 */
const void* ZL_Segmenter_getOpaquePtr(const ZL_Segmenter* segCtx);

// Request parameters
/**
 * @brief Retrieve a global compression parameter value.
 *
 * Allows the segmenter to consult global compression parameters that were
 * set during compressor configuration. This enables segmenters to adapt
 * their behavior based on the overall compression strategy.
 *
 * @param segCtx The segmenter context
 * @param gparam The global parameter to retrieve
 * @return The parameter value. If the parameter does not exist, returns 0.
 */
int ZL_Segmenter_getCParam(const ZL_Segmenter* segCtx, ZL_CParam gparam);

/**
 * @brief Retrieves all local params for the segmenter.
 *
 * A convenience function equivalent to calling ZL_Segmenter_getLocalIntParam()
 * and ZL_Segmenter_getLocalRefParam() on each int and ref param.
 */
const ZL_LocalParams* ZL_Segmenter_getLocalParams(const ZL_Segmenter* segCtx);

/**
 * @brief Retrieve a local integer parameter value.
 *
 * Accesses local integer parameters that were provided during segmenter
 * registration via ZL_SegmenterDesc.localParams. These parameters allow
 * fine-tuning of segmenter behavior on a per-instance basis.
 *
 * @param segCtx The segmenter context
 * @param intParamId The ID of the integer parameter to retrieve
 * @return The integer parameter value and validity status
 */
ZL_IntParam ZL_Segmenter_getLocalIntParam(
        const ZL_Segmenter* segCtx,
        int intParamId);

/**
 * @brief Retrieve a local reference parameter value.
 *
 * Accesses local reference parameters that were provided during segmenter
 * registration via ZL_SegmenterDesc.localParams. These parameters can
 * reference external data or configurations.
 *
 * @param segCtx The segmenter context
 * @param refParamId The ID of the reference parameter to retrieve
 * @return The reference parameter value and validity status
 */
ZL_RefParam ZL_Segmenter_getLocalRefParam(
        const ZL_Segmenter* segCtx,
        int refParamId);

/**
 * @brief Retrieve the list of custom successor graphs available to this
 * segmenter.
 *
 * Returns the custom graphs that were registered with this segmenter via
 * ZL_SegmenterDesc.customGraphs. These graphs can be used as targets for
 * chunk processing operations.
 *
 * @param segCtx The segmenter context
 * @return A list of available custom graph IDs, or an empty list if none were
 * registered
 */
ZL_GraphIDList ZL_Segmenter_getCustomGraphs(const ZL_Segmenter* segCtx);

/**
 * @brief Get the number of input streams available to this segmenter.
 *
 * Returns the count of input streams that this segmenter must process.
 * This is related to the numInputs value from the ZL_SegmenterDesc used
 * during registration, but can be different is the last input is variable.
 *
 * @param segCtx The segmenter context
 * @return The number of input streams available
 */
size_t ZL_Segmenter_numInputs(const ZL_Segmenter* segCtx);

/**
 * @brief Access input stream data for analysis and chunking decisions.
 *
 * Provides access to the actual input data to enable informed chunking
 * decisions. If chunks have already been processed, the returned ZL_Input
 * starts from the remaining unprocessed data.
 *
 * This function is essential for segmenters that need to analyze input
 * content to determine optimal chunk boundaries (e.g., based on data
 * patterns, entropy, or structural markers).
 *
 * @param segCtx The segmenter context
 * @param inputID The input stream ID (0-based indexing)
 * @return Pointer to the ZL_Input for the specified stream, or NULL if
 *         the inputID is invalid (exceeds available inputs)
 *
 * @note In future streaming mode, this method may require adjustments
 *       due to buffering and partial input availability considerations.
 */
const ZL_Input* ZL_Segmenter_getInput(
        const ZL_Segmenter* segCtx,
        size_t inputID);

/**
 * @brief Get the number of elements available in all input streams.
 *
 * Performs a bulk request to retrieve the current number of elements
 * available in all input streams simultaneously. This is more efficient
 * than querying each input individually and provides a consistent
 * snapshot of all input states.
 *
 * The element count represents different units depending on input type:
 * - Bytes for serial/binary inputs
 * - Field count for struct and numeric inputs
 * - String count for string inputs
 *
 * @param segCtx The segmenter context
 * @param numElts Pre-allocated array to store element counts for each input.
 *                Must be sized to accommodate numInputs elements.
 * @param numInputs The number of input streams (can be obtained via
 * ZL_Segmenter_numInputs())
 * @return ZL_Report indicating success or failure (e.g., if numInputs is
 * incorrect)
 *
 * @note Element counts change over time as inputs are progressively
 *       consumed by ZL_Segmenter_processChunk() operations.
 * @note The numElts array must be pre-allocated by the caller with sufficient
 *       space for numInputs elements.
 */
ZL_Report ZL_Segmenter_getNumElts(
        const ZL_Segmenter* segCtx,
        size_t numElts[],
        size_t numInputs);

/* Actions */
/* ------- */

/**
 * @brief Allocate memory using the session-lifetime arena allocator.
 *
 * Requests memory space from the OpenZL engine's arena allocator. The segmenter
 * can request multiple buffers of any size during its execution. This is the
 * preferred method for segmenter memory allocation.
 *
 * Memory characteristics:
 * - Buffers are not initialized (contain undefined data)
 * - Individual buffers cannot be freed separately
 * - All allocated memory persists until compression completion
 * - Memory is automatically released by the engine at session end
 * - Multiple allocation requests are supported
 *
 * @param segCtx The segmenter context
 * @param size The number of bytes to allocate
 * @return Pointer to the allocated memory buffer, or NULL on allocation failure
 *
 * @note In future streaming scenarios with multiple segmenter invocations,
 *       allocated memory will persist across calls, but segmenters will need
 *       a state management mechanism to track allocated buffers between
 *       invocations (since stack variables are reset).
 */
void* ZL_Segmenter_getScratchSpace(ZL_Segmenter* segCtx, size_t size);

/**
 * @brief Process a chunk of input data through the specified graph.
 *
 * Cuts a chunk from the input streams and processes it immediately using
 * the specified graph ID. In single-threaded mode, this is a blocking call
 * that returns only after the chunk is completely processed.
 *
 * This function consumes the specified number of elements from each input
 * stream and forwards them to the designated processing graph. The consumed
 * data is removed from the input streams, advancing their position for
 * subsequent operations.
 *
 * Requirements for successful operation:
 * - The numElts array size must equal the number of inputs (numInputs)
 * - Each input must contain at least the requested number of elements
 * - Element units vary by input type:
 *   * Bytes for serial/binary inputs
 *   * Field count for struct and numeric inputs
 *   * String count for string inputs
 *
 * @note At the end of the Segmenter operation,
 * all input data must be consumed; any unprocessed portions cause errors.
 *
 * @param segCtx The segmenter context
 * @param numElts Array specifying the number of elements to consume from each
 * input. Array size must match the number of inputs.
 * @param startingGraphID The graph ID to process this chunk
 * @return ZL_Report indicating success or failure of the chunk processing
 * operation
 *
 * @note Future multi-threading support may make this function non-blocking,
 *       processing chunks in parallel.
 *
 * @note Chunking is not the same as Streaming operation - the entire input must
 * be present and fully consumed. Future streaming capabilities may require
 *       interface changes.
 */
ZL_Report ZL_Segmenter_processChunk(
        ZL_Segmenter* segCtx,
        const size_t numElts[],
        size_t numInputs,
        ZL_GraphID startingGraphID,
        const ZL_RuntimeGraphParameters* rGraphParams);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_SEGMENTER_H
