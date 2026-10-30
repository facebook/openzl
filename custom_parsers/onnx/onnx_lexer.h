// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_ONNX_ONNX_LEXER_H
#define CUSTOM_PARSERS_ONNX_ONNX_LEXER_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

/**
 * Lexer for ONNX model files.
 *
 * An ONNX file is a serialized ModelProto protobuf message, without a magic
 * number. Weights are TensorProto messages: the initializers of the graph, the
 * tensors of node attributes (Constant nodes), those of subgraphs (If, Loop
 * and Scan bodies), and the values and indices of sparse tensors. Their bytes
 * are in the raw_data field, little-endian and row-major, with the element
 * type in data_type and the shape in dims, outermost first. Tensors stored in
 * typed fields (float_data, int64_data...) or in external files have no
 * raw_data: their bytes, if any, stay in the gaps. Field numbers are those of
 * onnx/onnx.proto, checked against ONNX 1.20.1.
 */

#define ZL_ONNX_MAX_DIMS 8
/// Files holding more tensors with raw_data are rejected, which bounds the
/// memory needed to parse them.
#define ZL_ONNX_MAX_TENSORS ((size_t)1 << 18)
/// Graphs nested deeper than this are not walked: their tensors stay in the
/// gaps.
#define ZL_ONNX_MAX_GRAPH_DEPTH 8

/// TensorProto.DataType values.
typedef enum {
    ZL_ONNXDataType_undefined      = 0,
    ZL_ONNXDataType_float          = 1,
    ZL_ONNXDataType_uint8          = 2,
    ZL_ONNXDataType_int8           = 3,
    ZL_ONNXDataType_uint16         = 4,
    ZL_ONNXDataType_int16          = 5,
    ZL_ONNXDataType_int32          = 6,
    ZL_ONNXDataType_int64          = 7,
    ZL_ONNXDataType_string         = 8,
    ZL_ONNXDataType_bool           = 9,
    ZL_ONNXDataType_float16        = 10,
    ZL_ONNXDataType_double         = 11,
    ZL_ONNXDataType_uint32         = 12,
    ZL_ONNXDataType_uint64         = 13,
    ZL_ONNXDataType_complex64      = 14,
    ZL_ONNXDataType_complex128     = 15,
    ZL_ONNXDataType_bfloat16       = 16,
    ZL_ONNXDataType_float8e4m3fn   = 17,
    ZL_ONNXDataType_float8e4m3fnuz = 18,
    ZL_ONNXDataType_float8e5m2     = 19,
    ZL_ONNXDataType_float8e5m2fnuz = 20,
    ZL_ONNXDataType_uint4          = 21,
    ZL_ONNXDataType_int4           = 22,
    ZL_ONNXDataType_float4e2m1     = 23,
    ZL_ONNXDataType_float8e8m0     = 24,
    ZL_ONNXDataType_uint2          = 25,
    ZL_ONNXDataType_int2           = 26,
} ZL_ONNXDataType;

typedef enum {
    ZL_ONNXTokenType_header, // everything before the first tensor
    ZL_ONNXTokenType_tensor, // the raw_data of a tensor
    ZL_ONNXTokenType_gap,    // protobuf fields between and after tensors
} ZL_ONNXTokenType;

typedef struct {
    /// Pointer to the beginning of the token in the source buffer.
    const char* ptr;
    /// Size of the token in bytes, never 0.
    size_t size;
    ZL_ONNXTokenType type;

    /// The following fields are only valid for tensor tokens.

    const char* name; // points into the file, not NUL-terminated
    size_t nameSize;
    uint32_t dataType; // possibly not a ZL_ONNXDataType
    uint32_t ndims;
    uint64_t dims[ZL_ONNX_MAX_DIMS]; // outermost first
} ZL_ONNXToken;

/// @returns the size in bits of an element of @p dataType, or 0 for unknown
/// types and strings. Elements smaller than a byte are packed.
size_t ZL_ONNX_dataTypeBits(uint32_t dataType);

/**
 * Lexes the ONNX file in @p src into tokens that cover it in order: the
 * header, then tensors separated by gaps. Tensors without raw_data, or with an
 * empty one, produce no token.
 *
 * A tensor whose raw_data does not hold exactly the elements of its dims and
 * data_type, or which has more than ZL_ONNX_MAX_DIMS dimensions, is reported
 * with ZL_ONNXDataType_undefined. There must be at most ZL_ONNX_MAX_TENSORS
 * tensors with raw_data.
 *
 * When @p tokens is NULL, the file is validated and its tensors counted,
 * without writing anything, so that the caller can size @p tokens before
 * lexing again.
 *
 * @returns the number of tokens written to @p tokens, or when @p tokens is
 *          NULL, the largest number of tokens the file can produce. Errors are
 *          graph_parser_malformedInput for an input that is not an ONNX file,
 *          and graph_parser_unhandledInput for a valid one that is not
 *          supported.
 */
ZL_Report ZL_ONNX_lex(
        const void* src,
        size_t srcSize,
        ZL_ONNXToken* tokens,
        size_t tokensCapacity,
        ZL_ErrorContext* errCtx);

ZL_END_C_DECLS

#endif
