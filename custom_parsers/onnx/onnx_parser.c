// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/onnx/onnx_parser.h"

#include "custom_parsers/onnx/onnx_lexer.h"
#include "custom_parsers/shared_components/tensor_chunker.h"
#include "custom_parsers/shared_components/tensor_graphs.h"
#include "openzl/zl_errors.h"

static ZL_TensorClass OX_classOf(uint32_t dataType)
{
    switch (dataType) {
        case ZL_ONNXDataType_uint8:
        case ZL_ONNXDataType_int8:
        case ZL_ONNXDataType_bool:
        // packed values of a few bits
        case ZL_ONNXDataType_uint4:
        case ZL_ONNXDataType_int4:
        case ZL_ONNXDataType_float4e2m1:
        case ZL_ONNXDataType_uint2:
        case ZL_ONNXDataType_int2:
            return ZL_TensorClass_int8;
        case ZL_ONNXDataType_float8e4m3fn:
        case ZL_ONNXDataType_float8e4m3fnuz:
        case ZL_ONNXDataType_float8e5m2:
        case ZL_ONNXDataType_float8e5m2fnuz:
        case ZL_ONNXDataType_float8e8m0:
            return ZL_TensorClass_fp8;
        case ZL_ONNXDataType_uint16:
        case ZL_ONNXDataType_int16:
            return ZL_TensorClass_int16;
        case ZL_ONNXDataType_int32:
        case ZL_ONNXDataType_uint32:
            return ZL_TensorClass_int32;
        case ZL_ONNXDataType_int64:
        case ZL_ONNXDataType_uint64:
            return ZL_TensorClass_int64;
        case ZL_ONNXDataType_float16:
            return ZL_TensorClass_f16;
        case ZL_ONNXDataType_bfloat16:
            return ZL_TensorClass_bf16;
        // complex numbers are pairs of floats
        case ZL_ONNXDataType_float:
        case ZL_ONNXDataType_complex64:
            return ZL_TensorClass_f32;
        case ZL_ONNXDataType_double:
        case ZL_ONNXDataType_complex128:
            return ZL_TensorClass_f64;
        default:
            return ZL_TensorClass_raw;
    }
}

/// Split granularity: whole rows when they are small enough, else elements,
/// or bytes for packed elements.
static size_t OX_splitAlign(const ZL_ONNXToken* t, size_t chunkSizeMax)
{
    const size_t bits    = ZL_ONNX_dataTypeBits(t->dataType);
    const size_t eltSize = bits < 8 ? 1 : bits / 8;
    if (bits < 8 || t->ndims < 2) {
        return eltSize;
    }
    uint64_t rowBytes = eltSize;
    for (uint32_t d = 1; d < t->ndims; ++d) {
        if (t->dims[d] != 0 && rowBytes > chunkSizeMax / 2 / t->dims[d]) {
            return eltSize;
        }
        rowBytes *= t->dims[d];
    }
    return rowBytes ? (size_t)rowBytes : eltSize;
}

static ZL_Report OX_lex(
        const void* src,
        size_t srcSize,
        void* tokens,
        size_t tokensCapacity,
        ZL_ErrorContext* errCtx)
{
    return ZL_ONNX_lex(src, srcSize, tokens, tokensCapacity, errCtx);
}

static ZL_TensorInfo OX_describe(const void* token, size_t chunkSizeMax)
{
    const ZL_ONNXToken* const t = (const ZL_ONNXToken*)token;
    switch (t->type) {
        case ZL_ONNXTokenType_header:
            return (ZL_TensorInfo){ t->ptr, t->size, ZL_TensorClass_header, 1 };
        case ZL_ONNXTokenType_tensor:
            return (ZL_TensorInfo){ t->ptr,
                                    t->size,
                                    OX_classOf(t->dataType),
                                    OX_splitAlign(t, chunkSizeMax) };
        case ZL_ONNXTokenType_gap:
        default:
            return (ZL_TensorInfo){ t->ptr, t->size, ZL_TensorClass_raw, 1 };
    }
}

static const ZL_TensorFormat kONNXFormat = {
    .name             = "ONNX",
    .defaultChunkSize = ZL_ONNX_DEFAULT_CHUNK_SIZE,
    .tokenSize        = sizeof(ZL_ONNXToken),
    .lex              = OX_lex,
    .describe         = OX_describe,
};

static ZL_Report OX_segmenterFn(ZL_Segmenter* sctx)
{
    return ZL_TensorChunker_segment(sctx, &kONNXFormat);
}

ZL_RESULT_OF(ZL_GraphID)
ZL_ONNX_registerGraph(ZL_Compressor* compressor, size_t chunkSizeMax)
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
            "!ONNX Segmenter",
            OX_segmenterFn,
            chunkSizeMax,
            "!ONNX Parser",
            successors,
            ZL_TensorClass_count);
}
