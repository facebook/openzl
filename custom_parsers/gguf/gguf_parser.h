// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_GGUF_GGUF_PARSER_H
#define CUSTOM_PARSERS_GGUF_GGUF_PARSER_H

#include "openzl/shared/portability.h"
#include "openzl/zl_compressor.h"

ZL_BEGIN_C_DECLS

#define ZL_GGUF_DEFAULT_CHUNK_SIZE ((size_t)16 << 20)

/**
 * Registers a graph compressing GGUF files (llama.cpp model format).
 *
 * Each tensor becomes its own stream, sent to a successor selected by type;
 * consecutive tensors smaller than 16 KiB share a stream, sent to the raw
 * successor when their types differ. Float and integer tensors are handled
 * like their safetensors counterparts. Blocks of the common quantized types
 * (Q4_0, Q4_1, Q5_0, Q5_1, Q8_0 and the K-quants) are split into their fields:
 * float16 scales and packed quantized values are compressed separately. Other
 * quantized types are compressed as bytes. Quantized weights are already
 * dense: the gain over zstd is a few percent, and decompression is slower than
 * zstd's.
 * Successors are ACE graphs, so the resulting compressor can be trained.
 * Chunks are formed as with ZL_Safetensors_registerGraph().
 * Inputs that are not valid GGUF files, and format versions without chunks,
 * are compressed with the generic graph, and the reason is reported as a
 * warning.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_GGUF_registerGraph(ZL_Compressor* compressor, size_t chunkSizeMax);

ZL_END_C_DECLS

#endif
