// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "openzl/openzl.hpp"

// Mints valid OpenZL frames that contain MULTIPLE chunks, where each chunk is
// compressed with a DIFFERENT codec graph. Like frame_factory.h, frames are
// always produced by driving the real OpenZL encoder (through a custom
// Segmenter), so frame/chunk headers and checksums are correct by construction
// -- this library never hand-serializes header bytes. Verify the per-chunk
// codec content with frame_verifier.h (standardCodecsPerChunk()).
//
// Multi-chunk frames require wire format version >= ZL_CHUNK_VERSION_MIN (21);
// the factory pins ZL_MAX_FORMAT_VERSION, which satisfies that. Each chunk must
// also clear the ZL_MIN_CHUNK_SIZE floor (32768 bytes of content), so callers
// must size chunks comfortably above that.

namespace openzl::gpu::testkit {

// One chunk of a multi-chunk frame: `numElts` elements (counted in the input's
// element units -- e.g. int32 values for a refNumeric<int32_t> input, bytes for
// a serial input) routed to the codec graph `graph`. The caller must have
// already built `graph` on the same Compressor passed to makeMultiChunkFrame().
struct ChunkSpec {
    size_t numElts;
    openzl::GraphID graph;
};

// Splits `input` into consecutive chunks as described by `chunks` (chunk i gets
// the next chunks[i].numElts elements) and mints ONE frame in which chunk i is
// compressed with chunks[i].graph. The element counts in `chunks` MUST sum to
// input.numElts(), or the underlying segmenter rejects the input (it requires
// the whole input to be consumed). Drives the real encoder via a custom
// Segmenter; returns the frame bytes.
//
// Applies the same store-backup-defeating CParams as frame_factory
// (StoreOnExpansion=disable, MinStreamSize=-1) so the per-chunk codecs survive,
// and pins FormatVersion to ZL_MAX_FORMAT_VERSION so chunking is available and
// reflection IDs are stable.
std::string makeMultiChunkFrame(
        openzl::Compressor& compressor,
        std::initializer_list<ChunkSpec> chunks,
        const openzl::Input& input);

// Returns an Input over a contiguous slice of `input`: the `numElts` elements
// starting at element `eltOffset`. The returned Input keeps `input`'s type and
// element width and REFERENCES `input`'s buffer (it does not copy), so `input`
// must outlive the returned slice.
//
// This is the building block for the documented per-chunk verification fallback
// (the public reflection API only exposes a frame's last chunk -- see
// frame_verifier.h). Carving out chunk i's slice and minting it as its own
// single-chunk frame (via frame_factory.h::makeFrameWithGraph) lets the public
// reflection API report that chunk's codecs, which is exactly the per-chunk
// graph makeMultiChunkFrame routes. Only Serial and Numeric inputs are
// supported (Struct/String throw); these cover the multi-chunk fixtures.
openzl::Input
sliceInput(const openzl::Input& input, size_t eltOffset, size_t numElts);

} // namespace openzl::gpu::testkit
