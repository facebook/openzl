// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_SHARED_COMPONENTS_TENSOR_CHUNKER_H
#define CUSTOM_PARSERS_SHARED_COMPONENTS_TENSOR_CHUNKER_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/shared/portability.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_segmenter.h"

ZL_BEGIN_C_DECLS

/**
 * Parsing shared by the model weight formats.
 *
 * A format lexes the input into tokens that cover it in order (header, tensors,
 * gaps), and describes each token: the index of the successor it goes to, and
 * how it may be split. Tokens become segments, grouped into chunks, and an
 * inner graph splits each chunk back into its segments.
 */

/// One token of a model weight file, as described by its format.
typedef struct {
    const char* ptr;
    size_t size;
    uint32_t tag;      // successor; f16, bf16 and f32 are refined by content
    size_t splitAlign; // byte granularity when the token must be split, at
                       // least 1 and at most half of the chunkSizeMax given
                       // to describe
} ZL_TensorInfo;

/// What a format provides to the shared parsing.
typedef struct {
    const char* name; // for warnings
    size_t defaultChunkSize;
    size_t tokenSize; // size of one of the tokens filled by lex
    /// Lexes @p src into @p tokens.
    /// @returns the number of tokens, or when @p tokens is NULL, the largest
    /// number of tokens @p src can produce, or why it cannot be parsed.
    ZL_Report (*lex)(
            const void* src,
            size_t srcSize,
            void* tokens,
            size_t tokensCapacity,
            ZL_ErrorContext* errCtx);
    /// Describes @p token, one of those filled by lex.
    ZL_TensorInfo (*describe)(const void* token, size_t chunkSizeMax);
} ZL_TensorFormat;

/**
 * Segments the input of a segmenter registered with
 * ZL_TensorChunker_registerSegmenter(). Inputs that @p format fails to lex,
 * and format versions without chunks, are compressed with the generic graph,
 * and the reason is reported as a warning.
 */
ZL_Report ZL_TensorChunker_segment(
        ZL_Segmenter* sctx,
        const ZL_TensorFormat* format);

/**
 * Registers the segmenter of a parser under @p segmenterName, and the inner
 * graph its chunks go to under @p innerName. The inner graph splits a chunk
 * into its segments and sends each one to @p successors[tag].
 *
 * Both names start with '!', which registers them as is: serialized
 * compressors find the graphs by these names, so they must not change.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_TensorChunker_registerSegmenter(
        ZL_Compressor* compressor,
        const char* segmenterName,
        ZL_SegmenterFn segmenterFn,
        size_t chunkSizeMax,
        const char* innerName,
        const ZL_GraphID* successors,
        size_t nbSuccessors);

ZL_END_C_DECLS

#endif
