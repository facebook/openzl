// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_SAFETENSORS_SAFETENSORS_LEXER_H
#define CUSTOM_PARSERS_SAFETENSORS_SAFETENSORS_LEXER_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

/**
 * Lexer for safetensors files.
 *
 * Layout: an 8-byte little-endian header size N, a JSON object of N bytes, then
 * the data section. Each JSON member describes one tensor:
 *   "name": {"dtype": "BF16", "shape": [rows, cols], "data_offsets": [b, e]}
 * where offsets are relative to the start of the data section. The optional
 * "__metadata__" member is skipped.
 */

#define ZL_SAFETENSORS_MAX_DIMS 8
#define ZL_SAFETENSORS_MAX_HEADER_SIZE ((uint64_t)100 * 1000 * 1000)
/// Headers describing more tensors are rejected, which bounds the memory needed
/// to parse them.
#define ZL_SAFETENSORS_MAX_TENSORS ((size_t)1 << 18)

typedef enum {
    ZL_SafetensorsDtype_unknown = 0, // unrecognized dtype: handled as raw bytes
    ZL_SafetensorsDtype_bool,
    ZL_SafetensorsDtype_u8,
    ZL_SafetensorsDtype_i8,
    ZL_SafetensorsDtype_f8_e4m3,
    ZL_SafetensorsDtype_f8_e5m2,
    ZL_SafetensorsDtype_i16,
    ZL_SafetensorsDtype_u16,
    ZL_SafetensorsDtype_f16,
    ZL_SafetensorsDtype_bf16,
    ZL_SafetensorsDtype_i32,
    ZL_SafetensorsDtype_u32,
    ZL_SafetensorsDtype_f32,
    ZL_SafetensorsDtype_i64,
    ZL_SafetensorsDtype_u64,
    ZL_SafetensorsDtype_f64,
} ZL_SafetensorsDtype;

typedef enum {
    ZL_SafetensorsTokenType_header, // header size and JSON header
    ZL_SafetensorsTokenType_tensor,
    ZL_SafetensorsTokenType_gap, // data not covered by any tensor
} ZL_SafetensorsTokenType;

typedef struct {
    /// Pointer to the beginning of the token in the source buffer.
    const char* ptr;
    /// Size of the token in bytes, never 0.
    size_t size;
    ZL_SafetensorsTokenType type;

    /// The following fields are only valid for tensor tokens.

    const char* name; // points into the header, not NUL-terminated, unescaped
    size_t nameSize;
    ZL_SafetensorsDtype dtype;
    uint32_t ndims;
    uint64_t shape[ZL_SAFETENSORS_MAX_DIMS];
} ZL_SafetensorsToken;

/// @returns the element size in bytes, or 1 for unknown dtypes.
size_t ZL_Safetensors_dtypeSize(ZL_SafetensorsDtype dtype);

/**
 * Lexes the safetensors file in @p src into tokens that cover it in order: the
 * header, then tensors separated by gaps. Zero-sized tensors produce no token.
 *
 * Tensors must not overlap and must fit within @p src, and there must be at
 * most ZL_SAFETENSORS_MAX_TENSORS of them. A tensor whose byte size does not
 * match its dtype and shape is reported with ZL_SafetensorsDtype_unknown.
 *
 * When @p tokens is NULL, only the header size and syntax are validated,
 * without allocating anything, so that the caller can size @p tokens before
 * lexing again.
 *
 * @returns the number of tokens written to @p tokens, or when @p tokens is
 *          NULL, the largest number of tokens the file can produce. Errors are
 *          graph_parser_malformedInput for a malformed file, and
 *          graph_parser_unhandledInput for a valid one that is not supported.
 */
ZL_Report ZL_Safetensors_lex(
        const void* src,
        size_t srcSize,
        ZL_SafetensorsToken* tokens,
        size_t tokensCapacity,
        ZL_ErrorContext* errCtx);

ZL_END_C_DECLS

#endif
