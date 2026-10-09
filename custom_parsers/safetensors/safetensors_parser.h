// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_SAFETENSORS_SAFETENSORS_PARSER_H
#define CUSTOM_PARSERS_SAFETENSORS_SAFETENSORS_PARSER_H

#include "openzl/shared/portability.h"
#include "openzl/zl_compressor.h"

ZL_BEGIN_C_DECLS

#define ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE ((size_t)16 << 20)

/**
 * Registers a graph compressing safetensors files.
 *
 * Each tensor becomes its own stream, sent to a successor selected by dtype;
 * consecutive tensors smaller than 16 KiB share a stream. Float tensors with
 * few distinct values (masks, constants) are sent to the Transformer instead.
 * Successors are ACE graphs, so the resulting compressor can be trained.
 * Chunks follow layer boundaries: consecutive tensors whose names share the
 * prefix up to their first integer component (e.g. "model.layers.12.") are
 * kept in the same chunk when they fit in @p chunkSizeMax bytes, which is
 * raised to at least 2 * ZL_MIN_CHUNK_SIZE and capped at 64 MiB. Larger
 * tensors are split on row boundaries. Inputs that are not valid safetensors
 * files, and format versions without chunks, are compressed with the generic
 * graph, and the reason is reported as a warning.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Safetensors_registerGraph(ZL_Compressor* compressor, size_t chunkSizeMax);

ZL_END_C_DECLS

#endif
