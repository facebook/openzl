// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "sddl2.h"

#include <limits.h>
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
#include "openzl/zl_segmenter.h"
#include "openzl/zl_selector.h" // ZL_LP_INVALID_PARAMID
#include "openzl/zl_version.h"

/**
 * SDDL2 segmenter and chunk replay integration.
 *
 * Public flow:
 * 1. Execute the SDDL2 VM once over the full serial input in `SDDL2_segment()`.
 * 2. Partition the emitted segment plan into chunk-local slices, splitting
 *    inside a segment only when that segment alone exceeds the chunk target.
 * 3. Replay each slice through the private `!zl.private.sddl2_chunk` graph.
 */

/**
 * Arena allocator wrapper for ZL_Segmenter_getScratchSpace.
 * Segmenter scratch space is session-lifetime, so emitted segment plans remain
 * valid while chunk-local inner graphs replay them.
 */
static void* sddl2_segmenter_arena_allocator(void* allocator_ctx, size_t size)
{
    return ZL_Segmenter_getScratchSpace((ZL_Segmenter*)allocator_ctx, size);
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

static ZL_GraphID sddl2_register_graph(
        ZL_Compressor* const compressor,
        const void* const bytecode,
        const size_t bytecode_size,
        const ZL_GraphID* const custom_graphs,
        const size_t nb_custom_graphs,
        const size_t chunk_byte_size)
{
    const bool has_chunk_size = chunk_byte_size != 0;
    if (chunk_byte_size > (size_t)INT_MAX) {
        ZL_LOG(WARN,
               "sddl2_register_graph: chunk_byte_size=%zu exceeds INT_MAX",
               chunk_byte_size);
        return ZL_GRAPH_ILLEGAL;
    }
    if (nb_custom_graphs > 1) {
        ZL_LOG(WARN,
               "sddl2_register_graph: expected at most 1 custom graph, got %zu",
               nb_custom_graphs);
        return ZL_GRAPH_ILLEGAL;
    }

    const ZL_CopyParam copyParams[]  = { {
             .paramId   = SDDL2_BYTECODE_PARAM,
             .paramPtr  = bytecode,
             .paramSize = bytecode_size,
    } };
    const ZL_IntParam intParams[]    = { {
               .paramId    = SDDL2_CHUNK_BYTE_SIZE_PARAM,
               .paramValue = (int)chunk_byte_size,
    } };
    const ZL_LocalParams localParams = {
        .intParams  = { intParams, has_chunk_size ? 1 : 0 },
        .copyParams = { copyParams, 1 },
    };
    const ZL_ParameterizedGraphDesc desc = {
        .graph          = ZL_GRAPH_SDDL2,
        .customGraphs   = custom_graphs,
        .nbCustomGraphs = nb_custom_graphs,
        .localParams    = &localParams,
    };

    return ZL_Compressor_registerParameterizedGraph(compressor, &desc);
}

ZL_GraphID ZL_Compressor_registerSDDL2Graph(
        ZL_Compressor* const compressor,
        const void* const bytecode,
        const size_t bytecode_size)
{
    return sddl2_register_graph(
            compressor, bytecode, bytecode_size, NULL, 0, 0);
}

ZL_GraphID ZL_Compressor_registerSDDL2Graph_advanced(
        ZL_Compressor* const compressor,
        const void* const bytecode,
        const size_t bytecode_size,
        const ZL_GraphID destination,
        const size_t chunk_byte_size)
{
    return sddl2_register_graph(
            compressor,
            bytecode,
            bytecode_size,
            &destination,
            1,
            chunk_byte_size);
}

static bool sddl2_try_count_primitive_fields(
        SDDL2_Type type,
        size_t* out_total_fields)
{
    if (type.kind != SDDL2_TYPE_STRUCTURE) {
        *out_total_fields = 1;
        return true;
    }

    if (type.struct_data == NULL) {
        return false;
    }

    size_t total = 0;
    for (size_t i = 0; i < type.struct_data->member_count; i++) {
        size_t member_total = 0;
        if (!sddl2_try_count_primitive_fields(
                    type.struct_data->members[i], &member_total)
            || member_total > SIZE_MAX - total) {
            return false;
        }
        total += member_total;
    }

    *out_total_fields = total;
    return true;
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

    size_t total_fields = 0;
    ZL_ERR_IF_NOT(
            sddl2_try_count_primitive_fields(type, &total_fields),
            GENERIC,
            "Failed to count primitive fields for SDDL2 type");

    return ZL_returnValue(total_fields);
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
        SDDL2_RESULT_OF(size_t) const field_size_report = SDDL2_Type_size(type);
        ZL_ERR_IF(
                SDDL2_isError(field_size_report),
                GENERIC,
                "Failed to compute size for type kind %d",
                (int)type.kind);
        size_t const field_size = SDDL2_value(field_size_report);

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
    ZL_TRY_LET(
            size_t,
            total_fields,
            sddl2_count_primitive_fields(graph, struct_type));

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
    ZL_TRY_LET(
            size_t,
            total_fields,
            sddl2_count_primitive_fields(graph, struct_type));

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
    ZL_TRY_LET(
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

static ZL_Report sddl2_set_next_stream_tag(
        ZL_Graph* graph,
        ZL_Edge* edge,
        ZL_IDType* next_stream_id);

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
 * @param type The structure type information
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
        SDDL2_Type type,
        ZL_GraphID dest,
        ZL_IDType* next_stream_id)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    ZL_DLOG(BLOCK,
            "Applying split-by-struct to segment with structure type (width=%u)",
            type.width);

    // Step 1: Extract flattened field sizes
    size_t* field_sizes = NULL;
    size_t nb_fields    = 0;
    ZL_ERR_IF_ERR(sddl2_extract_flat_field_sizes(
            graph, type, &field_sizes, &nb_fields));

    ZL_DLOG(BLOCK, "Structure has %zu flattened primitive fields", nb_fields);

    // Step 2: Extract flattened field types (for later conversion)
    SDDL2_Type_kind* field_types = NULL;
    ZL_TRY_LET(
            size_t,
            nb_field_types,
            sddl2_extract_flat_field_types(graph, type, &field_types));

    // Sanity check: field counts must match
    ZL_ERR_IF_NE(
            nb_fields,
            nb_field_types,
            GENERIC,
            "Field count mismatch: sizes=%zu, types=%zu",
            nb_fields,
            nb_field_types);

    // Step 3: Build non-zero sizes for split-by-struct
    size_t* nonzero_sizes = (size_t*)ZL_Graph_getScratchSpace(
            graph, nb_fields * sizeof(size_t));
    ZL_ERR_IF_NULL(nonzero_sizes, allocation);

    size_t nb_nonzero = 0;
    for (size_t i = 0; i < nb_fields; i++) {
        if (field_sizes[i] > 0) {
            nonzero_sizes[nb_nonzero++] = field_sizes[i];
        }
    }

    ZL_CopyParam const fieldSizesParam = {
        .paramId   = ZL_SPLITBYSTRUCT_FIELDSIZES_PID,
        .paramPtr  = nonzero_sizes,
        .paramSize = nb_nonzero * sizeof(size_t)
    };

    // Package into LocalParams structure
    ZL_LocalCopyParams const lcp = { &fieldSizesParam, 1 };
    ZL_LocalParams const lParams = { .copyParams = lcp };

    // Step 4: Run split-by-struct node with runtime parameters
    ZL_TRY_LET(
            ZL_EdgeList,
            split_outputs,
            ZL_Edge_runNode_withParams(
                    edge, ZL_NODE_SPLIT_BY_STRUCT, &lParams));

    // Validate we got the expected number of output edges
    ZL_ERR_IF_NE(
            split_outputs.nbEdges,
            nb_nonzero,
            GENERIC,
            "Split-by-struct produced %zu edges, expected %zu",
            split_outputs.nbEdges,
            nb_nonzero);

    ZL_DLOG(BLOCK, "Split-by-struct produced %zu field edges", nb_nonzero);

    // Step 5: Convert, tag, and route each field; skip IDs for zero-size
    size_t edge_idx = 0;
    for (size_t i = 0; i < nb_fields; i++) {
        // Skip zero-size fields but still increment stream ID
        if (field_sizes[i] == 0) {
            ZL_DLOG(BLOCK, "Field %zu: skipped (zero size)", i);
            *next_stream_id += 1;
            continue;
        }

        ZL_Edge* field_edge             = split_outputs.edges[edge_idx++];
        SDDL2_Type_kind field_type_kind = field_types[i];
        if (field_types[i] != SDDL2_TYPE_BYTES) {
            ZL_ERR_IF_ERR(sddl2_apply_struct_field_conversion(
                    graph, field_edge, field_type_kind, &field_edge));
        }

        ZL_DLOG(BLOCK,
                "Field %zu: converted Struct→Numeric (type kind %d)",
                i,
                (int)field_type_kind);

        // Attach clustering tag and route to destination
        ZL_ERR_IF_ERR(
                sddl2_set_next_stream_tag(graph, field_edge, next_stream_id));
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(field_edge, dest));
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
 * @param type The segment type information
 * @param out_converted_edge Output pointer to the converted edge
 * @return ZL_Report indicating success or error
 */
static ZL_Report sddl2_apply_type_conversion(
        ZL_Graph* graph,
        ZL_Edge* edge,
        SDDL2_Type type,
        ZL_Edge** out_converted_edge)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    // Determine primitive element size in bytes (not including width)
    // For array types, we convert the base element, not the full array
    SDDL2_RESULT_OF(size_t)
    const element_size_report = SDDL2_kind_size(type.kind);
    ZL_ERR_IF(
            SDDL2_isError(element_size_report),
            GENERIC,
            "Invalid SDDL2 type kind %d for segment (unsupported type)",
            (int)type.kind);
    size_t const element_size = SDDL2_value(element_size_report);

    // Determine endianness
    bool is_little_endian;
    ZL_ERR_IF_ERR(
            sddl2_determine_endianness(type.kind, &is_little_endian, graph));

    // Get the appropriate conversion node based on endianness and size
    size_t bit_width = element_size * 8;
    ZL_NodeID convert_node;
    if (is_little_endian) {
        convert_node = ZL_Node_convertSerialToNumLE(bit_width);
    } else {
        convert_node = ZL_Node_convertSerialToNumBE(bit_width);
    }

    // Apply type conversion to the edge
    ZL_TRY_LET(ZL_EdgeList, converted, ZL_Edge_runNode(edge, convert_node));

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

static ZL_Report sddl2_set_next_stream_tag(
        ZL_Graph* graph,
        ZL_Edge* edge,
        ZL_IDType* next_stream_id)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    ZL_ERR_IF_NULL(edge, GENERIC);
    ZL_ERR_IF_NULL(next_stream_id, GENERIC);
    ZL_ERR_IF_GT(
            *next_stream_id,
            (ZL_IDType)INT_MAX,
            internalBuffer_tooSmall,
            "SDDL2 clustering tag exceeded int metadata capacity");

    int const stream_tag = (int)(*next_stream_id);
    *next_stream_id += 1;
    return ZL_Edge_setIntMetadata(
            edge, ZL_CLUSTERING_TAG_METADATA_ID, stream_tag);
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
 * @param type The segment type metadata
 * @param dest The destination graph for non-structure segments
 * @param next_stream_id Pointer to counter for generating unique stream tags
 * @return ZL_Report indicating success or error
 */
static ZL_Report sddl2_process_segment(
        ZL_Graph* graph,
        ZL_Edge* edge,
        SDDL2_Type type,
        ZL_GraphID dest,
        ZL_IDType* next_stream_id)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    switch (type.kind) {
        case SDDL2_TYPE_BYTES:
            // BYTES segments: attach clustering tag and route
            {
                ZL_ERR_IF_ERR(
                        sddl2_set_next_stream_tag(graph, edge, next_stream_id));
                return ZL_Edge_setDestination(edge, dest);
            }

        case SDDL2_TYPE_STRUCTURE:
            // STRUCTURE segments: split, convert fields, attach tags, and route
            return sddl2_apply_structure_split(
                    graph, edge, type, dest, next_stream_id);

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
            ZL_ERR_IF_ERR(
                    sddl2_apply_type_conversion(graph, edge, type, &edge));
            ZL_ERR_IF_ERR(
                    sddl2_set_next_stream_tag(graph, edge, next_stream_id));
            return ZL_Edge_setDestination(edge, dest);
        }
    }

    // Unreachable: all SDDL2_Type_kind values are handled above
    ZL_ERR(GENERIC, "Unknown SDDL2 type kind: %d", (int)type.kind);
}

