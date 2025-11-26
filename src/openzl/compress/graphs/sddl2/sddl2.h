// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_GRAPHS_SDDL_V2_H
#define OPENZL_GRAPHS_SDDL_V2_H

#include "openzl/zl_graph_api.h"

#define SDDL2_BYTECODE_PARAM 7685

/**
 * Standard graph macro for SDDL2 (Simple Data Description Language v2)
 *
 * NOTE: SDDL2_parse requires bytecode as a parameter. Use
 * ZL_Compressor_registerSDDL2Graph() to register it with your bytecode.
 */
#define ZL_GRAPH_SDDL2                                         \
    (ZL_GraphID)                                               \
    {                                                          \
        ZL_StandardGraphID_simple_data_description_language_v2 \
    }

/**
 * SDDL2 parse function - interprets SDDL2 bytecode to segment and route data.
 *
 * This function graph executes SDDL2 bytecode to parse and segment input data.
 * It requires bytecode to be provided via local parameters
 * (SDDL2_BYTECODE_PARAM).
 *
 * Use ZL_Compressor_registerSDDL2Graph() for easier registration.
 *
 * @param graph Graph context for operations and error reporting
 * @param inputs Array of input edges (expects exactly 1 Serial edge)
 * @param nbInputs Number of input edges (must be 1)
 * @return ZL_Report indicating success or error
 */
ZL_Report SDDL2_parse(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
        ZL_NOEXCEPT_FUNC_PTR;

/**
 * Register SDDL2 parser as a parameterized graph with bytecode and destination.
 *
 * This creates a variant of the standard SDDL2 graph with specific bytecode
 * and a destination graph for non-structure segments. Structure segments are
 * always routed to COMPRESS_GENERIC internally.
 *
 * Example usage:
 *   ZL_GraphID sddl2_gid = ZL_Compressor_registerSDDL2Graph(
 *       cgraph, bytecode_array, bytecode_size, ZL_GRAPH_STORE);
 *   if (!ZL_GraphID_isValid(sddl2_gid)) {
 *       // handle error
 *   }
 *
 * @param compressor The compressor to register with
 * @param bytecode Pointer to SDDL2 bytecode
 * @param bytecode_size Size of bytecode in bytes
 * @param destination Destination graph for non-structure segments (required)
 * @return ZL_GraphID for the registered graph, or ZL_GRAPH_ILLEGAL on error
 */
ZL_GraphID ZL_Compressor_registerSDDL2Graph(
        ZL_Compressor* compressor,
        const void* bytecode,
        size_t bytecode_size,
        ZL_GraphID destination);

#endif // OPENZL_GRAPHS_SDDL_V2_H
