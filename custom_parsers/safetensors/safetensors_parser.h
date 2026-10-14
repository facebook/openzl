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
 * consecutive tensors smaller than 16 KiB share a stream, sent to the raw
 * successor when their dtypes differ. Float tensors with few distinct values
 * (masks, constants) are sent to the Transformer instead.
 * Successors are ACE graphs, so the resulting compressor can be trained.
 * A tensor starts a new chunk when it does not fit in the room left in the
 * current one, of at most @p chunkSizeMax bytes, which is raised to at least
 * 2 * ZL_MIN_CHUNK_SIZE and capped at 64 MiB. Larger tensors are split on row
 * boundaries.
 * Inputs that are not valid safetensors files, and format versions without
 * chunks, are compressed with the generic graph, and the reason is reported as
 * a warning.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Safetensors_registerGraph(ZL_Compressor* compressor, size_t chunkSizeMax);

ZL_END_C_DECLS

#endif
