// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/openzl.hpp"
#include "tests/codecs/test_codec.h"

namespace openzl {
namespace tests {

class Top8bitsTest : public CodecTest {
   public:
    /**
     * Tests round-trip compression/decompression using top8bits.
     *
     * @param input Numeric input data
     */
    template <typename T>
    void testTop8bitsRoundTrip(const std::vector<T>& input)
    {
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        // top8bits is parameter-free, use ZL_NODE_TOP8BITS directly
        auto graph = compressor_.buildStaticGraph(
                ZL_NODE_TOP8BITS, { graphs::Compress{}() });
        compressor_.selectStartingGraph(graph);

        Input numericInput = Input::refNumeric(poly::span<const T>{ input });
        testRoundTrip(numericInput);
    }
};

// =============================================================================
// Round-Trip Tests: effective width > 8
// =============================================================================

TEST_F(Top8bitsTest, RoundTrip_32bit_20bitEffective)
{
    // Values up to 0xFFFFF (20-bit effective width)
    // Expect 2 streams: 12-bit remainder + 8-bit top
    std::vector<uint32_t> input(1000);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = i & 0xFFFFF;
    }
    testTop8bitsRoundTrip(input);
}

TEST_F(Top8bitsTest, RoundTrip_64bit_12bitEffective)
{
    // Values up to 0xFFF (12-bit effective width)
    // Expect 2 streams: 4-bit remainder + 8-bit top
    std::vector<uint64_t> input(1000);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = i & 0xFFF;
    }
    testTop8bitsRoundTrip(input);
}

// =============================================================================
// Round-Trip Tests: effective width <= 8
// =============================================================================

TEST_F(Top8bitsTest, RoundTrip_16bit_8bitValues)
{
    // Values in [0, 255] → effective width = 8
    // Expect 1 stream (8-bit output)
    std::vector<uint16_t> input(1000);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = i & 0xFF;
    }
    testTop8bitsRoundTrip(input);
}

TEST_F(Top8bitsTest, RoundTrip_32bit_4bitValues)
{
    // Values in [0, 15] → effective width = 4
    // Expect 1 stream (8-bit output)
    std::vector<uint32_t> input(1000);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = i & 0xF;
    }
    testTop8bitsRoundTrip(input);
}

// =============================================================================
// All-Zero Input
// =============================================================================

TEST_F(Top8bitsTest, RoundTrip_AllZero_32bit)
{
    // All zeros → effective width = 0, treated as 1
    // Expect 1 stream (8-bit zeros)
    std::vector<uint32_t> input(1000, 0);
    testTop8bitsRoundTrip(input);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(Top8bitsTest, RoundTrip_EmptyInput)
{
    std::vector<uint32_t> input;
    testTop8bitsRoundTrip(input);
}

TEST_F(Top8bitsTest, RoundTrip_SingleElement)
{
    std::vector<uint32_t> input = { 0xABCD };
    testTop8bitsRoundTrip(input);
}

// =============================================================================
// Boundary: effective = 8 (max = 0xFF)
// =============================================================================

TEST_F(Top8bitsTest, Boundary_Effective8)
{
    // Max = 0xFF → effective width = 8 → 1 stream
    std::vector<uint32_t> input = { 0xFF, 0x01, 0x80, 0x00 };
    testTop8bitsRoundTrip(input);
}

// =============================================================================
// Boundary: effective = 9 (max = 0x1FF)
// =============================================================================

TEST_F(Top8bitsTest, Boundary_Effective9)
{
    // Max = 0x1FF → effective width = 9 → 2 streams (1-bit + 8-bit)
    std::vector<uint32_t> input = { 0x1FF, 0x100, 0x0FF, 0x000 };
    testTop8bitsRoundTrip(input);
}

// =============================================================================
// Various element widths
// =============================================================================

TEST_F(Top8bitsTest, RoundTrip_8bitInput)
{
    std::vector<uint8_t> input(256);
    for (size_t i = 0; i < 256; i++) {
        input[i] = (uint8_t)i;
    }
    testTop8bitsRoundTrip(input);
}

TEST_F(Top8bitsTest, RoundTrip_16bitInput)
{
    std::vector<uint16_t> input(1000);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = (uint16_t)(i & 0x3FF); // 10-bit effective
    }
    testTop8bitsRoundTrip(input);
}

TEST_F(Top8bitsTest, RoundTrip_64bitInput)
{
    std::vector<uint64_t> input(1000);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = i; // small values, effective width grows slowly
    }
    testTop8bitsRoundTrip(input);
}

} // namespace tests
} // namespace openzl
