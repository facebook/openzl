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

} // namespace openzl::gpu::testkit
