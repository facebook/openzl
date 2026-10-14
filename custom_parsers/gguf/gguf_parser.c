// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/gguf/gguf_parser.h"

#include <stdbool.h>

#include "custom_parsers/gguf/gguf_lexer.h"
#include "custom_parsers/shared_components/tensor_chunker.h"
#include "custom_parsers/shared_components/tensor_graphs.h"
#include "openzl/codecs/zl_ace.h"
#include "openzl/codecs/zl_entropy.h"
#include "openzl/codecs/zl_split_by_struct.h"
#include "openzl/codecs/zl_zstd.h"
#include "openzl/common/assertion.h"
#include "openzl/zl_errors.h"

/// A quantized type split into fields goes to successor
/// GG_FIRST_BLOCK_SUCCESSOR + its ggml type. ggml never reuses a type value, so
/// these indices keep their meaning in serialized compressors. They start at a
/// fixed index, so that tensor classes can grow too; unused indices go to the
/// raw successor.
#define GG_FIRST_BLOCK_SUCCESSOR 32
#define GG_NB_SUCCESSORS (GG_FIRST_BLOCK_SUCCESSOR + ZL_GGUFType_limit)
ZL_STATIC_ASSERT(
        ZL_TensorClass_count <= GG_FIRST_BLOCK_SUCCESSOR,
        "tensor classes overlap the successors of quantized types");

#define GG_MAX_FIELDS 5

/// Fields of a quantized block in storage order. Scales are float16 values;
/// the other fields hold packed quantized values or sub-block scales.
typedef struct {
    uint32_t type;
    size_t nbFields;
    size_t sizes[GG_MAX_FIELDS];
    bool isScale[GG_MAX_FIELDS];
} GG_BlockLayout;

// Fields in the order of the block structs of ggml/src/ggml-common.h, e.g.
// block_q2_K = { scales, qs, d, dmin } and block_q6_K = { ql, qh, scales, d }.
static const GG_BlockLayout kBlockLayouts[] = {
    { ZL_GGUFType_q4_0, 2, { 2, 16 }, { true, false } },
    { ZL_GGUFType_q4_1, 3, { 2, 2, 16 }, { true, true, false } },
    { ZL_GGUFType_q5_0, 3, { 2, 4, 16 }, { true, false, false } },
    { ZL_GGUFType_q5_1, 4, { 2, 2, 4, 16 }, { true, true, false, false } },
    { ZL_GGUFType_q8_0, 2, { 2, 32 }, { true, false } },
    { ZL_GGUFType_q2_k, 4, { 16, 64, 2, 2 }, { false, false, true, true } },
    { ZL_GGUFType_q3_k, 4, { 32, 64, 12, 2 }, { false, false, false, true } },
    { ZL_GGUFType_q4_k, 4, { 2, 2, 12, 128 }, { true, true, false, false } },
    { ZL_GGUFType_q5_k,
      5,
      { 2, 2, 12, 32, 128 },
      { true, true, false, false, false } },
    { ZL_GGUFType_q6_k, 4, { 128, 64, 16, 2 }, { false, false, false, true } },
};
#define GG_NB_BLOCK_LAYOUTS (sizeof(kBlockLayouts) / sizeof(kBlockLayouts[0]))

static uint32_t GG_tagOf(uint32_t type)
{
    switch (type) {
        case ZL_GGUFType_f32:
            return ZL_TensorClass_f32;
        case ZL_GGUFType_f16:
            return ZL_TensorClass_f16;
        case ZL_GGUFType_bf16:
            return ZL_TensorClass_bf16;
        case ZL_GGUFType_f64:
            return ZL_TensorClass_f64;
        case ZL_GGUFType_i8:
            return ZL_TensorClass_int8;
        case ZL_GGUFType_i16:
            return ZL_TensorClass_int16;
        case ZL_GGUFType_i32:
            return ZL_TensorClass_int32;
        case ZL_GGUFType_i64:
            return ZL_TensorClass_int64;
        default:
            break;
    }
    for (size_t i = 0; i < GG_NB_BLOCK_LAYOUTS; ++i) {
        if (kBlockLayouts[i].type == type) {
            return GG_FIRST_BLOCK_SUCCESSOR + type;
        }
    }
    size_t blockElts, blockBytes;
    if (!ZL_GGUF_typeLayout(type, &blockElts, &blockBytes)) {
        return ZL_TensorClass_raw;
    }
    // Other known types are typed bytes, kept apart from raw data so that
    // training specializes them. Q8_1 and Q8_K are among them: llama.cpp only
    // uses them for activations, not stored weights.
    return ZL_TensorClass_int8;
}

