// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "sddl2.h"

#include <stddef.h>

#include "openzl/codecs/splitByStruct/encode_splitByStruct_binding.h"
#include "openzl/codecs/zl_clustering.h" // ZL_CLUSTERING_TAG_METADATA_ID
#include "openzl/common/assertion.h"
#include "openzl/common/logging.h"
#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compressor.h"  // ZL_Compressor_registerParameterizedGraph
#include "openzl/zl_localParams.h" // ZL_CopyParam, ZL_LocalParams
#include "openzl/zl_public_nodes.h"

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
 * Determine endianness for a given SDDL2 type.
 *
 * @param type_kind The SDDL2 type kind
 * @param out_is_little_endian Output parameter for endianness result
 * @return ZL_Report indicating success or error
 *
 * Note: 1-byte types have no inherent endianness; we arbitrarily choose
 * little-endian for consistency.
 */
static ZL_Report sddl2_determine_endianness(
        SDDL2_Type_kind type_kind,
        bool* out_is_little_endian,
        ZL_Graph* graph)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    switch (type_kind) {
        // 1-byte types (no endianness - arbitrary choice: little-endian)
        case SDDL2_TYPE_U8:
        case SDDL2_TYPE_I8:
        case SDDL2_TYPE_F8:
            *out_is_little_endian = true;
            break;

        // Little-endian types
        case SDDL2_TYPE_U16LE:
        case SDDL2_TYPE_I16LE:
        case SDDL2_TYPE_U32LE:
        case SDDL2_TYPE_I32LE:
        case SDDL2_TYPE_U64LE:
        case SDDL2_TYPE_I64LE:
        case SDDL2_TYPE_F16LE:
        case SDDL2_TYPE_BF16LE:
        case SDDL2_TYPE_F32LE:
        case SDDL2_TYPE_F64LE:
            *out_is_little_endian = true;
            break;

        // Big-endian types
        case SDDL2_TYPE_U16BE:
        case SDDL2_TYPE_I16BE:
        case SDDL2_TYPE_U32BE:
        case SDDL2_TYPE_I32BE:
        case SDDL2_TYPE_U64BE:
        case SDDL2_TYPE_I64BE:
        case SDDL2_TYPE_F16BE:
        case SDDL2_TYPE_BF16BE:
        case SDDL2_TYPE_F32BE:
        case SDDL2_TYPE_F64BE:
            *out_is_little_endian = false;
            break;

        // BYTES type should be handled by caller
        case SDDL2_TYPE_BYTES:
            ZL_ERR(GENERIC,
                   "BYTES type should be filtered before endianness check");

        // STRUCTURE type should be handled by caller
        case SDDL2_TYPE_STRUCTURE:
            ZL_ERR(GENERIC,
                   "STRUCTURE type should be filtered before endianness check");

        default:
            ZL_ERR(GENERIC, "Unknown SDDL2 type kind: %d", (int)type_kind);
    }

    return ZL_returnSuccess();
}

ZL_GraphID ZL_Compressor_registerSDDL2Graph(
        ZL_Compressor* const compressor,
        const void* const bytecode,
        const size_t bytecode_size,
        const ZL_GraphID destination)
{
    // Setup bytecode parameter (using CopyParam like SDDL1)
    const ZL_CopyParam cp = {
        .paramId   = SDDL2_BYTECODE_PARAM,
        .paramPtr  = bytecode,
        .paramSize = bytecode_size,
    };

    const ZL_LocalParams lp = {
        .intParams  = {},
        .copyParams = {
            .copyParams   = &cp,
            .nbCopyParams = 1,
        },
        .refParams = {},
    };

    // Create parameterized graph descriptor with bytecode and destination
    const ZL_ParameterizedGraphDesc desc = {
        .name           = NULL, // Name derived from base graph
        .graph          = ZL_GRAPH_SDDL2,
        .customGraphs   = &destination,
        .nbCustomGraphs = 1,
        .customNodes    = NULL,
        .nbCustomNodes  = 0,
        .localParams    = &lp,
    };

    // Register using standard parameterization mechanism
    return ZL_Compressor_registerParameterizedGraph(compressor, &desc);
}

