// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_LZ4_H
#define OPENZL_CODECS_LZ4_H

#include "openzl/zl_errors.h"
#include "openzl/zl_graphs.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define ZL_GRAPH_LZ4 ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_lz4)

/// @returns ZL_GRAPH_LZ4 with overridden compression level
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildLZ4Graph(ZL_Compressor* cgraph, int compressionLevel);

/// Set this integer parameter to override the compression level
#define ZL_LZ4_COMPRESSION_LEVEL_OVERRIDE_PID 0

#if defined(__cplusplus)
}
#endif

#endif
