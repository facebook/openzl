// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "sddl2.h"

#include <stddef.h>

#include "openzl/codecs/zl_split.h"
#include "openzl/codecs/zl_zstd.h"
#include "openzl/compress/graphs/sddlv2/sddl2_interpreter.h"

/**
 * SDDL2 Function Graph - OpenZL Integration
 *
 * This function graph executes SDDL2 bytecode to parse and segment input data.
 *
 * Process:
 * 1. Extract bytecode from local parameters
 * 2. Extract input data from edge
 * 3. Execute bytecode interpreter to generate segment list
 * 4. Split input edge by segment sizes
 * 5. Route each segment to ZSTD compression
 */

/**
 * Arena allocator wrapper for ZL_Graph_getScratchSpace.
 * Used by SDDL2 VM to allocate memory via OpenZL's arena.
 */
static void* sddl2_arena_allocator(void* allocator_ctx, size_t size)
{
    return ZL_Graph_getScratchSpace((ZL_Graph*)allocator_ctx, size);
}

/**
 * Convert SDDL2 VM error codes to OpenZL ZL_Report with descriptive messages.
 *
 * This function maps internal VM errors to appropriate OpenZL error codes,
 * preserving semantic meaning while providing rich error context for callers.
 *
 * @param err The SDDL2 error code to convert
 * @param graph Graph context for error reporting
 * @return ZL_Report with mapped error code and descriptive message
 */
static ZL_Report sddl2_error_to_report(SDDL2_error err, ZL_Graph* graph)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    switch (err) {
        case SDDL2_OK:
            return ZL_returnSuccess();

        case SDDL2_INVALID_BYTECODE:
            ZL_ERR(parameter_invalid,
                   "SDDL2 bytecode is malformed or contains invalid "
                   "instructions");

        case SDDL2_STACK_OVERFLOW:
            ZL_ERR(transform_executionFailure,
                   "SDDL2 VM stack overflow: operation exceeded maximum "
                   "stack depth");

        case SDDL2_STACK_UNDERFLOW:
            ZL_ERR(transform_executionFailure,
                   "SDDL2 VM stack underflow: operation attempted to pop "
                   "from empty stack");

        case SDDL2_TYPE_MISMATCH:
            ZL_ERR(parameter_invalid,
                   "SDDL2 VM type error: operation received incompatible "
                   "value types");

        case SDDL2_LOAD_BOUNDS:
            ZL_ERR(corruption,
                   "SDDL2 VM attempted to load data beyond input buffer "
                   "bounds");

        case SDDL2_SEGMENT_BOUNDS:
            ZL_ERR(srcSize_tooSmall,
                   "SDDL2 VM segment extends beyond input buffer "
                   "boundaries");

        case SDDL2_LIMIT_EXCEEDED:
            ZL_ERR(internalBuffer_tooSmall,
                   "SDDL2 VM capacity limit exceeded: too many segments "
                   "or tags");

        case SDDL2_DIV_ZERO:
            ZL_ERR(parameter_invalid,
                   "SDDL2 VM division by zero in bytecode execution");
    }

    // Fallback for unexpected error codes
    ZL_ERR(GENERIC, "SDDL2 VM returned unknown error code: %d", (int)err);
}

ZL_Report SDDL2_parse(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
        ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    // Step 1: Validate input count - SDDL2 expects exactly one input
    ZL_ERR_IF_NE(nbInputs, 1, graph_invalidNumInputs);

    // Step 2: Validate input type - must be Serial
    const ZL_Input* input_obj = ZL_Edge_getData(inputs[0]);
    ZL_ERR_IF_NE(
            ZL_Input_type(input_obj), ZL_Type_serial, inputType_unsupported);

    // Step 3: Extract bytecode from local parameters
    ZL_RefParam bytecodeParam =
            ZL_Graph_getLocalRefParam(graph, SDDL2_BYTECODE_PARAM);

    // Validate bytecode parameter was provided
    ZL_ERR_IF_NE(
            bytecodeParam.paramId,
            SDDL2_BYTECODE_PARAM,
            graphParameter_invalid);

    const void* bytecode = bytecodeParam.paramRef;
    size_t bytecode_size = bytecodeParam.paramSize;

    // Sanity check: NULL bytecode must have zero size
    if (bytecode == NULL) {
        ZL_ERR_IF_NE(bytecode_size, 0, graphParameter_invalid);
    }

    // Step 4: Extract input data from edge
    const void* input_data = ZL_Input_ptr(input_obj);
    size_t input_size      = ZL_Input_contentSize(input_obj);

    // Step 5: Run interpreter to generate segments
    SDDL2_segment_list segments;
    // Use arena allocator for production (via ZL_Graph_getScratchSpace)
    SDDL2_segment_list_init(&segments, sddl2_arena_allocator, graph);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input_data, input_size, &segments);

    if (err != SDDL2_OK) {
        SDDL2_segment_list_destroy(&segments);
        return sddl2_error_to_report(err, graph);
    }

    // Step 5: Split input by segment sizes
    // Allocate scratch space for segment sizes array
    size_t* segmentSizes = (size_t*)ZL_Graph_getScratchSpace(
            graph, segments.count * sizeof(size_t));
    ZL_ERR_IF_NULL(segmentSizes, allocation);

    for (size_t i = 0; i < segments.count; i++) {
        segmentSizes[i] = segments.items[i].size_bytes;
    }

    // Split the input edge by segment sizes
    ZL_TRY_LET(
            ZL_EdgeList,
            outputs,
            ZL_Edge_runSplitNode(inputs[0], segmentSizes, segments.count));

    // Step 6: Set destinations for each output edge
    for (size_t i = 0; i < outputs.nbEdges; i++) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(outputs.edges[i], ZL_GRAPH_ZSTD));
    }

    // Cleanup
    SDDL2_segment_list_destroy(&segments);

    return ZL_returnSuccess();
}
