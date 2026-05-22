// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_ENTROPY_H
#define ZSTRONG_CODECS_ENTROPY_H

#include "openzl/zl_graph_api.h"
#include "openzl/zl_graphs.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Entropy backend that can use backends that are at least as fast as FSE
// Supports serialized inputs
#define ZL_GRAPH_FSE ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_fse)
// Entropy backend that can use backends that are at least as fast as Huffman
// Supports both serialized & 2-byte struct inputs
#define ZL_GRAPH_HUFFMAN ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_huffman)
// Entropy backend that can use any backend that satisfies the compression &
// decompression speed requirements. Supports both serialized & 2-byte struct
// inputs
#define ZL_GRAPH_ENTROPY ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_entropy)

/**
 * If provided to an entropy graph, ZL_GRAPH_STORE will be selected unless
 * using entropy compression saves at least this many bytes. If unset, the
 * entropy graph will use a small default value of its choosing.
 */
#define ZL_ENTROPY_MIN_GAIN_BYTES_PID 66

/**
 * If provided to an entropy graph, ZL_GRAPH_STORE will be selected unless
 * using entropy compression saves at least this percent of the input size. If
 * unset, the entropy graph will use a small default value of its choosing.
 */
#define ZL_ENTROPY_MIN_GAIN_PCT_PID 80

/**
 * Sets the destination of @p edge to @p entropyGraph with the parameters
 * @p minGainBytes and @p minGainPct.
 *
 * @param minGainBytes The value for ZL_ENTROPY_MIN_GAIN_BYTES_PID or < 0 to
 * leave unset.
 * @param minGainPct The value for ZL_ENTROPY_MIN_GAIN_PCT_PID or < 0 to leave
 * unset.
 */
ZL_Report ZL_Edge_setEntropyDestination(
        ZL_Edge* edge,
        ZL_GraphID entropyGraph,
        int minGainBytes,
        int minGainPct);

#if defined(__cplusplus)
}
#endif

#endif
