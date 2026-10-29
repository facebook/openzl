// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Inspection helpers for OpenZL compressed frames. These helpers drive real
// OpenZL decode paths, so the codec lists they report are exactly what the
// decoder will see -- never a guess from parsing header bytes by hand.

namespace openzl::gpu::testkit {

// Returns the wire IDs (ZL_StandardTransformID) of the standard codecs present
// in the last chunk of `frame`, in the order the reflection API reports them
// (decode order). Custom (non-standard) codecs are skipped, since they have no
// stable standard wire ID. Throws std::runtime_error if the frame cannot be
// decoded.
std::vector<uint32_t> standardCodecsInFrame(const std::string& frame);

// Returns true iff the standard codecs in `frame` are exactly `expected`,
// compared as a multiset (order-independent, duplicates significant). Custom
// codecs are ignored, matching standardCodecsInFrame().
bool frameHasExactlyCodecs(
        const std::string& frame,
        const std::vector<uint32_t>& expected);

// Per-chunk standard-codec enumeration for a multi-chunk frame. This helper
// uses decompression introspection hooks, with codec fusion disabled on its
// throwaway decompression context, so it reports every individual standard
// codec executed for each chunk. Custom (non-standard) codecs are skipped,
// since they have no stable standard wire ID.
//
// Throws std::runtime_error if the frame cannot be decoded.
std::vector<std::vector<uint32_t>> standardCodecsPerChunk(
        const std::string& frame);

// The decode graph of the last chunk of `frame` as text, for tests that pin
// a graph. Each codec appears as "ID:type(children)": ID is its codec ID, type
// labels the stream the codec regenerates (b for serial, sN for struct, nN for
// numeric, vN for string, N in bytes), and the children are the codec's
// outputs in slot order. A stored stream appears as its type alone. Several
// root streams are joined with ';'. The text holds no stream contents or
// sizes. Throws std::runtime_error if the frame cannot be decoded.
std::string decodeGraphSignature(const std::string& frame);

// decodeGraphSignature() with each split codec (splitn, splitn_struct,
// splitn_num) collapsed to "ID:type(child|child)*": its distinct children,
// sorted, without stored blocks when any block is coded. A stored block
// reaches the frame with no codec, or through conversion codecs only (STORE
// takes serial streams, so a stored struct block appears as "6:sN(b)"). The
// shape does not depend on how many blocks the encoder cut, which changes
// with the input size.
std::string decodeGraphShape(const std::string& frame);

} // namespace openzl::gpu::testkit
