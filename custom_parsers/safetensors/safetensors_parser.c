// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/safetensors/safetensors_parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "custom_parsers/safetensors/safetensors_lexer.h"
#include "custom_parsers/shared_components/tensor_chunker.h"
#include "custom_parsers/shared_components/tensor_graphs.h"
#include "openzl/zl_errors.h"

static ZL_TensorClass ST_classOf(ZL_SafetensorsDtype dtype)
{
    switch (dtype) {
        case ZL_SafetensorsDtype_bool:
        case ZL_SafetensorsDtype_u8:
        case ZL_SafetensorsDtype_i8:
        case ZL_SafetensorsDtype_f8_e4m3:
        case ZL_SafetensorsDtype_f8_e5m2:
            return ZL_TensorClass_int8;
        case ZL_SafetensorsDtype_i16:
        case ZL_SafetensorsDtype_u16:
            return ZL_TensorClass_int16;
        case ZL_SafetensorsDtype_i32:
        case ZL_SafetensorsDtype_u32:
            return ZL_TensorClass_int32;
        case ZL_SafetensorsDtype_i64:
        case ZL_SafetensorsDtype_u64:
            return ZL_TensorClass_int64;
        case ZL_SafetensorsDtype_f16:
            return ZL_TensorClass_f16;
        case ZL_SafetensorsDtype_bf16:
            return ZL_TensorClass_bf16;
        case ZL_SafetensorsDtype_f32:
            return ZL_TensorClass_f32;
        case ZL_SafetensorsDtype_f64:
            return ZL_TensorClass_f64;
        case ZL_SafetensorsDtype_unknown:
        default:
            return ZL_TensorClass_raw;
    }
}

static bool ST_nameEndsWith(const ZL_SafetensorsToken* t, const char* suffix)
{
    const size_t n = strlen(suffix);
    return t->nameSize >= n
            && memcmp(t->name + t->nameSize - n, suffix, n) == 0;
}

/// Quantized formats name their tensors after their role: `scales` (GPTQ, AWQ,
/// MLX), `biases` (MLX) and `weight_scale` (compressed-tensors) are scales of
/// groups of values; `qweight` (GPTQ, AWQ), `weight_packed`
/// (compressed-tensors) and `weight` (MLX) hold values of a few bits packed in
/// 32-bit words, which are coded as bytes. MLX stores them as uint32 and the
/// other formats as int32, so an int32 `weight` keeps the class of its dtype.
/// These names are a best-effort guess: a tensor named alike for another role
/// is only compressed less well, never lossily.
static ZL_TensorClass ST_tensorClassOf(const ZL_SafetensorsToken* t)
{
    const bool is16 = t->dtype == ZL_SafetensorsDtype_f16
            || t->dtype == ZL_SafetensorsDtype_bf16;
    const bool isU32 = t->dtype == ZL_SafetensorsDtype_u32;
    const bool is32  = isU32 || t->dtype == ZL_SafetensorsDtype_i32;
    if (is16 && t->ndims >= 2
        && (ST_nameEndsWith(t, ".scales") || ST_nameEndsWith(t, ".biases")
            || ST_nameEndsWith(t, ".weight_scale"))) {
        return ZL_TensorClass_scale16;
    }
    if ((is32
         && (ST_nameEndsWith(t, ".qweight")
             || ST_nameEndsWith(t, ".weight_packed")))
        || (isU32 && ST_nameEndsWith(t, ".weight"))) {
        return ZL_TensorClass_int8;
    }
    return ST_classOf(t->dtype);
}

/// Split granularity: whole rows when they are small enough, else elements.
static size_t ST_splitAlign(const ZL_SafetensorsToken* t, size_t chunkSizeMax)
{
    const size_t eltSize = ZL_Safetensors_dtypeSize(t->dtype);
    if (t->dtype == ZL_SafetensorsDtype_unknown || t->ndims < 2) {
        return eltSize;
    }
    uint64_t rowBytes = eltSize;
    for (uint32_t d = 1; d < t->ndims; ++d) {
        if (t->shape[d] != 0 && rowBytes > chunkSizeMax / 2 / t->shape[d]) {
            return eltSize;
        }
        rowBytes *= t->shape[d];
    }
    return rowBytes ? (size_t)rowBytes : eltSize;
}

static ZL_Report ST_lex(
        const void* src,
        size_t srcSize,
        void* tokens,
        size_t tokensCapacity,
        ZL_ErrorContext* errCtx)
{
    return ZL_Safetensors_lex(src, srcSize, tokens, tokensCapacity, errCtx);
}

static ZL_TensorInfo ST_describe(const void* token, size_t chunkSizeMax)
{
    const ZL_SafetensorsToken* const t = (const ZL_SafetensorsToken*)token;
    switch (t->type) {
        case ZL_SafetensorsTokenType_header:
            return (ZL_TensorInfo){ t->ptr, t->size, ZL_TensorClass_header, 1 };
        case ZL_SafetensorsTokenType_tensor:
            return (ZL_TensorInfo){ t->ptr,
                                    t->size,
                                    ST_tensorClassOf(t),
                                    ST_splitAlign(t, chunkSizeMax) };
        case ZL_SafetensorsTokenType_gap:
        default:
            return (ZL_TensorInfo){ t->ptr, t->size, ZL_TensorClass_raw, 1 };
    }
}

static const ZL_TensorFormat kSafetensorsFormat = {
    .name             = "safetensors",
    .defaultChunkSize = ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE,
    .tokenSize        = sizeof(ZL_SafetensorsToken),
    .lex              = ST_lex,
    .describe         = ST_describe,
};

static ZL_Report ST_segmenterFn(ZL_Segmenter* sctx)
{
    return ZL_TensorChunker_segment(sctx, &kSafetensorsFormat);
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Safetensors_registerGraph(ZL_Compressor* compressor, size_t chunkSizeMax)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);
    ZL_TRY_LET(
            ZL_GraphID,
            scale16,
            ZL_TensorGraphs_registerScale16Graph(compressor));
    ZL_GraphID successors[ZL_TensorClass_count];
    ZL_TensorGraphs_buildSuccessors(compressor, scale16, successors);
    return ZL_TensorChunker_registerSegmenter(
            compressor,
            "!Safetensors Segmenter",
            ST_segmenterFn,
            chunkSizeMax,
            "!Safetensors Parser",
            successors,
            ZL_TensorClass_count);
}
