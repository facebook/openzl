// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/codecs/bitSplit/encode_bitSplit_binding.h"
#include "openzl/openzl.hpp"
#include "tests/codecs/test_codec.h"

namespace openzl {
namespace tests {

class BitSplitTest : public CodecTest {
   public:
    /**
     * Tests round-trip compression/decompression using bitSplit.
     *
     * @param bitWidths Array of bit widths for splitting
     * @param nbWidths Number of bit widths
     * @param input Numeric input data
     */
    template <typename T>
    void testBitSplitRoundTrip(
            const uint8_t* bitWidths,
            size_t nbWidths,
            const std::vector<T>& input)
    {
        // Set format version
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        // Register bitSplit node with parameters
        NodeID bitSplitNode = ZL_Compressor_registerBitSplitNode(
                compressor_.get(), bitWidths, nbWidths);
        ASSERT_NE(bitSplitNode.nid, ZL_NODE_ILLEGAL.nid);

        // bitSplit uses Variable Output (VO) - 1 port that generates N outputs
        // at runtime The single successor will be instantiated N times
        auto graph = compressor_.buildStaticGraph(
                bitSplitNode, { graphs::Compress{}() });
        compressor_.selectStartingGraph(graph);

        // Test round-trip
        Input numericInput = Input::refNumeric(poly::span<const T>{ input });
        testRoundTrip(numericInput);
    }
};

// =============================================================================
// Basic Round-Trip Tests
// =============================================================================

TEST_F(BitSplitTest, RoundTrip_8bit_Split_4_4)
{
    std::vector<uint8_t> input(1000);
    std::iota(input.begin(), input.end(), 0);

    uint8_t widths[] = { 4, 4 };
    testBitSplitRoundTrip(widths, 2, input);
}

TEST_F(BitSplitTest, RoundTrip_16bit_Split_8_8)
{
    std::vector<uint16_t> input(1000);
    std::iota(input.begin(), input.end(), 0);

    uint8_t widths[] = { 8, 8 };
    testBitSplitRoundTrip(widths, 2, input);
}

TEST_F(BitSplitTest, RoundTrip_32bit_Split_4_8_12_8)
{
    std::vector<uint32_t> input(1000);
    std::iota(input.begin(), input.end(), 0);

    uint8_t widths[] = { 4, 8, 12, 8 };
    testBitSplitRoundTrip(widths, 4, input);
}

TEST_F(BitSplitTest, RoundTrip_32bit_Split_8_8_8_8)
{
    std::vector<uint32_t> input(1000);
    std::iota(input.begin(), input.end(), 0);

    uint8_t widths[] = { 8, 8, 8, 8 };
    testBitSplitRoundTrip(widths, 4, input);
}

TEST_F(BitSplitTest, RoundTrip_64bit_Split_16_16_16_16)
{
    std::vector<uint64_t> input(1000);
    std::iota(input.begin(), input.end(), 0);

    uint8_t widths[] = { 16, 16, 16, 16 };
    testBitSplitRoundTrip(widths, 4, input);
}

// =============================================================================
// Single Stream Tests (Identity-like)
// =============================================================================

TEST_F(BitSplitTest, RoundTrip_SingleStream_8bit)
{
    std::vector<uint8_t> input(1000);
    std::iota(input.begin(), input.end(), 0);

    uint8_t widths[] = { 8 };
    testBitSplitRoundTrip(widths, 1, input);
}

TEST_F(BitSplitTest, RoundTrip_SingleStream_32bit)
{
    std::vector<uint32_t> input(1000);
    std::iota(input.begin(), input.end(), 0);

    uint8_t widths[] = { 32 };
    testBitSplitRoundTrip(widths, 1, input);
}

// =============================================================================
// Partial Coverage Tests (Top bits must be zero)
// =============================================================================

TEST_F(BitSplitTest, RoundTrip_PartialCoverage_32bit_Split_3_4_5)
{
    // Values must fit in 12 bits (max 0xFFF)
    std::vector<uint32_t> input(1000);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = i & 0xFFF;
    }

    uint8_t widths[] = { 3, 4, 5 };
    testBitSplitRoundTrip(widths, 3, input);
}

TEST_F(BitSplitTest, RoundTrip_PartialCoverage_16bit_Split_4_4)
{
    // Values must fit in 8 bits (max 0xFF)
    std::vector<uint16_t> input(1000);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = i & 0xFF;
    }

    uint8_t widths[] = { 4, 4 };
    testBitSplitRoundTrip(widths, 2, input);
}

// =============================================================================
// Random Data Tests
// =============================================================================

TEST_F(BitSplitTest, RoundTrip_Random_32bit)
{
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint32_t> dist;

    std::vector<uint32_t> input(1000);
    for (auto& v : input) {
        v = dist(rng);
    }

    uint8_t widths[] = { 8, 8, 8, 8 };
    testBitSplitRoundTrip(widths, 4, input);
}

TEST_F(BitSplitTest, RoundTrip_Random_64bit)
{
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<uint64_t> dist;

    std::vector<uint64_t> input(1000);
    for (auto& v : input) {
        v = dist(rng);
    }

    uint8_t widths[] = { 1, 9, 22, 32 };
    testBitSplitRoundTrip(widths, 4, input);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(BitSplitTest, RoundTrip_EmptyInput)
{
    std::vector<uint32_t> input;

    uint8_t widths[] = { 8, 8, 8, 8 };
    testBitSplitRoundTrip(widths, 4, input);
}

TEST_F(BitSplitTest, RoundTrip_SingleElement)
{
    std::vector<uint32_t> input = { 0xDEADBEEF };

    uint8_t widths[] = { 4, 8, 12, 8 };
    testBitSplitRoundTrip(widths, 4, input);
}

TEST_F(BitSplitTest, RoundTrip_AllZeros)
{
    std::vector<uint32_t> input(1000, 0);

    uint8_t widths[] = { 8, 8, 8, 8 };
    testBitSplitRoundTrip(widths, 4, input);
}

TEST_F(BitSplitTest, RoundTrip_AllOnes)
{
    std::vector<uint32_t> input(1000, 0xFFFFFFFF);

    uint8_t widths[] = { 8, 8, 8, 8 };
    testBitSplitRoundTrip(widths, 4, input);
}

// =============================================================================
// Asymmetric Split Tests
// =============================================================================

TEST_F(BitSplitTest, RoundTrip_AsymmetricSplit_1_7)
{
    std::vector<uint8_t> input(1000);
    std::iota(input.begin(), input.end(), 0);

    uint8_t widths[] = { 1, 7 };
    testBitSplitRoundTrip(widths, 2, input);
}

TEST_F(BitSplitTest, RoundTrip_AsymmetricSplit_1_31)
{
    std::vector<uint32_t> input(1000);
    std::iota(input.begin(), input.end(), 0);

    uint8_t widths[] = { 1, 31 };
    testBitSplitRoundTrip(widths, 2, input);
}

TEST_F(BitSplitTest, RoundTrip_ManySmallWidths)
{
    std::vector<uint8_t> input(1000);
    std::iota(input.begin(), input.end(), 0);

    // Split 8 bits into 8 individual bits
    uint8_t widths[] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    testBitSplitRoundTrip(widths, 8, input);
}

} // namespace tests
} // namespace openzl
