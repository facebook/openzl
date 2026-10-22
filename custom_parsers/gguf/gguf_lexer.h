// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_GGUF_GGUF_LEXER_H
#define CUSTOM_PARSERS_GGUF_GGUF_LEXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

/**
 * Lexer for GGUF files (llama.cpp model format).
 *
 * Layout, little-endian: "GGUF", version (u32), tensor count (u64), metadata
 * count (u64), metadata key/value pairs, tensor infos, padding to the
 * alignment, then the data section. Each tensor info holds a name, up to 4
 * dimensions (innermost first), a ggml type and an offset relative to the data
 * section. Missing dimensions count as 1, so a tensor without dimensions holds
 * one element. The alignment is the "general.alignment" metadata, 32 by
 * default.
 */

#define ZL_GGUF_MAX_DIMS 4
/// Headers describing more tensors are rejected, which bounds the memory needed
/// to parse them.
#define ZL_GGUF_MAX_TENSORS ((size_t)1 << 18)

/// ggml tensor types (ggml_type), as stored in tensor infos. Checked against
/// ggml/include/ggml.h of llama.cpp b11046 (upstream 60081bb2b5b3).
typedef enum {
    ZL_GGUFType_f32     = 0,
    ZL_GGUFType_f16     = 1,
    ZL_GGUFType_q4_0    = 2,
    ZL_GGUFType_q4_1    = 3,
    ZL_GGUFType_q5_0    = 6,
    ZL_GGUFType_q5_1    = 7,
    ZL_GGUFType_q8_0    = 8,
    ZL_GGUFType_q8_1    = 9,
    ZL_GGUFType_q2_k    = 10,
    ZL_GGUFType_q3_k    = 11,
    ZL_GGUFType_q4_k    = 12,
    ZL_GGUFType_q5_k    = 13,
    ZL_GGUFType_q6_k    = 14,
    ZL_GGUFType_q8_k    = 15,
    ZL_GGUFType_iq2_xxs = 16,
    ZL_GGUFType_iq2_xs  = 17,
    ZL_GGUFType_iq3_xxs = 18,
    ZL_GGUFType_iq1_s   = 19,
    ZL_GGUFType_iq4_nl  = 20,
    ZL_GGUFType_iq3_s   = 21,
    ZL_GGUFType_iq2_s   = 22,
    ZL_GGUFType_iq4_xs  = 23,
    ZL_GGUFType_i8      = 24,
    ZL_GGUFType_i16     = 25,
    ZL_GGUFType_i32     = 26,
    ZL_GGUFType_i64     = 27,
    ZL_GGUFType_f64     = 28,
    ZL_GGUFType_iq1_m   = 29,
    ZL_GGUFType_bf16    = 30,
    ZL_GGUFType_tq1_0   = 34,
    ZL_GGUFType_tq2_0   = 35,
    ZL_GGUFType_mxfp4   = 39,
    ZL_GGUFType_nvfp4   = 40,
    ZL_GGUFType_q1_0    = 41,
    ZL_GGUFType_q2_0    = 42,
    ZL_GGUFType_limit, // one more than the largest type, keep last
} ZL_GGUFType;

typedef enum {
    ZL_GGUFTokenType_header, // everything before the data section
    ZL_GGUFTokenType_tensor,
    ZL_GGUFTokenType_gap, // data not covered by any tensor, e.g. alignment
} ZL_GGUFTokenType;

typedef struct {
    /// Pointer to the beginning of the token in the source buffer.
    const char* ptr;
    /// Size of the token in bytes, never 0.
    size_t size;
    ZL_GGUFTokenType type;

    /// The following fields are only valid for tensor tokens.

    const char* name; // points into the header, not NUL-terminated
    size_t nameSize;
    uint32_t ggmlType; // possibly not a ZL_GGUFType
    uint32_t ndims;
    uint64_t ne[ZL_GGUF_MAX_DIMS]; // ne[0] is the number of elements of a row
} ZL_GGUFToken;

/// Gets the number of elements and bytes of a block of @p type.
/// @returns false if @p type is unknown.
bool ZL_GGUF_typeLayout(uint32_t type, size_t* blockElts, size_t* blockBytes);

/**
 * Lexes the GGUF file in @p src into tokens that cover it in order: the header,
 * then tensors separated by gaps. A tensor whose type is unknown, or whose rows
 * are not made of whole blocks, has no known size and produces no token: its
 * bytes are left to the gaps.
 *
 * Tensors must not overlap and must fit within @p src, and there must be at
 * most ZL_GGUF_MAX_TENSORS of them.
 *
 * When @p tokens is NULL, the metadata are validated without allocating
 * anything, so that the caller can size @p tokens before lexing again.
 *
 * @returns the number of tokens written to @p tokens, or when @p tokens is
 *          NULL, the largest number of tokens the file can produce. Errors are
 *          graph_parser_malformedInput for a malformed file, and
 *          graph_parser_unhandledInput for a valid one that is not supported.
 */
ZL_Report ZL_GGUF_lex(
        const void* src,
        size_t srcSize,
        ZL_GGUFToken* tokens,
        size_t tokensCapacity,
        ZL_ErrorContext* errCtx);

ZL_END_C_DECLS

#endif
