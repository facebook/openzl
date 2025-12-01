// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_CONVERSION_H
#define ZSTRONG_CODECS_CONVERSION_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_graph_api.h"
#include "openzl/zl_graphs.h"
#include "openzl/zl_localParams.h"
#include "openzl/zl_nodes.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_stream.h"

#if defined(__cplusplus)
extern "C" {
#endif

// ==========================================
// Conversion operations
// ==========================================

// Conversion operations generally take 1 input and produce 1 output with same
// content but different type tag. Stream types of input and output are
// unambiguous from the conversion name.

#define ZL_CONVERT_SERIAL_TO_STRUCT_SIZE_PID 1

/**
 * Converts serial data to a structs of a fixed size controlled by the
 * `ZL_CONVERT_SERIAL_TO_STRUCT_SIZE_PID` parameter. The struct size
 * must be set to a multiple of the serial data content size.
 *
 * Input: Serial
 * Output: Struct
 */
#define ZL_NODE_CONVERT_SERIAL_TO_STRUCT           \
    (ZL_NodeID)                                    \
    {                                              \
        ZL_StandardNodeID_convert_serial_to_struct \
    }

/**
 * Helper function to parameterize the `ZL_NODE_CONVERT_SERIAL_TO_STRUCT` node
 * with the struct size.
 *
 * Input: Serial
 * Output: Struct
 */
ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_parameterizeConvertSerialToStructNode(
        ZL_Compressor* compressor,
        int structSize);

/**
 * Converts serial data to 2-byte structs
 *
 * Input: Serial
 * Output: Struct
 */
#define ZL_NODE_CONVERT_SERIAL_TO_STRUCT2           \
    (ZL_NodeID)                                     \
    {                                               \
        ZL_StandardNodeID_convert_serial_to_struct2 \
    }

/**
 * Converts serial data to 4-byte structs
 *
 * Input: Serial
 * Output: Struct
 */
#define ZL_NODE_CONVERT_SERIAL_TO_STRUCT4           \
    (ZL_NodeID)                                     \
    {                                               \
        ZL_StandardNodeID_convert_serial_to_struct4 \
    }

/**
 * Converts serial data to 8-byte structs
 *
 * Input: Serial
 * Output: Struct
 */
#define ZL_NODE_CONVERT_SERIAL_TO_STRUCT8           \
    (ZL_NodeID)                                     \
    {                                               \
        ZL_StandardNodeID_convert_serial_to_struct8 \
    }

/**
 * Converts struct data to serial data
 *
 * @note Eligible as a transparent conversion operation when invoking a
 * successor graph.
 *
 * Input: Struct
 * Output: Serial
 */
#define ZL_NODE_CONVERT_STRUCT_TO_SERIAL           \
    (ZL_NodeID)                                    \
    {                                              \
        ZL_StandardNodeID_convert_struct_to_serial \
    }

/**
 * Converts from struct data of width 1, 2, 4, or 8 in little-endian format to
 * numeric data.
 *
 * Input: Struct
 * Output: Numeric
 */
#define ZL_NODE_CONVERT_STRUCT_TO_NUM_LE           \
    (ZL_NodeID)                                    \
    {                                              \
        ZL_StandardNodeID_convert_struct_to_num_le \
    }

/**
 * Converts from struct data of width 1, 2, 4, or 8 in big-endian format to
 * numeric data.
 *
 * Input: Struct
 * Output: Numeric
 */
#define ZL_NODE_CONVERT_STRUCT_TO_NUM_BE           \
    (ZL_NodeID)                                    \
    {                                              \
        ZL_StandardNodeID_convert_struct_to_num_be \
    }

/**
 * Converts from numeric data to struct data of the same width in little-endian
 * format.
 *
 * @note Eligible as a transparent conversion operation when invoking a
 * successor graph.
 *
 * Input: Numeric
 * Output Struct
 */
#define ZL_NODE_CONVERT_NUM_TO_STRUCT_LE           \
    (ZL_NodeID)                                    \
    {                                              \
        ZL_StandardNodeID_convert_num_to_struct_le \
    }

/**
 * Convert from serial data to 8-bit numeric data.
 *
 * Input: Serial
 * Output: Numeric
 */
#define ZL_NODE_CONVERT_SERIAL_TO_NUM8           \
    (ZL_NodeID)                                  \
    {                                            \
        ZL_StandardNodeID_convert_serial_to_num8 \
    }

/// @see ZL_NODE_CONVERT_SERIAL_TO_NUM8
#define ZL_NODE_CONVERT_SERIAL_TO_NUM_LE8 ZL_NODE_CONVERT_SERIAL_TO_NUM8

/**
 * Convert from serial data to 16-bit little-endian numeric data.
 *
 * Input: Serial
 * Output: Numeric
 */
#define ZL_NODE_CONVERT_SERIAL_TO_NUM_LE16           \
    (ZL_NodeID)                                      \
    {                                                \
        ZL_StandardNodeID_convert_serial_to_num_le16 \
    }

/**
 * Convert from serial data to 32-bit little-endian numeric data.
 *
 * Input: Serial
 * Output: Numeric
 */
#define ZL_NODE_CONVERT_SERIAL_TO_NUM_LE32           \
    (ZL_NodeID)                                      \
    {                                                \
        ZL_StandardNodeID_convert_serial_to_num_le32 \
    }

/**
 * Convert from serial data to 64-bit little-endian numeric data.
 *
 * Input: Serial
 * Output: Numeric
 */
#define ZL_NODE_CONVERT_SERIAL_TO_NUM_LE64           \
    (ZL_NodeID)                                      \
    {                                                \
        ZL_StandardNodeID_convert_serial_to_num_le64 \
    }

/// @see ZL_NODE_CONVERT_SERIAL_TO_NUM8
#define ZL_NODE_CONVERT_SERIAL_TO_NUM_BE8 ZL_NODE_CONVERT_SERIAL_TO_NUM8

/**
 * Convert from serial data to 16-bit big-endian numeric data.
 *
 * Input: Serial
 * Output: Numeric
 */
#define ZL_NODE_CONVERT_SERIAL_TO_NUM_BE16           \
    (ZL_NodeID)                                      \
    {                                                \
        ZL_StandardNodeID_convert_serial_to_num_be16 \
    }

/**
 * Convert from serial data to 32-bit big-endian numeric data.
 *
 * Input: Serial
 * Output: Numeric
 */
#define ZL_NODE_CONVERT_SERIAL_TO_NUM_BE32           \
    (ZL_NodeID)                                      \
    {                                                \
        ZL_StandardNodeID_convert_serial_to_num_be32 \
    }

/**
 * Convert from serial data to 64-bit big-endian numeric data.
 *
 * Input: Serial
 * Output: Numeric
 */
#define ZL_NODE_CONVERT_SERIAL_TO_NUM_BE64           \
    (ZL_NodeID)                                      \
    {                                                \
        ZL_StandardNodeID_convert_serial_to_num_be64 \
    }

/**
 * Convert from serial to `bitWidth`-bit little-endian numeric data.
 *
 * @pre bitWidth must be 8, 16, 32, or 64.
 *
 * Input: Serial
 * Output: Numeric
 */
ZL_NodeID ZL_Node_convertSerialToNumLE(size_t bitWidth);

/**
 * Convert from serial to `bitWidth`-bit big-endian numeric data.
 *
 * @pre bitWidth must be 8, 16, 32, or 64.
 *
 * Input: Serial
 * Output: Numeric
 */
ZL_NodeID ZL_Node_convertSerialToNumBE(size_t bitWidth);

/**
 * Convert numeric data to serial data in little-endian format.
 *
 * @note Eligible as a transparent conversion operation when invoking a
 * successor graph.
 *
 * Input: Numeric
 * Output: Serial
 */
#define ZL_NODE_CONVERT_NUM_TO_SERIAL_LE           \
    (ZL_NodeID)                                    \
    {                                              \
        ZL_StandardNodeID_convert_num_to_serial_le \
    }

// Conversion from Serial to String:
// This conversion requires providing an additional array of string lengths.
// The array is produced at runtime by a parser function,
// ZL_SetStringLensParserFn, which must be provided as a function pointer at
// Graph construction time. The parser function receives the ZL_Data as
// input, and must return an array of lengths, which sum must be equal to the
// byte size of the input. The parser function is allowed to fail, by returning
// `{NULL, !0}`, in which case the transform's execution also fails (returns an
// error). The declaration logic is implemented by
// ZL_Compressor_registerConvertSerialToStringNode().
typedef struct {
    const uint32_t* stringLens;
    size_t nbStrings;
} ZL_SetStringLensInstructions;
typedef struct ZL_SetStringLensState_s ZL_SetStringLensState;
typedef ZL_SetStringLensInstructions (*ZL_SetStringLensParserFn)(
        ZL_SetStringLensState* state,
        const ZL_Input* in);
// Capabilities for ZL_SetStringLensParserFn:
// - opaque ptr: optional, useful to transmit some information to the parser.
//               note that it's a `const` pointer: it can't be used to export
//               information.
// - allocation: useful to request memory for temporary structures.
//               required to allocate the resulting array.
// It's recommended to only employ ZL_SetStringLensState_malloc() for allocation
// needs. Note that memory allocated with ZL_SetStringLensState_malloc() cannot
// be freed explicitly, it will be freed automatically at the end of the
// Transform's execution.
const void* ZL_SetStringLensState_getOpaquePtr(
        const ZL_SetStringLensState* state);
void* ZL_SetStringLensState_malloc(ZL_SetStringLensState* state, size_t size);

ZL_NodeID ZL_Compressor_registerConvertSerialToStringNode(
        ZL_Compressor* cgraph,
        ZL_SetStringLensParserFn f,
        const void* opaque);

// This variant is invoked within a function Graph context.
// It reads an array of string lengths determined at runtime
ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runConvertSerialToStringNode(
        ZL_Edge* sctx,
        const uint32_t* stringLens,
        size_t nbString);

// The reverse operation, unbundling string, actually generates 2 output
// streams: the first one (serialized) with the concatenation of all strings,
// and the second one (numeric) for the array of string sizes.
#define ZL_NODE_SEPARATE_STRING_COMPONENTS \
    ZL_MAKE_NODE_ID(ZL_StandardNodeID_separate_string_components)

// Old names for nodes
enum { ZL_trlip_tokenSize = 1 };
#define ZL_CREATENODE_CONVERT_SERIAL_TO_TOKEN(g, l)                       \
    ZL_Compressor_cloneNode(                                              \
            g,                                                            \
            ZL_NODE_CONVERT_SERIAL_TO_STRUCT,                             \
            &(const ZL_LocalParams){                                      \
                    { &(const ZL_IntParam){ ZL_trlip_tokenSize, l }, 1 }, \
                    { NULL, 0 },                                          \
                    { NULL, 0 } })

#define ZL_NODE_CONVERT_SERIAL_TO_TOKENX ZL_NODE_CONVERT_SERIAL_TO_STRUCT
#define ZL_NODE_CONVERT_SERIAL_TO_TOKEN2 ZL_NODE_CONVERT_SERIAL_TO_STRUCT2
#define ZL_NODE_CONVERT_SERIAL_TO_TOKEN4 ZL_NODE_CONVERT_SERIAL_TO_STRUCT4
#define ZL_NODE_CONVERT_SERIAL_TO_TOKEN8 ZL_NODE_CONVERT_SERIAL_TO_STRUCT8
#define ZL_NODE_CONVERT_TOKEN_TO_SERIAL ZL_NODE_CONVERT_STRUCT_TO_SERIAL
#define ZL_NODE_INTERPRET_TOKEN_AS_LE ZL_NODE_CONVERT_STRUCT_TO_NUM_LE
#define ZL_NODE_CONVERT_NUM_TO_TOKEN ZL_NODE_CONVERT_NUM_TO_STRUCT_LE
#define ZL_NODE_INTERPRET_AS_LE8 ZL_NODE_CONVERT_SERIAL_TO_NUM8
#define ZL_NODE_INTERPRET_AS_LE16 ZL_NODE_CONVERT_SERIAL_TO_NUM_LE16
#define ZL_NODE_INTERPRET_AS_LE32 ZL_NODE_CONVERT_SERIAL_TO_NUM_LE32
#define ZL_NODE_INTERPRET_AS_LE64 ZL_NODE_CONVERT_SERIAL_TO_NUM_LE64

ZL_INLINE ZL_NodeID ZL_Node_interpretAsLE(size_t bitWidth)
{
    return ZL_Node_convertSerialToNumLE(bitWidth);
}

#define ZL_NODE_CONVERT_NUM_TO_SERIAL ZL_NODE_CONVERT_NUM_TO_SERIAL_LE

#if defined(__cplusplus)
}
#endif

#endif