static ZL_Report sddl2_segmenter_error_to_report(
        ZL_Segmenter* sctx,
        SDDL2_Error err)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    switch (err) {
        case SDDL2_OK:
            return ZL_returnSuccess();

        case SDDL2_INVALID_BYTECODE:
            ZL_ERR(parameter_invalid,
                   "SDDL2 bytecode is malformed or contains invalid "
                   "instructions");

        case SDDL2_STACK_OVERFLOW:
            ZL_ERR(transform_executionFailure,
                   "SDDL2 VM stack overflow: operation exceeded "
                   "maximum stack depth");

        case SDDL2_STACK_UNDERFLOW:
            ZL_ERR(transform_executionFailure,
                   "SDDL2 VM stack underflow: operation attempted to "
                   "pop from empty stack");

        case SDDL2_MATH_OVERFLOW:
            ZL_ERR(transform_executionFailure,
                   "SDDL2 VM mathematical operation overflows");

        case SDDL2_TYPE_MISMATCH:
            ZL_ERR(parameter_invalid,
                   "SDDL2 VM type error: operation received "
                   "incompatible value types");

        case SDDL2_LOAD_BOUNDS:
            ZL_ERR(corruption,
                   "SDDL2 VM attempted to load data beyond input "
                   "buffer bounds");

        case SDDL2_SEGMENT_BOUNDS:
            ZL_ERR(srcSize_tooSmall,
                   "SDDL2 VM segment extends beyond input buffer "
                   "boundaries");

        case SDDL2_LIMIT_EXCEEDED:
            ZL_ERR(internalBuffer_tooSmall,
                   "SDDL2 VM capacity limit exceeded: too many "
                   "segments or tags");

        case SDDL2_DIV_ZERO:
            ZL_ERR(parameter_invalid,
                   "SDDL2 VM division by zero in bytecode execution");

        case SDDL2_ALLOCATION_FAILED:
            ZL_ERR(allocation, "SDDL2 VM memory allocation failed");

        case SDDL2_VALIDATION_FAILED:
            ZL_ERR(parameter_invalid,
                   "SDDL2 VM validation failed: expect_true "
                   "condition not met");
    }

    ZL_ERR(GENERIC, "SDDL2 VM returned unknown error code: %d", (int)err);
}

