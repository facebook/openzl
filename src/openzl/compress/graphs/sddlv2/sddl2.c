// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "sddl2.h"

#include <assert.h>
#include <stddef.h>

#include "openzl/codecs/zl_split.h"
#include "openzl/codecs/zl_zstd.h"
#include "openzl/compress/graphs/sddlv2/sddl2_interpreter.h"
#include "openzl/zl_selector.h" // ZL_LP_INVALID_PARAMID

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
    ZL_ERR_IF_NE(bytecodeParam.paramId, SDDL2_BYTECODE_PARAM, graph_invalid);

    const void* bytecode = bytecodeParam.paramRef;
    size_t bytecode_size = bytecodeParam.paramSize;
    if (bytecode == NULL)
        assert(bytecode_size == 0);

    // Step 4: Extract input data from edge
    const void* input_data = ZL_Input_ptr(input_obj);
    size_t input_size      = ZL_Input_contentSize(input_obj);

    // Step 5: Run interpreter to generate segments
    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input_data, input_size, &segments);

    if (err != SDDL2_OK) {
        SDDL2_segment_list_destroy(&segments);
        ZL_ERR(GENERIC); // should find or create better error code
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
