// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/openzl.hpp"
#include "tests/codecs/test_codec.h"

namespace openzl {
namespace tests {

class SplitByRangeTest : public CodecTest {
   public:
    template <typename T>
    void testSplitByRangeRoundTrip(const std::vector<T>& input)
    {
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        auto graph = compressor_.buildStaticGraph(
                ZL_NODE_SPLIT_BYRANGE, { graphs::Compress{}() });
        compressor_.selectStartingGraph(graph);

        Input numericInput = Input::refNumeric(poly::span<const T>{ input });
        testRoundTrip(numericInput);
    }
};

// =============================================================================
// Basic: Two well-separated ranges
// =============================================================================

TEST_F(SplitByRangeTest, TwoRanges_WellSeparated)
{
    // Range 1: [100, 110], Range 2: [500, 510]
    std::vector<uint32_t> input;
    for (uint32_t i = 0; i < 500; i++) {
        input.push_back(100 + (i % 11));
    }
    for (uint32_t i = 0; i < 500; i++) {
        input.push_back(500 + (i % 11));
    }
    testSplitByRangeRoundTrip(input);
}

TEST_F(SplitByRangeTest, TwoRanges_HighThenLow)
{
    // Range 1: [1000, 2000], Range 2: [0, 100]
    std::vector<uint32_t> input;
    input.reserve(600);
    for (uint32_t i = 0; i < 300; i++) {
        input.push_back(1000 + (i % 1001));
    }
    for (uint32_t i = 0; i < 300; i++) {
        input.push_back(i % 101);
    }
    testSplitByRangeRoundTrip(input);
}

// =============================================================================
// Three ranges
// =============================================================================

TEST_F(SplitByRangeTest, ThreeRanges)
{
    // Range 1: [100, 110], Range 2: [5, 8], Range 3: [200, 210]
    std::vector<uint32_t> input;
    for (uint32_t i = 0; i < 200; i++) {
        input.push_back(100 + (i % 11));
    }
    for (uint32_t i = 0; i < 200; i++) {
        input.push_back(5 + (i % 4));
    }
    for (uint32_t i = 0; i < 200; i++) {
        input.push_back(200 + (i % 11));
    }
    testSplitByRangeRoundTrip(input);
}

// =============================================================================
// Single range (no split needed)
// =============================================================================

TEST_F(SplitByRangeTest, SingleRange_NoSplit)
{
    std::vector<uint32_t> input;
    input.reserve(1000);
    for (uint32_t i = 0; i < 1000; i++) {
        input.push_back(50 + (i % 51));
    }
    testSplitByRangeRoundTrip(input);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(SplitByRangeTest, EmptyInput)
{
    std::vector<uint32_t> input;
    testSplitByRangeRoundTrip(input);
}

TEST_F(SplitByRangeTest, SingleElement)
{
    std::vector<uint32_t> input = { 42 };
    testSplitByRangeRoundTrip(input);
}

TEST_F(SplitByRangeTest, TwoElements_SameRange)
{
    std::vector<uint32_t> input = { 42, 43 };
    testSplitByRangeRoundTrip(input);
}

TEST_F(SplitByRangeTest, TwoElements_DifferentRanges)
{
    std::vector<uint32_t> input = { 100, 0 };
    testSplitByRangeRoundTrip(input);
}

TEST_F(SplitByRangeTest, AllSameValue)
{
    std::vector<uint32_t> input(1000, 42);
    testSplitByRangeRoundTrip(input);
}

TEST_F(SplitByRangeTest, AllZeros)
{
    std::vector<uint32_t> input(1000, 0);
    testSplitByRangeRoundTrip(input);
}

TEST_F(SplitByRangeTest, MaxValues)
{
    std::vector<uint32_t> input = { UINT32_MAX, UINT32_MAX - 1, 0, 1 };
    testSplitByRangeRoundTrip(input);
}

// =============================================================================
// Various element widths
// =============================================================================

TEST_F(SplitByRangeTest, Width8bit)
{
    // u8 uses large blocks (blockSize=64), so segments need many elements
    // for reliable boundary detection with M=7.
    std::vector<uint8_t> input;
    input.reserve(1200);
    for (int i = 0; i < 600; i++) {
        input.push_back(static_cast<uint8_t>(200 + (i % 50)));
    }
    for (int i = 0; i < 600; i++) {
        input.push_back(static_cast<uint8_t>(i % 50));
    }
    testSplitByRangeRoundTrip(input);
}

TEST_F(SplitByRangeTest, Width16bit)
{
    std::vector<uint16_t> input;
    for (uint16_t i = 0; i < 500; i++) {
        input.push_back(10000 + (i % 100));
    }
    for (uint16_t i = 0; i < 500; i++) {
        input.push_back(i % 100);
    }
    testSplitByRangeRoundTrip(input);
}

TEST_F(SplitByRangeTest, Width64bit)
{
    std::vector<uint64_t> input;
    for (uint64_t i = 0; i < 500; i++) {
        input.push_back(1000000ULL + (i % 100));
    }
    for (uint64_t i = 0; i < 500; i++) {
        input.push_back(i % 100);
    }
    testSplitByRangeRoundTrip(input);
}

// =============================================================================
// Adjacent ranges (touching but not overlapping)
// =============================================================================

TEST_F(SplitByRangeTest, AdjacentRanges)
{
    // Range 1: [0, 99], Range 2: [100, 199]
    // These ranges are adjacent but non-overlapping
    std::vector<uint32_t> input;
    input.reserve(1000);
    for (uint32_t i = 0; i < 500; i++) {
        input.push_back(i % 100);
    }
    for (uint32_t i = 0; i < 500; i++) {
        input.push_back(100 + (i % 100));
    }
    testSplitByRangeRoundTrip(input);
}

// =============================================================================
// Many small segments
// =============================================================================

TEST_F(SplitByRangeTest, ManyAlternatingRanges)
{
    // Alternating between high and low values, many segments
    std::vector<uint32_t> input;
    for (int seg = 0; seg < 10; seg++) {
        uint32_t base = (seg % 2 == 0) ? 1000 : 0;
        for (uint32_t i = 0; i < 50; i++) {
            input.push_back(base + (i % 10));
        }
    }
    testSplitByRangeRoundTrip(input);
}

// =============================================================================
// Segment count tests — verify the algorithm doesn't over-split
// =============================================================================

class CountingFunctionGraph : public FunctionGraph {
   public:
    FunctionGraphDescription functionGraphDescription() const override
    {
        return FunctionGraphDescription{
            .name           = "Counter",
            .inputTypeMasks = { TypeMask::Numeric },
        };
    }

    void graph(GraphState& state) const override
    {
        ++count_;
        state.edges()[0].setDestination(graphs::Store{}());
    }

    size_t count() const
    {
        return count_;
    }
    void reset() const
    {
        count_ = 0;
    }

   private:
    mutable size_t count_ = 0;
};

class SplitByRangeSegmentCountTest : public SplitByRangeTest {
   public:
    template <typename T>
    size_t countSegments(const std::vector<T>& input)
    {
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        auto counter    = std::make_shared<CountingFunctionGraph>();
        auto countGraph = compressor_.registerFunctionGraph(counter);
        auto graph      = compressor_.buildStaticGraph(
                ZL_NODE_SPLIT_BYRANGE, { countGraph });
        compressor_.selectStartingGraph(graph);

        Input numericInput = Input::refNumeric(poly::span<const T>{ input });
        testRoundTrip(numericInput);
        return counter->count();
    }
};

TEST_F(SplitByRangeSegmentCountTest, TwoRanges_ProducesTwoSegments)
{
    // Range 1: [100, 110], Range 2: [500, 510]
    std::vector<uint32_t> input;
    for (uint32_t i = 0; i < 500; i++) {
        input.push_back(100 + (i % 11));
    }
    for (uint32_t i = 0; i < 500; i++) {
        input.push_back(500 + (i % 11));
    }
    EXPECT_EQ(countSegments(input), 2u);
}

TEST_F(SplitByRangeSegmentCountTest, SingleRange_ProducesOneSegment)
{
    std::vector<uint32_t> input;
    input.reserve(1000);
    for (uint32_t i = 0; i < 1000; i++) {
        input.push_back(50 + (i % 51));
    }
    EXPECT_EQ(countSegments(input), 1u);
}

TEST_F(SplitByRangeSegmentCountTest, MonotonicIncreasing_ShouldNotOversplit)
{
    // A monotonically increasing series has non-overlapping prefix/suffix
    // at every position, but splitting it serves no purpose.
    // Ideally this should produce 1 segment.
    std::vector<uint32_t> input;
    for (uint32_t i = 0; i < 1000; i++) {
        input.push_back(i);
    }
    size_t segments = countSegments(input);
    EXPECT_LE(segments, 2u) << "Monotonic series should not be over-split, got "
                            << segments << " segments";
}

TEST_F(SplitByRangeSegmentCountTest, MonotonicDecreasing_ShouldNotOversplit)
{
    std::vector<uint32_t> input;
    for (uint32_t i = 0; i < 1000; i++) {
        input.push_back(1000 - i);
    }
    size_t segments = countSegments(input);
    EXPECT_LE(segments, 2u) << "Monotonic series should not be over-split, got "
                            << segments << " segments";
}

} // namespace tests
} // namespace openzl