static ZL_Report sddl2_extract_single_destination(
        ZL_GraphIDList gidlist,
        ZL_GraphID* out_dest)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    ZL_ERR_IF_NULL(out_dest, GENERIC);
    ZL_GraphID dest = ZL_GRAPH_COMPRESS_GENERIC;
    ZL_ERR_IF_GT(
            gidlist.nbGraphIDs,
            1,
            GENERIC,
            "SDDL2 supports at most 1 custom graph, got %zu",
            gidlist.nbGraphIDs);
    if (gidlist.nbGraphIDs != 0) {
        ZL_ASSERT_NN(gidlist.graphids);
        dest = gidlist.graphids[0];
    }

    *out_dest = dest;
    return ZL_returnSuccess();
}

static ZL_Report sddl2_get_destination_graph(
        ZL_Graph* graph,
        ZL_GraphID* out_dest)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    return sddl2_extract_single_destination(
            ZL_Graph_getCustomGraphs(graph), out_dest);
}

static ZL_Report sddl2_get_segmenter_destination_graph(
        ZL_Segmenter* sctx,
        ZL_GraphID* out_dest)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    return sddl2_extract_single_destination(
            ZL_Segmenter_getCustomGraphs(sctx), out_dest);
}

static ZL_Report sddl2_get_replay_start_stream_id(ZL_Graph* graph)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    const ZL_IntParam startStreamParam = ZL_Graph_getLocalIntParam(
            graph, SDDL2_REPLAY_START_STREAM_ID_PARAM);
    if (startStreamParam.paramId == ZL_LP_INVALID_PARAMID) {
        return ZL_returnValue(0);
    }

    ZL_ERR_IF_LT(
            startStreamParam.paramValue,
            0,
            graphParameter_invalid,
            "SDDL2 replay start stream id must be non-negative");
    return ZL_returnValue((size_t)startStreamParam.paramValue);
}

