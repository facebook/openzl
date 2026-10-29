// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "openzl/openzl.hpp"

// These functions compress leaf streams the same way that Nimble compresses
// its numeric leaf streams, so GPU decoder tests and benchmarks get the frames
// that Nimble writes. The graph is a copy of the numeric graph in Nimble's
// open-source `OpenZLCompressor.cpp`.

namespace openzl::gpu::testkit {

// Element types that Nimble compresses with the numeric graph. Nimble sends
// every other type (double, bool, string) to plain zstd.
enum class NimbleNumericType {
    U8,
    U16,
    U32,
    U64,
    F32,
};

size_t elementByteWidth(NimbleNumericType type);

// Builds Nimble's numeric graph on `compressor` and returns its starting
// graph. The graph takes one serial input whose size is a multiple of the
// element width.
openzl::GraphID buildNimbleNumericGraph(
        openzl::Compressor& compressor,
        NimbleNumericType type);

// Format version 27, compression level 4, and decompression level 2. Format
// version 27 is the newest, so frames can use the most recent codecs, such
// as PivCo Huffman.
struct NimbleCompressParams {
    int formatVersion      = 27;
    int compressionLevel   = 4;
    int decompressionLevel = 2;
};

// Compresses `data` with Nimble's numeric graph and the CParams in `params`,
// and returns the frame. Throws openzl::Exception on error, and
// std::invalid_argument for an unknown type.
std::string compressLikeNimble(
        std::string_view data,
        NimbleNumericType type,
        const NimbleCompressParams& params = {});

} // namespace openzl::gpu::testkit
