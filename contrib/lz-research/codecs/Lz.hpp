// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <openzl/openzl.hpp>

namespace openzl::lz {

struct Lz4Like {
    static size_t compressBound(size_t srcSize);
    static size_t decompressedSize(poly::string_view src);
    static size_t compress(poly::span<char> dst, poly::string_view src);
    static size_t decompress(poly::span<char> dst, poly::string_view src);
};

struct LzParameters {
    int llBits{ 3 };
};

NodeID registerLz(Compressor& compressor, LzParameters params);

void registerLz(DCtx& dctx);

using offset_t = uint16_t;
using length_t = uint16_t;

} // namespace openzl::lz