static ZL_Report sddl2_get_replay_segments(
        ZL_Graph* graph,
        const SDDL2_ReplaySegment** out_segments,
        size_t* out_num_segments)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    const ZL_RefParam replaySegments =
            ZL_Graph_getLocalRefParam(graph, SDDL2_REPLAY_SEGMENTS_PARAM);
    ZL_ERR_IF_NE(
            replaySegments.paramId,
            SDDL2_REPLAY_SEGMENTS_PARAM,
            graphParameter_invalid,
            "SDDL2 replay requires a segments parameter");
    ZL_ERR_IF_NE(
            replaySegments.paramSize % sizeof(SDDL2_ReplaySegment),
            0,
            graphParameter_invalid,
            "SDDL2 replay segments parameter has invalid size");
    ZL_ERR_IF(
            replaySegments.paramRef == NULL && replaySegments.paramSize != 0,
            graphParameter_invalid,
            "SDDL2 replay segments parameter has a null payload");

    *out_segments     = (const SDDL2_ReplaySegment*)replaySegments.paramRef;
    *out_num_segments = replaySegments.paramSize / sizeof(SDDL2_ReplaySegment);
    return ZL_returnSuccess();
}

static ZL_Report sddl2_replay_segments(
        ZL_Graph* graph,
        ZL_Edge* input,
        const SDDL2_ReplaySegment* segments,
        size_t nbSegments)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    if (nbSegments == 0) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(input, ZL_GRAPH_STORE));
        return ZL_returnSuccess();
    }
    ZL_ERR_IF_NULL(
            segments,
            graphParameter_invalid,
            "SDDL2 replay requires a non-null segment slice");

    size_t* segmentSizes = (size_t*)ZL_Graph_getScratchSpace(
            graph, nbSegments * sizeof(size_t));
    ZL_ERR_IF_NULL(segmentSizes, allocation);

    size_t nbNonZero = 0;
    for (size_t i = 0; i < nbSegments; i++) {
        if (segments[i].size_bytes > 0) {
            segmentSizes[nbNonZero++] = segments[i].size_bytes;
        }
    }

    ZL_TRY_LET(
            ZL_EdgeList,
            outputs,
            ZL_Edge_runSplitNode(input, segmentSizes, nbNonZero));
    ZL_ERR_IF_NE(
            outputs.nbEdges,
            nbNonZero,
            GENERIC,
            "SDDL2 replay split produced %zu edges for %zu segments",
            outputs.nbEdges,
            nbNonZero);

    ZL_GraphID dest = ZL_GRAPH_COMPRESS_GENERIC;
    ZL_ERR_IF_ERR(sddl2_get_destination_graph(graph, &dest));
    ZL_TRY_LET(
            size_t,
            next_stream_id_value,
            sddl2_get_replay_start_stream_id(graph));
    ZL_IDType next_stream_id = (ZL_IDType)next_stream_id_value;

    size_t outputIdx = 0;
    for (size_t i = 0; i < nbSegments; i++) {
        if (segments[i].size_bytes == 0) {
            // Skip zero-sized segments but still increment the stream id
            ZL_TRY_LET(
                    size_t,
                    nbFields,
                    sddl2_count_primitive_fields(graph, segments[i].type));
            next_stream_id += (unsigned int)nbFields;
            continue;
        }
        ZL_ERR_IF_ERR(sddl2_process_segment(
                graph,
                outputs.edges[outputIdx++],
                segments[i].type,
                dest,
                &next_stream_id));
    }

    return ZL_returnSuccess();
}

