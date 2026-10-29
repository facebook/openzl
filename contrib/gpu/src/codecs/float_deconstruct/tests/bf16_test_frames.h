// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openzl::gpu::test {

/// The one-byte bf16 float_deconstruct codec header.
extern const std::byte kBf16Header[1];

/// @returns @p numElts random bf16 bit patterns, the same for each @p seed.
std::vector<uint16_t> makeBf16Values(size_t numElts, unsigned seed);

/// The encoded streams of a bf16 float_deconstruct node: the decoder's inputs.
struct Bf16Streams {
    std::vector<uint8_t> signFrac;
    std::vector<uint8_t> exponent;
};

/**
 * Compresses @p values with bf16 float_deconstruct through the real encoder,
 * and reads its encoded streams back with the reflection API.
 *
 * Records a test failure and returns empty streams if that fails.
 */
Bf16Streams encodeBf16(const std::vector<uint16_t>& values);

} // namespace openzl::gpu::test
