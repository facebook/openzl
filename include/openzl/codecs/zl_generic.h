// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_GENERIC_H
#define ZSTRONG_CODECS_GENERIC_H

#include "openzl/zl_graphs.h"

#if defined(__cplusplus)
extern "C" {
#endif

// "default" compression for any stream type - supports multiple inputs
#define ZL_GRAPH_COMPRESS_GENERIC \
    ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_compress_generic)

// Selects between LZ backends based on compression level - superseded by
// ZL_GRAPH_COMPRESS_GENERIC - could be retired now
#define ZL_GRAPH_GENERIC_LZ_BACKEND \
    ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_select_generic_lz_backend)

// Numeric
// Input : 1 stream of numeric data
// Result : compresses a stream of numeric data using a GBT model to select the
// best compression algorithm based on stream
#define ZL_GRAPH_NUMERIC ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_select_numeric)

#define ZL_GRAPH_SEGMENT_NUMERIC           \
    (ZL_GraphID)                           \
    {                                      \
        ZL_StandardGraphID_segment_numeric \
    }

#if defined(__cplusplus)
}
#endif

#endif
