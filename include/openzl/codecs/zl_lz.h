// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_LZ_H
#define OPENZL_CODECS_LZ_H

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

/// The LZ encoder sets the metadata on this ID to the minimum possible
/// match length emitted by the encoder.
#define ZL_LZ_MIN_MATCH_LENGTH_METADATA_ID 77

#if defined(__cplusplus)
}
#endif

#endif