/**
 * Count the total number of primitive fields in a type (recursive).
 *
 * For structures, recursively counts all primitive fields within.
 * For primitives, returns the width (number of elements).
 *
 * Rejects arrays of structures (width > 1 on STRUCTURE type).
 *
 * @param graph Graph context for error reporting
 * @param type The type to analyze
 * @return ZL_Report containing the field count on success, or error
 */
static ZL_Report sddl2_count_primitive_fields(ZL_Graph* graph, SDDL2_Type type)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    if (type.kind == SDDL2_TYPE_STRUCTURE) {
        // Reject arrays of structures (complex edge case)
        if (type.width > 1) {
            ZL_ERR(GENERIC,
                   "Arrays of structures not yet supported (width=%u)",
                   type.width);
        }

        // For single structure instance, recursively count fields
        ZL_ERR_IF_NULL(
                type.struct_data,
                GENERIC,
                "Structure type has NULL struct_data");

        size_t total = 0;
        for (size_t i = 0; i < type.struct_data->member_count; i++) {
            ZL_TRY_LET_R(
                    member_count,
                    sddl2_count_primitive_fields(
                            graph, type.struct_data->members[i]));
            total += member_count;
        }

        return ZL_returnValue(total);
    } else {
        // Primitive type (including arrays): counts as 1 field
        // The width is handled by field SIZE calculation, not field COUNT
        // Example: Bytes[2] is 1 field of size 2, not 2 fields of size 1
        return ZL_returnValue(1);
    }
}

/**
 * Recursively flatten a type's field sizes into an array.
 *
 * For structures, recursively flattens all nested fields.
 * For primitives, appends the field size.
 *
 * @param graph Graph context for error reporting
 * @param type The type to flatten
 * @param field_sizes Output array (must be pre-allocated)
 * @param index Pointer to current index in output array (updated during
 * recursion)
 * @return ZL_Report indicating success or error
 */
static ZL_Report sddl2_flatten_field_sizes(
        ZL_Graph* graph,
        SDDL2_Type type,
        size_t* field_sizes,
        size_t* index)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    if (type.kind == SDDL2_TYPE_STRUCTURE) {
        // Reject arrays of structures
        if (type.width > 1) {
            ZL_ERR(GENERIC,
                   "Arrays of structures not yet supported (width=%u)",
                   type.width);
        }

        // Recursively flatten all members
        ZL_ERR_IF_NULL(
                type.struct_data,
                GENERIC,
                "Structure type has NULL struct_data");

        for (size_t i = 0; i < type.struct_data->member_count; i++) {
            ZL_ERR_IF_ERR(sddl2_flatten_field_sizes(
                    graph, type.struct_data->members[i], field_sizes, index));
        }
    } else {
        // Primitive type: calculate size and append
        size_t field_size = SDDL2_Type_size(type);

        // Skip zero-sized fields (invalid types)
        if (field_size == 0) {
            ZL_DLOG(BLOCK,
                    "Skipping field with zero size (type kind %d)",
                    (int)type.kind);
            return ZL_returnSuccess();
        }

        field_sizes[(*index)++] = field_size;
    }

    return ZL_returnSuccess();
}

/**
 * Extract field sizes from a structure type (supports nested structures).
 *
 * Recursively flattens nested structures into a flat array of primitive field
 * sizes. Supports arbitrary nesting depth as long as all structures have
 * width=1.
 *
 * @param graph Graph context for memory allocation and error reporting
 * @param struct_type The structure type to analyze
 * @param out_field_sizes Output pointer to array of field sizes
 * @param out_nb_fields Output pointer to number of fields
 * @return ZL_Report indicating success or error
 *
 * Supported:
 * - Nested structures with width=1: {U8, {I16LE, I32LE}, F64BE}
 * - Arrays of primitives: {U8, [I32LE × 10], F64BE}
 * - Arbitrary nesting depth
 *
 * Not supported (rejected with error):
 * - Arrays of structures: [{U8, I32LE} × 10]
 */
