// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_SAFETENSORS_SAFETENSORS_HEADER_H
#define CUSTOM_PARSERS_SAFETENSORS_SAFETENSORS_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/**
 * Parser for the header of a safetensors file.
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

typedef struct {
    const char* name; // points into the header, not NUL-terminated, unescaped
    size_t nameSize;
    ZL_SafetensorsDtype dtype;
    uint32_t ndims;
    uint64_t shape[ZL_SAFETENSORS_MAX_DIMS];
    uint64_t begin; // offsets relative to the data section
    uint64_t end;
} ZL_SafetensorsTensor;

/// @returns the element size in bytes, or 1 for unknown dtypes.
size_t ZL_Safetensors_dtypeSize(ZL_SafetensorsDtype dtype);

/**
 * Parses and validates the header of the safetensors file in @p src.
 *
 * On success, fills @p tensors (sorted by data offset, zero-sized tensors
 * included), and sets @p nbTensors and @p dataStart (offset of the data
 * section within @p src). Tensors must not overlap and must fit within @p src,
 * and there must be at most ZL_SAFETENSORS_MAX_TENSORS of them.
 * A tensor whose byte size does not match its dtype and shape is reported with
 * ZL_SafetensorsDtype_unknown.
 *
 * When @p tensors is NULL, only the header size and syntax are validated and
 * the tensors are counted into @p nbTensors, without allocating anything, so
 * that the caller can size @p tensors exactly before parsing again.
 *
 * @returns NULL on success, or a static string describing the error.
 */
const char* ZL_Safetensors_parseHeader(
        const void* src,
        size_t srcSize,
        ZL_SafetensorsTensor* tensors,
        size_t tensorsCapacity,
        size_t* nbTensors,
        size_t* dataStart);

ZL_END_C_DECLS

#endif
