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

#if defined(__cplusplus)
}
#endif

#endif