static ZL_Report sddl2_extract_flat_field_sizes(
        ZL_Graph* graph,
        SDDL2_Type struct_type,
        size_t** out_field_sizes,
        size_t* out_nb_fields)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    // Validate this is a structure type
    ZL_ERR_IF_NE(
            struct_type.kind,
            SDDL2_TYPE_STRUCTURE,
            GENERIC,
            "Expected structure type, got type kind %d",
            (int)struct_type.kind);

    // Count total primitive fields (recursive)
    ZL_TRY_LET_R(
            total_fields, sddl2_count_primitive_fields(graph, struct_type));

    ZL_ERR_IF_EQ(
            total_fields,
            0,
            GENERIC,
            "Structure has no valid primitive fields");

    // Allocate field sizes array using arena
    size_t* field_sizes = (size_t*)ZL_Graph_getScratchSpace(
            graph, total_fields * sizeof(size_t));
    ZL_ERR_IF_NULL(field_sizes, allocation);

    // Recursively flatten field sizes
    size_t index = 0;
    ZL_ERR_IF_ERR(
            sddl2_flatten_field_sizes(graph, struct_type, field_sizes, &index));

    // Verify we filled the expected number of fields
    ZL_ERR_IF_NE(
            index,
            total_fields,
            GENERIC,
            "Field count mismatch: expected %zu, got %zu",
            total_fields,
            index);

    *out_field_sizes = field_sizes;
    *out_nb_fields   = total_fields;

    return ZL_returnSuccess();
}

/**
 * Recursively extract primitive field types from a structure (flattened).
 *
 * Similar to sddl2_flatten_field_sizes, but extracts the actual SDDL2_Type_kind
 * for each primitive field. This is needed to apply proper type conversions.
 *
 * @param graph Graph context for error reporting
 * @param type The type to flatten
 * @param field_types Output array of SDDL2_Type_kind (must be pre-allocated)
 * @param index Pointer to current index in output array (updated during
 * recursion)
 * @return ZL_Report indicating success or error
 */
static ZL_Report sddl2_flatten_field_types(
        ZL_Graph* graph,
        SDDL2_Type type,
        SDDL2_Type_kind* field_types,
        size_t* index)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    if (type.kind == SDDL2_TYPE_STRUCTURE) {
        // Reject arrays of structures
        if (type.width > 1) {
            ZL_ERR(GENERIC,
                   "Arrays of structures not yet supported (width=%u)",
                   type.width);
        }

        // Recursively flatten all members
        ZL_ERR_IF_NULL(
                type.struct_data,
                GENERIC,
                "Structure type has NULL struct_data");

        for (size_t i = 0; i < type.struct_data->member_count; i++) {
            ZL_ERR_IF_ERR(sddl2_flatten_field_types(
                    graph, type.struct_data->members[i], field_types, index));
        }
    } else {
        // Primitive type: append its kind
        // For array types (width > 1), we still just record the element type
        // The width is handled by field_sizes array
        field_types[(*index)++] = type.kind;
    }

    return ZL_returnSuccess();
}

/**
 * Extract field types from a structure type (supports nested structures).
 *
 * Recursively flattens nested structures into a flat array of primitive field
 * types. Works in tandem with sddl2_extract_flat_field_sizes().
 *
 * @param graph Graph context for memory allocation and error reporting
 * @param struct_type The structure type to analyze
 * @param out_field_types Output pointer to array of SDDL2_Type_kind
 * @return ZL_Report containing the number of fields on success, or error
 */
static ZL_Report sddl2_extract_flat_field_types(
        ZL_Graph* graph,
        SDDL2_Type struct_type,
        SDDL2_Type_kind** out_field_types)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    // Count total primitive fields (reuse existing function)
    ZL_TRY_LET_R(
            total_fields, sddl2_count_primitive_fields(graph, struct_type));

    ZL_ERR_IF_EQ(
            total_fields,
            0,
            GENERIC,
            "Structure has no valid primitive fields");

    // Allocate field types array using arena
    SDDL2_Type_kind* field_types = (SDDL2_Type_kind*)ZL_Graph_getScratchSpace(
            graph, total_fields * sizeof(SDDL2_Type_kind));
    ZL_ERR_IF_NULL(field_types, allocation);

    // Recursively flatten field types
    size_t index = 0;
    ZL_ERR_IF_ERR(
            sddl2_flatten_field_types(graph, struct_type, field_types, &index));

    // Verify we filled the expected number of fields
    ZL_ERR_IF_NE(
            index,
            total_fields,
            GENERIC,
            "Field type count mismatch: expected %zu, got %zu",
            total_fields,
            index);

    *out_field_types = field_types;

    return ZL_returnValue(total_fields);
}