static ZL_Report sddl2_process_segment_chunk(
        ZL_Segmenter* sctx,
        ZL_GraphID destination,
        const SDDL2_ReplaySegment* chunkSegments,
        size_t nbChunkSegments,
        size_t chunkBytes,
        ZL_IDType startStreamId)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    /*
     * Empty chunks never reach this helper: sddl2_flush_chunk_segments()
     * returns early on count == 0, and sddl2_plan_segment_chunks() handles the
     * top-level zero-segment case directly.
     */
    ZL_ASSERT_GT(nbChunkSegments, 0);

    ZL_ERR_IF_GT(
            startStreamId,
            (ZL_IDType)INT_MAX,
            internalBuffer_tooSmall,
            "SDDL2 chunk stream id overflowed");
    int const startStreamParamValue  = (int)startStreamId;
    const ZL_IntParam intParams[]    = { {
               .paramId    = SDDL2_REPLAY_START_STREAM_ID_PARAM,
               .paramValue = startStreamParamValue,
    } };
    const ZL_RefParam refParams[]    = { {
               .paramId   = SDDL2_REPLAY_SEGMENTS_PARAM,
               .paramRef  = chunkSegments,
               .paramSize = nbChunkSegments * sizeof(*chunkSegments),
    } };
    const ZL_LocalParams chunkParams = {
        .intParams = { intParams, 1 },
        .refParams = { refParams, 1 },
    };
    const ZL_GraphID customGraphs[]         = { destination };
    const ZL_RuntimeGraphParameters gparams = {
        .customGraphs   = customGraphs,
        .nbCustomGraphs = 1,
        .localParams    = &chunkParams,
    };

    ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
            sctx, &chunkBytes, 1, ZL_GRAPH_SDDL2_CHUNK, &gparams));
    return ZL_returnSuccess();
}

/* Mutable state for one in-flight replay chunk. */
typedef struct {
    SDDL2_ReplaySegment* segments;
    size_t capacity;
    size_t count;
    size_t bytes;
    ZL_IDType startStreamId;
} SDDL2_ChunkAccumulator;

static ZL_Report sddl2_append_chunk_replay_slice(
        ZL_Segmenter* sctx,
        SDDL2_ChunkAccumulator* chunk,
        ZL_IDType segmentStreamId,
        SDDL2_Type type,
        size_t sliceBytes)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    ZL_ERR_IF_NULL(chunk, GENERIC);
    ZL_ERR_IF_NULL(chunk->segments, allocation);
    ZL_ASSERT_LE(chunk->count, chunk->capacity);
    ZL_ERR_IF_GT(
            chunk->bytes,
            SIZE_MAX - sliceBytes,
            internalBuffer_tooSmall,
            "SDDL2 chunk byte count overflowed");

    if (chunk->count == 0) {
        chunk->startStreamId = segmentStreamId;
    }
    ZL_ERR_IF_EQ(
            chunk->count,
            chunk->capacity,
            internalBuffer_tooSmall,
            "SDDL2 chunk replay-slice buffer overflow");

    SDDL2_ReplaySegment const replaySlice = {
        .size_bytes = sliceBytes,
        .type       = type,
    };
    chunk->segments[chunk->count] = replaySlice;
    chunk->count += 1;
    chunk->bytes += sliceBytes;
    return ZL_returnSuccess();
}

