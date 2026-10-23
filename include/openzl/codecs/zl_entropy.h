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
 * If provided to an entropy graph, byte inputs larger than this many bytes,
 * between 2^15 and 2^26, are cut into chunks of this size, each coded with its
 * own statistics. Larger chunks spend less on headers when statistics are
 * homogeneous, smaller ones follow changing statistics. Huffman codes chunks
 * of at most 128 KiB: ZL_GRAPH_HUFFMAN caps its chunks at this size, and
 * larger chunks of ZL_GRAPH_ENTROPY are not coded with Huffman. If unset, the
 * entropy graph uses chunks of its choosing. Inputs of 2-byte elements keep
 * their default chunks. Format versions below 15 ignore it.
 */
#define ZL_ENTROPY_CHUNK_SIZE_PID 81

/**
 * If provided to an entropy graph, the maximum table log of FSE, between 9 and
 * 12. Larger tables code wide alphabets with rare symbols more accurately, at
 * the cost of a larger header and decoding table. If unset, the entropy graph
 * uses a default of its choosing. Format versions below 15 ignore it.
 */
#define ZL_ENTROPY_FSE_MAX_TABLE_LOG_PID 82

/**
 * If provided to an entropy graph, the maximum length of the codes of byte
 * Huffman, 11 or 12. 12-bit codes double the decoding table and slow decoding
 * down, so they are only used where they make a chunk at least 1/512 (~0.2%)
 * smaller. If unset, codes have at most 11 bits. Format versions below 15
 * ignore it.
 */
#define ZL_ENTROPY_HUFFMAN_MAX_TABLE_LOG_PID 83

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

/**
 * Specifically opts into HUF0 style Huffman encoding.
 * Generally not recommended, but useful for testing.
 *
 * This graph will likely be removed in the future.
 */
#define ZL_GRAPH_HUFFMAN_HUF0 ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_huffman_huf0)

/**
 * Specifically opts into PivCo Huffman encoding.
 * Generally not recommended, but useful for testing.
 *
 * This graph will likely be removed in the future.
 */
#define ZL_GRAPH_HUFFMAN_PIVCO \
    ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_huffman_pivco)

#if defined(__cplusplus)
}
#endif

#endif