/**
 * Convert a Struct edge (from split-by-struct) to a Numeric edge.
 *
 * This function handles the specific conversion needed for structure fields
 * after split-by-struct operation. Split-by-struct outputs Struct edges
 * with embedded size information, which need to be converted to Numeric
 * edges with appropriate endianness.
 *
 * @param graph Graph context for error reporting
 * @param struct_edge The Struct edge to convert (output from split-by-struct)
 * @param field_type_kind The SDDL2 type kind for this field (determines
 * endianness)
 * @param out_converted_edge Output pointer to the converted Numeric edge
 * @return ZL_Report indicating success or error
 */
static ZL_Report sddl2_apply_struct_field_conversion(
        ZL_Graph* graph,
        ZL_Edge* struct_edge,
        SDDL2_Type_kind field_type_kind,
        ZL_Edge** out_converted_edge)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    // Skip BYTES type (shouldn't happen with structures, but be defensive)
    if (field_type_kind == SDDL2_TYPE_BYTES) {
        ZL_ERR(GENERIC, "BYTES type not supported in structure fields");
    }

    // Determine endianness for this field
    bool is_little_endian;
    ZL_ERR_IF_ERR(sddl2_determine_endianness(
            field_type_kind, &is_little_endian, graph));

    // Get the appropriate Struct→Numeric conversion node
    ZL_NodeID convert_node;
    if (is_little_endian) {
        convert_node = ZL_NODE_CONVERT_STRUCT_TO_NUM_LE;
    } else {
        convert_node = ZL_NODE_CONVERT_STRUCT_TO_NUM_BE;
    }

    // Apply Struct→Numeric conversion
    ZL_TRY_LET_T(
            ZL_EdgeList, converted, ZL_Edge_runNode(struct_edge, convert_node));

    // Validate that conversion produced exactly one edge
    ZL_ERR_IF_NE(
            converted.nbEdges,
            1,
            GENERIC,
            "Struct-to-numeric conversion should produce exactly 1 edge, got %zu",
            converted.nbEdges);

    *out_converted_edge = converted.edges[0];
    return ZL_returnSuccess();
}

/**
 * Apply split-by-struct transform to a structure segment.
 *
 * Splits an edge containing an array of structures into N separate edges,
 * one for each primitive field. Handles nested structures by flattening them.
 *
 * Process:
 * 1. Extract flattened field sizes from structure type
 * 2. Extract flattened field types from structure type
 * 3. Run split-by-struct node with field sizes as runtime parameters
 * 4. Apply type conversion to each output edge based on field type
 * 5. Route each field edge to COMPRESS_GENERIC
 *
 * @param graph Graph context for operations and error reporting
 * @param edge The edge containing the structure array
 * @param seg The segment containing the structure type information
 * @param dest Destination graph for field edges
 * @param next_stream_id Pointer to counter for generating unique stream tags
 * @return ZL_Report indicating success or error
 *
 * Example:
 * Input: Array of {U8, I16LE, I32LE} structures
 * Output: 3 edges - [all U8], [all I16LE], [all I32LE]
 */