/* ------------------------------------------------------------------------
 * Description of the tokens
 * ------------------------------------------------------------------------ */

/// Split granularity: whole rows when they are small enough, else blocks.
static size_t GG_splitAlign(const ZL_GGUFToken* t, size_t chunkSizeMax)
{
    size_t blockElts = 1, blockBytes = 1;
    ZL_GGUF_typeLayout(t->ggmlType, &blockElts, &blockBytes);
    if (t->ndims < 2) {
        return blockBytes;
    }
    const uint64_t rowBlocks = t->ne[0] / blockElts;
    if (rowBlocks == 0 || rowBlocks > chunkSizeMax / 2 / blockBytes) {
        return blockBytes;
    }
    return (size_t)rowBlocks * blockBytes;
}

static ZL_Report GG_lex(
        const void* src,
        size_t srcSize,
        void* tokens,
        size_t tokensCapacity,
        ZL_ErrorContext* errCtx)
{
    return ZL_GGUF_lex(src, srcSize, tokens, tokensCapacity, errCtx);
}

static ZL_TensorInfo GG_describe(const void* token, size_t chunkSizeMax)
{
    const ZL_GGUFToken* const t = (const ZL_GGUFToken*)token;
    switch (t->type) {
        case ZL_GGUFTokenType_header:
            return (ZL_TensorInfo){ t->ptr, t->size, ZL_TensorClass_header, 1 };
        case ZL_GGUFTokenType_tensor:
            return (ZL_TensorInfo){ t->ptr,
                                    t->size,
                                    GG_tagOf(t->ggmlType),
                                    GG_splitAlign(t, chunkSizeMax) };
        case ZL_GGUFTokenType_gap:
        default:
            return (ZL_TensorInfo){ t->ptr, t->size, ZL_TensorClass_raw, 1 };
    }
}

static const ZL_TensorFormat kGGUFFormat = {
    .name             = "GGUF",
    .defaultChunkSize = ZL_GGUF_DEFAULT_CHUNK_SIZE,
    .tokenSize        = sizeof(ZL_GGUFToken),
    .lex              = GG_lex,
    .describe         = GG_describe,
};

static ZL_Report GG_segmenterFn(ZL_Segmenter* sctx)
{
    return ZL_TensorChunker_segment(sctx, &kGGUFFormat);
}

/* ------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------ */

/// Quantized values and sub-block scales are nearly uniform bytes, where the
/// Huffman stage of zstd beats FSE; float16 scales are coded as 16-bit symbols.
static ZL_GraphID GG_blockSuccessor(
        ZL_Compressor* compressor,
        const GG_BlockLayout* layout)
{
    ZL_GraphID fieldGraphs[GG_MAX_FIELDS];
    for (size_t i = 0; i < layout->nbFields; ++i) {
        fieldGraphs[i] = layout->isScale[i] ? ZL_GRAPH_HUFFMAN : ZL_GRAPH_ZSTD;
    }
    return ZL_Compressor_buildACEGraphWithDefault(
            compressor,
            ZL_Compressor_registerSplitByStructGraph(
                    compressor, layout->sizes, fieldGraphs, layout->nbFields));
}

ZL_RESULT_OF(ZL_GraphID)
ZL_GGUF_registerGraph(ZL_Compressor* compressor, size_t chunkSizeMax)
{
    ZL_GraphID successors[GG_NB_SUCCESSORS];
    ZL_TensorGraphs_buildSuccessors(compressor, successors);
    for (size_t i = ZL_TensorClass_count; i < GG_NB_SUCCESSORS; ++i) {
        successors[i] = successors[ZL_TensorClass_raw];
    }
    for (size_t i = 0; i < GG_NB_BLOCK_LAYOUTS; ++i) {
        ZL_ASSERT_LT(kBlockLayouts[i].type, ZL_GGUFType_limit);
        successors[GG_FIRST_BLOCK_SUCCESSOR + kBlockLayouts[i].type] =
                GG_blockSuccessor(compressor, &kBlockLayouts[i]);
    }
    return ZL_TensorChunker_registerSegmenter(
            compressor,
            "!GGUF Segmenter",
            GG_segmenterFn,
            chunkSizeMax,
            "!GGUF Parser",
            successors,
            GG_NB_SUCCESSORS);
}