static ZL_Report sddl2_flush_chunk_segments(
        ZL_Segmenter* sctx,
        ZL_GraphID destination,
        SDDL2_ChunkAccumulator* chunk)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    ZL_ERR_IF_NULL(chunk, GENERIC);
    if (chunk->count == 0) {
        return ZL_returnSuccess();
    }

    ZL_ASSERT_NN(chunk->segments);
    ZL_ERR_IF_ERR(sddl2_process_segment_chunk(
            sctx,
            destination,
            chunk->segments,
            chunk->count,
            chunk->bytes,
            chunk->startStreamId));

    chunk->count = 0;
    chunk->bytes = 0;
    return ZL_returnSuccess();
}

static ZL_Report sddl2_get_chunk_split_unit_size(
        ZL_Segmenter* sctx,
        SDDL2_Type type)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    if (type.kind == SDDL2_TYPE_STRUCTURE) {
        ZL_ERR_IF_NULL(
                type.struct_data,
                GENERIC,
                "SDDL2 structure segment has null type metadata");
        ZL_ERR_IF_EQ(
                type.struct_data->total_size_bytes,
                0,
                GENERIC,
                "SDDL2 structure segment has zero-sized split units");
        return ZL_returnValue(type.struct_data->total_size_bytes);
    }

    // Primitive kinds, including BYTES, split on their element size.
    // BYTES intentionally uses 1-byte units so raw byte spans can chunk.
    SDDL2_RESULT_OF(size_t) const unitBytes_report = SDDL2_kind_size(type.kind);
    ZL_ERR_IF(
            SDDL2_isError(unitBytes_report),
            GENERIC,
            "Failed to derive a split unit size for SDDL2 segment chunking");
    return ZL_returnValue(SDDL2_value(unitBytes_report));
}

typedef struct {
    ZL_GraphID destination;
    size_t remainingInputBytes;
    size_t chunkSizeLimit;
    ZL_IDType nextStreamId;
    SDDL2_ChunkAccumulator chunk;
} SDDL2_ChunkPlanner;

static ZL_Report sddl2_execute_vm_segments(
        ZL_Segmenter* sctx,
        const void* bytecode,
        size_t bytecode_size,
        const void* input_data,
        size_t input_size,
        SDDL2_Segment_list* out_segments)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    ZL_ERR_IF_NULL(out_segments, GENERIC);

    SDDL2_Segment_list_init(
            out_segments, sddl2_segmenter_arena_allocator, sctx);
    const SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input_data, input_size, out_segments);
    if (err != SDDL2_OK) {
        SDDL2_Segment_list_destroy(out_segments);
        return sddl2_segmenter_error_to_report(sctx, err);
    }

    return ZL_returnSuccess();
}

static ZL_Report sddl2_resolve_chunk_size_limit(
        ZL_Segmenter* sctx,
        ZL_IntParam chunkSizeParam,
        size_t* out_chunkSizeLimit)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    ZL_ERR_IF_NULL(out_chunkSizeLimit, GENERIC);

    size_t chunkSizeLimit = SDDL2_DEFAULT_CHUNK_BYTE_SIZE;
    if (chunkSizeParam.paramId != ZL_LP_INVALID_PARAMID) {
        ZL_ERR_IF_LT(
                chunkSizeParam.paramValue,
                0,
                parameter_invalid,
                "SDDL2 chunk size must be non-negative when provided");
        if (chunkSizeParam.paramValue > 0) {
            chunkSizeLimit = (size_t)chunkSizeParam.paramValue;
        }
    }
    if (ZL_Segmenter_getCParam(sctx, ZL_CParam_formatVersion)
        < ZL_CHUNK_VERSION_MIN) {
        chunkSizeLimit = SIZE_MAX;
    }

    *out_chunkSizeLimit = chunkSizeLimit;
    return ZL_returnSuccess();
}

static ZL_Report sddl2_init_chunk_planner(
        ZL_Segmenter* sctx,
        ZL_GraphID destination,
        size_t input_size,
        size_t chunkSizeLimit,
        size_t maxSegments,
        SDDL2_ChunkPlanner* out_planner)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    ZL_ERR_IF_NULL(out_planner, GENERIC);

    /*
     * Reused as a chunk-local replay-slice buffer. Runtime graph params are
     * consumed synchronously by ZL_Segmenter_processChunk(), so the buffer only
     * needs to remain valid until each call returns.
     */
    SDDL2_ReplaySegment* replaySegments =
            (SDDL2_ReplaySegment*)ZL_Segmenter_getScratchSpace(
                    sctx, maxSegments * sizeof(*replaySegments));
    ZL_ERR_IF_NULL(replaySegments, allocation);

    *out_planner = (SDDL2_ChunkPlanner){
        .destination        = destination,
        .remainingInputBytes = input_size,
        .chunkSizeLimit      = chunkSizeLimit,
        .nextStreamId        = 0,
        .chunk               = {
            .segments      = replaySegments,
            .capacity      = maxSegments,
            .count         = 0,
            .bytes         = 0,
            .startStreamId = 0,
        },
    };
    return ZL_returnSuccess();
}