static ZL_Report sddl2_apply_structure_split(
        ZL_Graph* graph,
        ZL_Edge* edge,
        const SDDL2_Segment* seg,
        ZL_GraphID dest,
        int* next_stream_id)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    ZL_DLOG(BLOCK,
            "Applying split-by-struct to segment with structure type (width=%u)",
            seg->type.width);

    // Step 1: Extract flattened field sizes
    size_t* field_sizes = NULL;
    size_t nb_fields    = 0;
    ZL_ERR_IF_ERR(sddl2_extract_flat_field_sizes(
            graph, seg->type, &field_sizes, &nb_fields));

    ZL_DLOG(BLOCK, "Structure has %zu flattened primitive fields", nb_fields);

    // Step 2: Extract flattened field types (for later conversion)
    SDDL2_Type_kind* field_types = NULL;
    ZL_TRY_LET_R(
            nb_field_types,
            sddl2_extract_flat_field_types(graph, seg->type, &field_types));

    // Sanity check: field counts must match
    ZL_ERR_IF_NE(
            nb_fields,
            nb_field_types,
            GENERIC,
            "Field count mismatch: sizes=%zu, types=%zu",
            nb_fields,
            nb_field_types);

    // Step 3: Create copy parameter for field sizes
    ZL_CopyParam const fieldSizesParam = {
        .paramId   = ZL_SPLITBYSTRUCT_FIELDSIZES_PID,
        .paramPtr  = field_sizes,
        .paramSize = nb_fields * sizeof(size_t)
    };

    // Package into LocalParams structure
    ZL_LocalCopyParams const lcp = { &fieldSizesParam, 1 };
    ZL_LocalParams const lParams = { .copyParams = lcp };

    // Step 4: Run split-by-struct node with runtime parameters
    ZL_TRY_LET_T(
            ZL_EdgeList,
            split_outputs,
            ZL_Edge_runNode_withParams(
                    edge, ZL_NODE_SPLIT_BY_STRUCT, &lParams));

    // Validate we got the expected number of output edges
    ZL_ERR_IF_NE(
            split_outputs.nbEdges,
            nb_fields,
            GENERIC,
            "Split-by-struct produced %zu edges, expected %zu",
            split_outputs.nbEdges,
            nb_fields);

    ZL_DLOG(BLOCK, "Split-by-struct produced %zu field edges", nb_fields);

    // Step 5: Apply Struct→Numeric conversion to each field edge
    for (size_t i = 0; i < split_outputs.nbEdges; i++) {
        SDDL2_Type_kind field_type_kind = field_types[i];

        // Skip BYTES type (shouldn't happen with structures, but check anyway)
        if (field_type_kind == SDDL2_TYPE_BYTES) {
            ZL_DLOG(BLOCK, "Field %zu: skipping BYTES type", i);
            continue;
        }

        // Apply Struct→Numeric conversion (split-by-struct outputs Struct
        // edges)
        ZL_ERR_IF_ERR(sddl2_apply_struct_field_conversion(
                graph,
                split_outputs.edges[i],
                field_type_kind,
                &split_outputs.edges[i]));

        ZL_DLOG(BLOCK,
                "Field %zu: converted Struct→Numeric (type kind %d)",
                i,
                (int)field_type_kind);
    }

    // Step 6: Attach clustering tags and route all field edges to destination
    for (size_t i = 0; i < split_outputs.nbEdges; i++) {
        int stream_tag = (*next_stream_id)++; // Assign and increment counter
        ZL_ERR_IF_ERR(ZL_Edge_setIntMetadata(
                split_outputs.edges[i],
                ZL_CLUSTERING_TAG_METADATA_ID,
                stream_tag));
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(split_outputs.edges[i], dest));
    }

    ZL_DLOG(BLOCK,
            "Structure split complete: %zu fields routed to destination",
            nb_fields);

    return ZL_returnSuccess();
}

/**
 * Apply type conversion to a segment edge.
 *
 * Converts a Serial edge to a Numeric edge with the appropriate bit width
 * and endianness based on the segment's type information.
 *
 * For array types (width > 1), this converts the primitive element type,
 * not the entire array. For example, Type{U32LE, 10} converts each U32LE
 * element (32 bits), not the whole 320-bit array.
 *
 * @param graph Graph context for error reporting
 * @param edge The edge to convert
 * @param seg The segment containing type information
 * @param out_converted_edge Output pointer to the converted edge
 * @return ZL_Report indicating success or error
 */
