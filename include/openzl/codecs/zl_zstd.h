// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_ZSTD_H
#define ZSTRONG_CODECS_ZSTD_H

#include "openzl/zl_graphs.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define ZL_GRAPH_ZSTD ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_zstd)

/// @return zstd graph with a compression level overridden
ZL_GraphID ZL_Compressor_registerZstdGraph_withLevel(
        ZL_Compressor* cgraph,
        int compressionLevel);

#if defined(__cplusplus)
}
#endif

#endif