static ZL_Report sddl2_reserve_segment_stream_id(
        ZL_Segmenter* sctx,
        SDDL2_ChunkPlanner* planner,
        SDDL2_Type type,
        ZL_IDType* out_segmentStreamId)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    ZL_ERR_IF_NULL(planner, GENERIC);
    ZL_ERR_IF_NULL(out_segmentStreamId, GENERIC);

    const ZL_IDType maxStreamCount = (ZL_IDType)INT_MAX;
    size_t segmentStreams          = 0;
    ZL_ERR_IF_NOT(
            sddl2_try_count_primitive_fields(type, &segmentStreams)
                    && segmentStreams != 0,
            GENERIC,
            "Failed to count replay streams for SDDL2 segment");
    ZL_ERR_IF_GT(
            segmentStreams,
            maxStreamCount,
            internalBuffer_tooSmall,
            "SDDL2 chunking produced too many logical output streams");

    ZL_IDType const segmentStreamCount = (ZL_IDType)segmentStreams;
    ZL_ERR_IF_GT(
            planner->nextStreamId,
            maxStreamCount - segmentStreamCount,
            internalBuffer_tooSmall,
            "SDDL2 chunking exceeded clustering tag capacity");

    *out_segmentStreamId = planner->nextStreamId;
    planner->nextStreamId += segmentStreamCount;
    return ZL_returnSuccess();
}

static ZL_Report sddl2_plan_split_segment_chunks(
        ZL_Segmenter* sctx,
        SDDL2_ChunkPlanner* planner,
        const SDDL2_Segment* seg,
        ZL_IDType segmentStreamId)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    ZL_ERR_IF_NULL(planner, GENERIC);
    ZL_ERR_IF_NULL(seg, GENERIC);

    ZL_TRY_LET(
            size_t,
            elementBytes,
            sddl2_get_chunk_split_unit_size(sctx, seg->type));
    ZL_ERR_IF_NE(
            seg->size_bytes % elementBytes,
            0,
            nodeParameter_invalidValue,
            "SDDL2 segment size is not aligned to its split unit size");
    ZL_ERR_IF_GT(
            elementBytes,
            planner->chunkSizeLimit,
            parameter_invalid,
            "SDDL2 chunk size %zu cannot fit one element of size %zu",
            planner->chunkSizeLimit,
            elementBytes);

    size_t const maxElementsPerChunk = planner->chunkSizeLimit / elementBytes;
    ZL_ASSERT_GT(maxElementsPerChunk, 0);
    size_t const maxSliceBytes = maxElementsPerChunk * elementBytes;

    size_t remainingSegmentBytes = seg->size_bytes;
    while (remainingSegmentBytes > 0) {
        size_t sliceBytes = remainingSegmentBytes;
        if (sliceBytes > maxSliceBytes) {
            sliceBytes = maxSliceBytes;
        }

        ZL_ERR_IF_ERR(sddl2_append_chunk_replay_slice(
                sctx, &planner->chunk, segmentStreamId, seg->type, sliceBytes));
        remainingSegmentBytes -= sliceBytes;

        if (remainingSegmentBytes > 0) {
            ZL_ERR_IF_ERR(sddl2_flush_chunk_segments(
                    sctx, planner->destination, &planner->chunk));
        }
    }

    return ZL_returnSuccess();
}

static ZL_Report sddl2_plan_segment_chunk(
        ZL_Segmenter* sctx,
        SDDL2_ChunkPlanner* planner,
        const SDDL2_Segment* seg)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    ZL_ERR_IF_NULL(planner, GENERIC);
    ZL_ERR_IF_NULL(seg, GENERIC);
    ZL_ERR_IF_GT(
            seg->size_bytes,
            planner->remainingInputBytes,
            nodeParameter_invalidValue,
            "split instructions do not map exactly the entire input");
    planner->remainingInputBytes -= seg->size_bytes;

    ZL_IDType segmentStreamId = 0;
    ZL_ERR_IF_ERR(sddl2_reserve_segment_stream_id(
            sctx, planner, seg->type, &segmentStreamId));

    if (planner->chunkSizeLimit == SIZE_MAX
        || seg->size_bytes <= planner->chunkSizeLimit) {
        const bool wouldExceedChunk = planner->chunk.count > 0
                && planner->chunkSizeLimit != SIZE_MAX
                && (planner->chunk.bytes >= planner->chunkSizeLimit
                    || seg->size_bytes
                            > planner->chunkSizeLimit - planner->chunk.bytes);
        if (wouldExceedChunk) {
            ZL_ERR_IF_ERR(sddl2_flush_chunk_segments(
                    sctx, planner->destination, &planner->chunk));
        }

        return sddl2_append_chunk_replay_slice(
                sctx,
                &planner->chunk,
                segmentStreamId,
                seg->type,
                seg->size_bytes);
    }

    ZL_ERR_IF_ERR(sddl2_flush_chunk_segments(
            sctx, planner->destination, &planner->chunk));
    return sddl2_plan_split_segment_chunks(sctx, planner, seg, segmentStreamId);
}