static ZL_Report sddl2_apply_type_conversion(
        ZL_Graph* graph,
        ZL_Edge* edge,
        const SDDL2_Segment* seg,
        ZL_Edge** out_converted_edge)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    // Determine primitive element size in bytes (not including width)
    // For array types, we convert the base element, not the full array
    size_t element_size = SDDL2_kind_size(seg->type.kind);
    ZL_ERR_IF_EQ(
            element_size,
            0,
            GENERIC,
            "Invalid SDDL2 type kind %d for segment (unsupported or zero-sized type)",
            (int)seg->type.kind);

    // Determine endianness
    bool is_little_endian;
    ZL_ERR_IF_ERR(sddl2_determine_endianness(
            seg->type.kind, &is_little_endian, graph));

    // Get the appropriate conversion node based on endianness and size
    size_t bit_width = element_size * 8;
    ZL_NodeID convert_node;
    if (is_little_endian) {
        convert_node = ZL_Node_convertSerialToNumLE(bit_width);
    } else {
        convert_node = ZL_Node_convertSerialToNumBE(bit_width);
    }

    // Apply type conversion to the edge
    ZL_TRY_LET_T(ZL_EdgeList, converted, ZL_Edge_runNode(edge, convert_node));

    // Validate that conversion produced exactly one edge
    ZL_ERR_IF_NE(
            converted.nbEdges,
            1,
            GENERIC,
            "Type conversion should produce exactly 1 edge, got %zu",
            converted.nbEdges);

    *out_converted_edge = converted.edges[0];
    return ZL_returnSuccess();
}

/**
 * Process a single segment: apply type conversion and route to destination.
 *
 * Handles three types of segments:
 * - BYTES: Route directly to destination without conversion
 * - STRUCTURE: Split into field arrays, convert each field, route to
 * destination
 * - Primitive: Convert Serial→Numeric and route to destination
 *
 * @param graph Graph context for operations and error reporting
 * @param edge The edge to process
 * @param seg The segment metadata containing type information
 * @param dest The destination graph for non-structure segments
 * @param next_stream_id Pointer to counter for generating unique stream tags
 * @return ZL_Report indicating success or error
 */
static ZL_Report sddl2_process_segment(
        ZL_Graph* graph,
        ZL_Edge* edge,
        const SDDL2_Segment* seg,
        ZL_GraphID dest,
        int* next_stream_id)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    switch (seg->type.kind) {
        case SDDL2_TYPE_BYTES:
            // BYTES segments: attach clustering tag and route
            {
                int stream_tag = (*next_stream_id)++;
                ZL_ERR_IF_ERR(ZL_Edge_setIntMetadata(
                        edge, ZL_CLUSTERING_TAG_METADATA_ID, stream_tag));
                return ZL_Edge_setDestination(edge, dest);
            }

        case SDDL2_TYPE_STRUCTURE:
            // STRUCTURE segments: split, convert fields, attach tags, and route
            return sddl2_apply_structure_split(
                    graph, edge, seg, dest, next_stream_id);

        // Primitive numeric types: convert Serial→Numeric, attach tag, and
        // route
        case SDDL2_TYPE_U8:
        case SDDL2_TYPE_I8:
        case SDDL2_TYPE_U16LE:
        case SDDL2_TYPE_U16BE:
        case SDDL2_TYPE_I16LE:
        case SDDL2_TYPE_I16BE:
        case SDDL2_TYPE_U32LE:
        case SDDL2_TYPE_U32BE:
        case SDDL2_TYPE_I32LE:
        case SDDL2_TYPE_I32BE:
        case SDDL2_TYPE_U64LE:
        case SDDL2_TYPE_U64BE:
        case SDDL2_TYPE_I64LE:
        case SDDL2_TYPE_I64BE:
        case SDDL2_TYPE_F8:
        case SDDL2_TYPE_F16LE:
        case SDDL2_TYPE_F16BE:
        case SDDL2_TYPE_BF16LE:
        case SDDL2_TYPE_BF16BE:
        case SDDL2_TYPE_F32LE:
        case SDDL2_TYPE_F32BE:
        case SDDL2_TYPE_F64LE:
        case SDDL2_TYPE_F64BE: {
            ZL_ERR_IF_ERR(sddl2_apply_type_conversion(graph, edge, seg, &edge));
            int stream_tag = (*next_stream_id)++;
            ZL_ERR_IF_ERR(ZL_Edge_setIntMetadata(
                    edge, ZL_CLUSTERING_TAG_METADATA_ID, stream_tag));
            return ZL_Edge_setDestination(edge, dest);
        }
    }

    // Unreachable: all SDDL2_Type_kind values are handled above
    ZL_ERR(GENERIC, "Unknown SDDL2 type kind: %d", (int)seg->type.kind);
}

