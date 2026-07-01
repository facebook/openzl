// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <initializer_list>
#include <string>

#include "openzl/openzl.hpp"

// Mints valid OpenZL compressed frames whose codec content is fully controlled,
// for building GPU-decoder test fixtures. Frames are always produced by driving
// the real OpenZL encoder, so frame/chunk headers are correct by construction
// (this library never hand-serializes header bytes). Verify the resulting codec
// list with frame_verifier.h.

namespace openzl::gpu::testkit {

// Core minter. Applies the CParams needed to keep hand-picked codecs from being
// silently replaced by a raw STORE backup (see frame_factory.cpp), selects
// `startGraph` (which the caller must have already built on `compressor`), and
// compresses `input` into a frame. Returns the frame bytes.
std::string makeFrameWithGraph(
        openzl::Compressor& compressor,
        openzl::GraphID startGraph,
        const openzl::Input& input);

// Convenience: build a static graph that routes `node`'s outputs all to STORE
// (via the canonical buildTrivialGraph helper), then mint a frame for `input`.
// Whether the `node` codec actually survives in such a pure store-everything
// frame depends on the backup-defeating CParams; that is the subject of the
// KEY EXPERIMENT in the tests.
std::string makeFrameNodeToStore(
        openzl::Compressor& compressor,
        openzl::NodeID node,
        const openzl::Input& input);

// Convenience: build a static graph that runs `node` and routes each of its
// outputs to the matching successor graph in `successors`, then mint a frame.
// Feeding outputs to real backends (rather than STORE) lets a structural
// transform shrink the data and therefore survive in the frame.
std::string makeFrameNodeWithSuccessors(
        openzl::Compressor& compressor,
        openzl::NodeID node,
        std::initializer_list<openzl::GraphID> successors,
        const openzl::Input& input);

// Convenience: build a single-output chain node[0] -> node[1] -> ... -> STORE,
// then mint a frame. Requires a non-empty `chain`; each node must have exactly
// one output for the chain to be well-formed.
std::string makeFrameChainToStore(
        openzl::Compressor& compressor,
        std::initializer_list<openzl::NodeID> chain,
        const openzl::Input& input);

// Compresses then decompresses `input` through a freshly configured CCtx/DCtx
// using `compressor`/`startGraph`, and returns true iff the decompressed output
// equals `input`. Used as an internal self-check by the factory and by tests.
bool roundTrips(
        openzl::Compressor& compressor,
        openzl::GraphID startGraph,
        const openzl::Input& input);

// Decompresses an already-minted `frame` and returns true iff the single
// decompressed output equals `input`. Lets a test confirm a specific frame
// round-trips without re-minting it (and without registering more graphs).
bool decompressedEquals(const std::string& frame, const openzl::Input& input);

} // namespace openzl::gpu::testkit