static ZL_Report sddl2_plan_segment_chunks(
        ZL_Segmenter* sctx,
        const SDDL2_Segment_list* segments,
        size_t input_size,
        size_t chunkSizeLimit,
        ZL_GraphID destination)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    ZL_ERR_IF_NULL(segments, GENERIC);

    if (segments->count == 0) {
        return ZL_Segmenter_processChunk(
                sctx, &input_size, 1, ZL_GRAPH_STORE, NULL);
    }

    /*
     * Initialize eagerly so static analysis can see all planner fields are
     * defined even though the real setup happens through the helper below.
     */
    SDDL2_ChunkPlanner planner = { 0 };
    ZL_ERR_IF_ERR(sddl2_init_chunk_planner(
            sctx,
            destination,
            input_size,
            chunkSizeLimit,
            segments->count,
            &planner));

    for (size_t i = 0; i < segments->count; i++) {
        ZL_ERR_IF_ERR(
                sddl2_plan_segment_chunk(sctx, &planner, &segments->items[i]));
    }

    ZL_ERR_IF_NE(
            planner.remainingInputBytes,
            0,
            nodeParameter_invalidValue,
            "split instructions do not map exactly the entire input");
    ZL_ERR_IF_ERR(sddl2_flush_chunk_segments(
            sctx, planner.destination, &planner.chunk));

    return ZL_returnSuccess();
}

ZL_Report SDDL2_segment(ZL_Segmenter* sctx) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    ZL_ERR_IF_NE(ZL_Segmenter_numInputs(sctx), 1, node_invalid_input);

    const ZL_Input* input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ASSERT_NN(input);
    ZL_ERR_IF_NE(ZL_Input_type(input), ZL_Type_serial, node_invalid_input);

    const ZL_RefParam bytecodeParam =
            ZL_Segmenter_getLocalRefParam(sctx, SDDL2_BYTECODE_PARAM);
    ZL_ERR_IF_NE(
            bytecodeParam.paramId, SDDL2_BYTECODE_PARAM, parameter_invalid);
    if (bytecodeParam.paramRef == NULL) {
        ZL_ERR_IF_NE(bytecodeParam.paramSize, 0, parameter_invalid);
    }

    const ZL_IntParam chunkSizeParam =
            ZL_Segmenter_getLocalIntParam(sctx, SDDL2_CHUNK_BYTE_SIZE_PARAM);
    ZL_GraphID destination = ZL_GRAPH_COMPRESS_GENERIC;
    ZL_ERR_IF_ERR(sddl2_get_segmenter_destination_graph(sctx, &destination));

    const void* input_data  = ZL_Input_ptr(input);
    const size_t input_size = ZL_Input_contentSize(input);

    size_t chunkSizeLimit = 0;
    ZL_ERR_IF_ERR(sddl2_resolve_chunk_size_limit(
            sctx, chunkSizeParam, &chunkSizeLimit));

    SDDL2_Segment_list segments;
    ZL_ERR_IF_ERR(sddl2_execute_vm_segments(
            sctx,
            bytecodeParam.paramRef,
            bytecodeParam.paramSize,
            input_data,
            input_size,
            &segments));

    ZL_Report const planReport = sddl2_plan_segment_chunks(
            sctx, &segments, input_size, chunkSizeLimit, destination);
    SDDL2_Segment_list_destroy(&segments);
    return planReport;
}

ZL_Report SDDL2_replayChunk(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
        ZL_NOEXCEPT_FUNC_PTR
{
    ZL_ASSERT_NN(graph);
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    ZL_ERR_IF_NE(nbInputs, 1, graph_invalidNumInputs);
    ZL_ASSERT_NN(inputs);
    ZL_ASSERT_NN(inputs[0]);
    ZL_Edge* const input_edge = inputs[0];

    const ZL_Input* const input = ZL_Edge_getData(input_edge);
    ZL_ASSERT_NN(input);
    ZL_ERR_IF_NE(ZL_Input_type(input), ZL_Type_serial, inputType_unsupported);

    const SDDL2_ReplaySegment* segments = NULL;
    size_t num_segments                 = 0;
    ZL_ERR_IF_ERR(sddl2_get_replay_segments(graph, &segments, &num_segments));

    return sddl2_replay_segments(graph, input_edge, segments, num_segments);
}