/**
 * Convert SDDL2 VM error codes to OpenZL ZL_Report with descriptive
 * messages.
 *
 * This function maps internal VM errors to appropriate OpenZL error codes,
 * preserving semantic meaning while providing rich error context for
 * callers.
 *
 * @param graph Graph context for error reporting
 * @param err The SDDL2 error code to convert
 * @return ZL_Report with mapped error code and descriptive message
 */
static ZL_Report sddl2_error_to_report(ZL_Graph* graph, SDDL2_Error err)
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

        case SDDL2_MATH_OVERFLOW:
            ZL_ERR(transform_executionFailure,
                   "SDDL2 VM mathematical operation overflows");

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

        case SDDL2_ALLOCATION_FAILED:
            ZL_ERR(allocation, "SDDL2 VM memory allocation failed");

        case SDDL2_VALIDATION_FAILED:
            ZL_ERR(parameter_invalid,
                   "SDDL2 VM validation failed: expect_true condition not met");
    }

    // Fallback for unexpected error codes
    ZL_ERR(GENERIC, "SDDL2 VM returned unknown error code: %d", (int)err);
}

ZL_Report SDDL2_parse(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
        ZL_NOEXCEPT_FUNC_PTR
{
    // Assertions: Validate OpenZL framework contract (development checks)
    ZL_ASSERT_NN(graph);
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    // Step 1: Validate input count - SDDL2 expects exactly one input
    ZL_ERR_IF_NE(nbInputs, 1, graph_invalidNumInputs);
    ZL_ASSERT_NN(inputs);

    // Step 2: Validate input type - must be Serial
    const ZL_Input* input_obj = ZL_Edge_getData(inputs[0]);
    ZL_ASSERT_NN(input_obj); // Edge_getData contract: never returns NULL

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
    SDDL2_Segment_list segments;

    SDDL2_Segment_list_init(&segments, sddl2_arena_allocator, graph);

    SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input_data, input_size, &segments);

    if (err != SDDL2_OK) {
        SDDL2_Segment_list_destroy(&segments);
        return sddl2_error_to_report(graph, err);
    }

    // Step 6: Split input by segment sizes
    // Allocate scratch space for segment sizes array
    size_t* segmentSizes = (size_t*)ZL_Graph_getScratchSpace(
            graph, segments.count * sizeof(size_t));
    ZL_ERR_IF_NULL(segmentSizes, allocation);

    for (size_t i = 0; i < segments.count; i++) {
        segmentSizes[i] = segments.items[i].size_bytes;
    }

    ZL_TRY_LET(
            ZL_EdgeList,
            outputs,
            ZL_Edge_runSplitNode(inputs[0], segmentSizes, segments.count));

    // Step 7: Determine selected destination (via Custom Graphs parameter)
    ZL_GraphID dest        = ZL_GRAPH_COMPRESS_GENERIC;
    ZL_GraphIDList gidlist = ZL_Graph_getCustomGraphs(graph);
    ZL_ERR_IF_GT(
            gidlist.nbGraphIDs,
            1,
            GENERIC,
            "SDDL2_parse supports at most 1 custom graph, got %zu",
            gidlist.nbGraphIDs);
    if (gidlist.nbGraphIDs) {
        ZL_ASSERT_NN(gidlist.graphids);
        dest = gidlist.graphids[0];
    }

    // Step 8: Initialize stream counter and process each segment
    int next_stream_id = 0;
    for (size_t i = 0; i < outputs.nbEdges; i++) {
        ZL_ERR_IF_ERR(sddl2_process_segment(
                graph,
                outputs.edges[i],
                &segments.items[i],
                dest,
                &next_stream_id));
    }

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);

    return ZL_returnSuccess();
}
