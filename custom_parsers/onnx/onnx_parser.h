// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_ONNX_ONNX_PARSER_H
#define CUSTOM_PARSERS_ONNX_ONNX_PARSER_H

#include "openzl/shared/portability.h"
#include "openzl/zl_compressor.h"

ZL_BEGIN_C_DECLS

#define ZL_ONNX_DEFAULT_CHUNK_SIZE ((size_t)16 << 20)

/**
 * Registers a graph compressing ONNX model files.
 *
 * Each tensor whose bytes are in raw_data becomes its own stream, sent to a
 * successor selected by its element type, as for safetensors files. The
 * protobuf fields around the tensors (graph, names, shapes) are compressed
 * apart, but tensors smaller than 16 KiB share a stream with them, sent to the
 * raw successor. Models of 2 GB or more keep their weights in external files,
 * which are not ONNX files: only the graph is then parsed.
 * Successors are ACE graphs, so the resulting compressor can be trained.
 * Chunks are formed as with ZL_Safetensors_registerGraph().
 * Inputs that are not valid ONNX files, and format versions without chunks,
 * are compressed with the generic graph, and the reason is reported as a
 * warning.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_ONNX_registerGraph(ZL_Compressor* compressor, size_t chunkSizeMax);

ZL_END_C_DECLS

#endif
