// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_LZ_H
#define OPENZL_CODECS_LZ_H

#include "openzl/zl_graphs.h"
#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// LZ compresses a serial byte stream using LZ77 matching,
// decomposing it into four output streams.
//
// Input: A serial byte stream.
// Output 1: Literals: A serial stream of unmatched bytes.
// Output 2: Offsets: A numeric stream of match distances.
// Output 3: Literal Lengths: A numeric stream of literal run lengths.
// Output 4: Match Lengths: A numeric stream of match lengths.
#define ZL_NODE_LZ ZL_MAKE_NODE_ID(ZL_StandardNodeID_lz)

/// Standard function graph for LZ compression.
/// Runs ZL_NODE_LZ with presets to offer performance similar to Zstd.
/// In the future, it will be parameterizable to select different tradeoffs
/// (like LZ4) and control the parameters like compression level.
#define ZL_GRAPH_LZ ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_lz)

/// The LZ encoder sets the metadata on this ID to the minimum expected
/// match length emitted by the encoder. There may be edge cases where
/// the match length is shorter, but it will be extremely rare. The
/// current cases are:
/// - The literal length is >= 2^16, and a match length of 0 is emitted.
/// - The match length is >= 2^16 and (ml % 2^16 < min_match_length)
#define ZL_LZ_MIN_MATCH_LENGTH_METADATA_ID 77

/**
 * Parameters that control the behavior of ZL_NODE_LZ and ZL_GRAPH_LZ.
 */
typedef enum {
    /**
     * If set, it overrides the ZL_GCParam_compressionLevel, otherwise
     * it defaults to the global compression level.
     *
     * The compression level controls the default parameters and successor
     * graphs. If compression level has no effect on parameters or successor
     * graphs that are explicitly overridden.
     */
    ZL_LzParam_compressionLevel = 1,

    /**
     * The acceleration parameter controls how quickly the match finder
     * skips over the input. Only one in every `acceleration` bytes of
     * the input is checked for matches. The default value is derived
     * from the compression level. If the compression level is >= 0,
     * then it defaults to 1. Otherwise it defaults to -compression_level.
     *
     * @note This parameter is clamped to 1 if set to a lower value.
     */
    ZL_LzParam_acceleration = 100,

    /**
     * The log2 of the maximum lookback window in LZ match finding.
     * windowSize = 1u << windowLog.
     *
     * The default value is set depending on the compression level and source
     * size, and is capped at the source size.
     *
     * @note If the window size is <= 64K, then LZ will use 16-bit offsets
     * rather than 32-bit offsets.
     * @note currently, this value will be clamped between 10 and 28, but this
     * is subject to change.
     */
    ZL_LzParam_windowLog = 101,
} ZL_LzParam;

#if defined(__cplusplus)
}
#endif

#endif
