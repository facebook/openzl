// Copyright (c) Meta Platforms, Inc. and affiliates.

// Generator-based tests for split_byrange.
// Generates samples with known range boundaries and verifies
// that split_byrange detects the exact boundaries by checking
// the actual output segments.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/openzl.hpp"
#include "tests/codecs/test_codec.h"
#include "tests/datagen/DataGen.h"

namespace openzl {
namespace tests {

using datagen::DataGen;

class SplitByRangeGeneratorTest : public CodecTest {
   public:
    struct RangeSpec {
        uint32_t lo;
        uint32_t hi;
        size_t count;
    };

    std::vector<uint32_t> generateSample(
            const std::vector<RangeSpec>& ranges,
            unsigned seed = 42)
    {
        DataGen gen(seed);
        std::vector<uint32_t> data;
        for (const auto& r : ranges) {
            for (size_t i = 0; i < r.count; i++) {
                data.push_back(gen.u32_range("val", r.lo, r.hi));
            }
        }
        return data;
    }

    // Build the expected output segments from the input and expected sizes.
    // Returns pointers into owned storage.
    struct SegmentData {
        std::vector<std::vector<uint32_t>> segments;
        std::vector<Input> inputs;
        std::vector<const Input*> ptrs;
    };

    SegmentData buildExpectedSegments(
            const std::vector<uint32_t>& data,
            const std::vector<size_t>& expectedSizes)
    {
        SegmentData sd;
        size_t pos = 0;
        for (size_t sz : expectedSizes) {
            sd.segments.emplace_back(
                    data.begin() + pos, data.begin() + pos + sz);
            pos += sz;
        }
        EXPECT_EQ(pos, data.size());
        // Build Input objects (must survive until test completes)
        for (auto& seg : sd.segments) {
            sd.inputs.push_back(
                    Input::refNumeric(poly::span<const uint32_t>{ seg }));
        }
        for (auto& inp : sd.inputs) {
            sd.ptrs.push_back(&inp);
        }
        return sd;
    }

    // Verify that split_byrange produces exactly the expected segments.
    void verifyExactBoundaries(
            const std::vector<uint32_t>& data,
            const std::vector<size_t>& expectedSizes)
    {
        Input numericInput =
                Input::refNumeric(poly::span<const uint32_t>{ data });
        auto sd = buildExpectedSegments(data, expectedSizes);
        testCodecVO(
                ZL_NODE_SPLIT_BYRANGE,
                numericInput,
                sd.ptrs,
                24 /* minFormatVersion */);
    }

    // Verify exact boundaries with a custom minSegmentSize parameter.
    void verifyExactBoundariesWithMinSegSize(
            const std::vector<uint32_t>& data,
            const std::vector<size_t>& expectedSizes,
            int minSegmentSize)
    {
        Input numericInput =
                Input::refNumeric(poly::span<const uint32_t>{ data });
        auto sd = buildExpectedSegments(data, expectedSizes);
        NodeID paramNode =
                nodes::SplitByRange(minSegmentSize).parameterize(compressor_);
        testCodecVO(
                paramNode, numericInput, sd.ptrs, 24 /* minFormatVersion */);
    }

    // Width-generic helpers for testing non-32-bit element widths
    template <typename T>
    struct TypedRangeSpec {
        T lo;
        T hi;
        size_t count;
    };

    template <typename T>
    std::vector<T> generateTypedSample(
            const std::vector<TypedRangeSpec<T>>& ranges,
            unsigned seed = 42)
    {
        DataGen gen(seed);
        std::vector<T> data;
        for (const auto& r : ranges) {
            for (size_t i = 0; i < r.count; i++) {
                data.push_back(
                        static_cast<T>(gen.u64_range("val", r.lo, r.hi)));
            }
        }
        return data;
    }

    template <typename T>
    struct TypedSegmentData {
        std::vector<std::vector<T>> segments;
        std::vector<Input> inputs;
        std::vector<const Input*> ptrs;
    };

    template <typename T>
    void verifyTypedExactBoundaries(
            const std::vector<T>& data,
            const std::vector<size_t>& expectedSizes,
            int minSegmentSize = 0)
    {
        TypedSegmentData<T> sd;
        size_t pos = 0;
        for (size_t sz : expectedSizes) {
            sd.segments.emplace_back(
                    data.begin() + pos, data.begin() + pos + sz);
            pos += sz;
        }
        EXPECT_EQ(pos, data.size());
        for (auto& seg : sd.segments) {
            sd.inputs.push_back(Input::refNumeric(poly::span<const T>{ seg }));
        }
        for (auto& inp : sd.inputs) {
            sd.ptrs.push_back(&inp);
        }
        Input numericInput = Input::refNumeric(poly::span<const T>{ data });
        NodeID nodeId      = ZL_NODE_SPLIT_BYRANGE;
        if (minSegmentSize > 0) {
            nodeId = nodes::SplitByRange(minSegmentSize)
                             .parameterize(compressor_);
        }
        testCodecVO(nodeId, numericInput, sd.ptrs, 24 /* minFormatVersion */);
    }
};

// =============================================================================
// Stage 1: Two well-separated ranges
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage1_TwoRanges_FarApart)
{
    auto data = generateSample(
            {
                    { 100, 200, 500 },
                    { 1000, 1100, 500 },
            });
    printf("Stage 1: Two ranges far apart (100-200, 1000-1100)\n");
    verifyExactBoundaries(data, { 500, 500 });
}

TEST_F(SplitByRangeGeneratorTest, Stage1_TwoRanges_HighThenLow)
{
    auto data = generateSample(
            {
                    { 5000, 6000, 300 },
                    { 0, 100, 400 },
            });
    printf("Stage 1: Two ranges high then low (5000-6000, 0-100)\n");
    verifyExactBoundaries(data, { 300, 400 });
}

// =============================================================================
// Stage 2: Three ranges (tests recursive splitting)
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage2_ThreeRanges_HighLowHigher)
{
    // A=[100,200], B=[5,50], C=[500,600]
    // First pass finds boundary between [A,B] and C.
    // Second pass (recursive) finds boundary between A and B.
    auto data = generateSample(
            {
                    { 100, 200, 300 },
                    { 5, 50, 200 },
                    { 500, 600, 400 },
            });
    printf("Stage 2: Three ranges high-low-higher (100-200, 5-50, 500-600)\n");
    verifyExactBoundaries(data, { 300, 200, 400 });
}

TEST_F(SplitByRangeGeneratorTest, Stage2_ThreeRanges_Ascending)
{
    auto data = generateSample(
            {
                    { 0, 100, 200 },
                    { 200, 300, 200 },
                    { 400, 500, 200 },
            });
    printf("Stage 2: Three ascending ranges (0-100, 200-300, 400-500)\n");
    verifyExactBoundaries(data, { 200, 200, 200 });
}

TEST_F(SplitByRangeGeneratorTest, Stage2_ThreeRanges_Descending)
{
    auto data = generateSample(
            {
                    { 400, 500, 200 },
                    { 200, 300, 200 },
                    { 0, 100, 200 },
            });
    printf("Stage 2: Three descending ranges\n");
    verifyExactBoundaries(data, { 200, 200, 200 });
}

// =============================================================================
// Stage 3: Multiple ranges in monotonic order (recursive splitting)
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage3_FiveRanges_Ascending)
{
    // Ranges in ascending order: prefix max always < suffix min at boundaries
    auto data = generateSample(
            {
                    { 0, 50, 100 },
                    { 500, 550, 150 },
                    { 1000, 1100, 200 },
                    { 2000, 2100, 100 },
                    { 3000, 3100, 150 },
            });
    printf("Stage 3: Five ascending ranges\n");
    verifyExactBoundaries(data, { 100, 150, 200, 100, 150 });
}

TEST_F(SplitByRangeGeneratorTest, Stage3_FiveRanges_Descending)
{
    // Ranges in descending order: suffix max always < prefix min at boundaries
    auto data = generateSample(
            {
                    { 3000, 3100, 150 },
                    { 2000, 2100, 100 },
                    { 1000, 1100, 200 },
                    { 500, 550, 150 },
                    { 0, 50, 100 },
            });
    printf("Stage 3: Five descending ranges\n");
    verifyExactBoundaries(data, { 150, 100, 200, 150, 100 });
}

TEST_F(SplitByRangeGeneratorTest, Stage3_FourRanges_Valley)
{
    // Valley pattern: high, low, high — prefix/suffix splits find boundaries
    auto data = generateSample(
            {
                    { 2000, 2100, 100 },
                    { 0, 50, 200 },
                    { 3000, 3100, 150 },
            });
    printf("Stage 3: Valley pattern (high, low, high)\n");
    verifyExactBoundaries(data, { 100, 200, 150 });
}

// =============================================================================
// Stage 4: Adjacent ranges (gap = 1)
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage4_AdjacentRanges_Gap1)
{
    auto data = generateSample(
            {
                    { 0, 99, 300 },
                    { 100, 199, 300 },
            });
    printf("Stage 4: Adjacent ranges with gap=1 (0-99, 100-199)\n");
    verifyExactBoundaries(data, { 300, 300 });
}

TEST_F(SplitByRangeGeneratorTest, Stage4_NearRanges_Gap10)
{
    auto data = generateSample(
            {
                    { 0, 100, 300 },
                    { 111, 200, 300 },
            });
    printf("Stage 4: Near ranges with gap=10 (0-100, 111-200)\n");
    verifyExactBoundaries(data, { 300, 300 });
}

// =============================================================================
// Stage 5: Many contiguous segments (ascending staircase)
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage5_TenAscendingSegments)
{
    // 10 contiguous segments with ascending ranges — algorithm can split these
    std::vector<RangeSpec> specs;
    std::vector<size_t> expectedSizes;
    for (int i = 0; i < 10; i++) {
        uint32_t base = static_cast<uint32_t>(i) * 1000;
        specs.push_back({ base, base + 100, 50 });
        expectedSizes.push_back(50);
    }
    auto data = generateSample(specs);
    printf("Stage 5: 10 ascending segments of 50 elements each\n");
    verifyExactBoundaries(data, expectedSizes);
}

TEST_F(SplitByRangeGeneratorTest, Stage5b_AlternatingSegments_NoSplit)
{
    // Alternating ranges: min-max overlap prevents any split
    std::vector<uint32_t> data;
    for (int seg = 0; seg < 4; seg++) {
        uint32_t base = (seg % 2 == 0) ? 0 : 10000;
        for (uint32_t i = 0; i < 50; i++) {
            data.push_back(base + (i % 100));
        }
    }
    printf("Stage 5b: Alternating ranges, no split expected\n");
    verifyExactBoundaries(data, { 200 });
}

// =============================================================================
// Stage 6: Single range (no split)
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage6_SingleRange_NoSplit)
{
    auto data = generateSample(
            {
                    { 500, 600, 1000 },
            });
    printf("Stage 6: Single range, no split expected\n");
    verifyExactBoundaries(data, { 1000 });
}

// =============================================================================
// Stage 7: Overlapping ranges (should NOT split)
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage7_OverlappingRanges_NoSplit)
{
    auto data = generateSample(
            {
                    { 0, 200, 500 },
                    { 100, 300, 500 },
            });
    printf("Stage 7: Overlapping ranges (0-200, 100-300), no split\n");
    verifyExactBoundaries(data, { 1000 });
}

// =============================================================================
// Stage 8: Minimum-sized segments (default MIN_SEGMENT_SIZE=16)
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage8_MinSizeSegments)
{
    // Each segment has exactly 16 elements (the default minimum)
    auto data = generateSample(
            {
                    { 1000, 1100, 16 },
                    { 0, 50, 16 },
            });
    printf("Stage 8: Two segments of 16 elements each (default min size)\n");
    verifyExactBoundaries(data, { 16, 16 });
}

TEST_F(SplitByRangeGeneratorTest, Stage8b_BelowMinSize_NoSplit)
{
    // 16 elements total < 2*DEFAULT_MIN_SEGMENT_SIZE=32, so no split
    auto data = generateSample(
            {
                    { 1000, 1100, 8 },
                    { 0, 50, 8 },
            });
    printf("Stage 8b: Below default min size, no split expected\n");
    verifyExactBoundaries(data, { 16 });
}

// =============================================================================
// Stage 9: Large dataset with 3 ranges
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage9_LargeDataset)
{
    auto data = generateSample(
            {
                    { 0, 1000, 10000 },
                    { 50000, 60000, 10000 },
                    { 100000, 110000, 10000 },
            });
    printf("Stage 9: Large dataset, 3 ranges x 10000 elements\n");
    verifyExactBoundaries(data, { 10000, 10000, 10000 });
}

// =============================================================================
// Stage 10: Progressively closer ranges
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage10_Gap1000)
{
    auto data = generateSample(
            {
                    { 0, 100, 200 },
                    { 1100, 1200, 200 },
            });
    printf("Stage 10: Gap=1000\n");
    verifyExactBoundaries(data, { 200, 200 });
}

TEST_F(SplitByRangeGeneratorTest, Stage10_Gap100)
{
    auto data = generateSample(
            {
                    { 0, 100, 200 },
                    { 200, 300, 200 },
            });
    printf("Stage 10: Gap=100\n");
    verifyExactBoundaries(data, { 200, 200 });
}

TEST_F(SplitByRangeGeneratorTest, Stage10_Gap10)
{
    auto data = generateSample(
            {
                    { 0, 100, 200 },
                    { 110, 200, 200 },
            });
    printf("Stage 10: Gap=10\n");
    verifyExactBoundaries(data, { 200, 200 });
}

TEST_F(SplitByRangeGeneratorTest, Stage10_Gap1)
{
    auto data = generateSample(
            {
                    { 0, 100, 200 },
                    { 101, 200, 200 },
            });
    printf("Stage 10: Gap=1\n");
    verifyExactBoundaries(data, { 200, 200 });
}

// =============================================================================
// Stage 11: Four ranges in non-monotonic order
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage11_FourRanges_Shuffled)
{
    // D=[3000,3100], A=[0,50], C=[1000,1100], B=[500,550]
    auto data = generateSample(
            {
                    { 3000, 3100, 100 },
                    { 0, 50, 150 },
                    { 1000, 1100, 100 },
                    { 500, 550, 200 },
            });
    printf("Stage 11: Four ranges in non-monotonic order\n");
    verifyExactBoundaries(data, { 100, 150, 100, 200 });
}

// =============================================================================
// Stage 12: Different element widths with exact boundary verification
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage12_Width8bit)
{
    auto data = generateTypedSample<uint8_t>({
            { 200, 250, 50 },
            { 0, 30, 50 },
    });
    printf("Stage 12: u8 two ranges (200-250, 0-30)\n");
    verifyTypedExactBoundaries<uint8_t>(data, { 50, 50 });
}

TEST_F(SplitByRangeGeneratorTest, Stage12_Width16bit)
{
    auto data = generateTypedSample<uint16_t>({
            { 0, 100, 100 },
            { 50000, 60000, 100 },
            { 200, 300, 100 },
    });
    printf("Stage 12: u16 three ascending-then-low ranges\n");
    verifyTypedExactBoundaries<uint16_t>(data, { 100, 100, 100 });
}

TEST_F(SplitByRangeGeneratorTest, Stage12_Width64bit)
{
    auto data = generateTypedSample<uint64_t>({
            { 1000000000ULL, 1000001000ULL, 100 },
            { 0, 500, 100 },
    });
    printf("Stage 12: u64 two ranges (1e9-range, 0-500)\n");
    verifyTypedExactBoundaries<uint64_t>(data, { 100, 100 });
}

TEST_F(SplitByRangeGeneratorTest, Stage12_Width8bit_ThreeRanges)
{
    auto data = generateTypedSample<uint8_t>({
            { 0, 10, 30 },
            { 100, 110, 30 },
            { 200, 210, 30 },
    });
    printf("Stage 12: u8 three ascending ranges\n");
    verifyTypedExactBoundaries<uint8_t>(data, { 30, 30, 30 });
}

// =============================================================================
// Stage 13: Custom minSegmentSize parameter
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage13_SmallMinSegSize_AllowsSmallSegments)
{
    // With default minSegmentSize=16, 8-element segments can't be detected.
    // With minSegmentSize=4, they can.
    auto data = generateSample(
            {
                    { 1000, 1100, 8 },
                    { 0, 50, 8 },
            });
    printf("Stage 13: minSegmentSize=4 allows 8-element segments\n");
    verifyExactBoundariesWithMinSegSize(data, { 8, 8 }, 4);
}

TEST_F(SplitByRangeGeneratorTest, Stage13_LargeMinSegSize_PreventsSmallSplits)
{
    // With minSegmentSize=200, 100-element segments are too small to split.
    auto data = generateSample(
            {
                    { 0, 100, 100 },
                    { 5000, 6000, 100 },
            });
    printf("Stage 13: minSegmentSize=200 prevents split\n");
    verifyExactBoundariesWithMinSegSize(data, { 200 }, 200);
}

TEST_F(SplitByRangeGeneratorTest, Stage13_DefaultParam_SameAsUnparameterized)
{
    // Explicitly passing the default value should produce the same result
    auto data = generateSample(
            {
                    { 0, 100, 200 },
                    { 1000, 1100, 200 },
            });
    printf("Stage 13: Explicit default minSegmentSize=16\n");
    verifyExactBoundariesWithMinSegSize(data, { 200, 200 }, 16);
}

// =============================================================================
// Stage 14: Repeated ranges (same value range appearing multiple times,
//           separated by different ranges)
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage14_ABA_SameRangeTwice)
{
    // Range A appears twice, separated by Range B.
    // The algorithm should detect 3 segments even though A repeats.
    auto data = generateSample(
            {
                    { 100, 200, 200 },
                    { 500, 600, 200 },
                    { 100, 200, 200 },
            });
    printf("Stage 14: A-B-A pattern (same range twice)\n");
    verifyExactBoundaries(data, { 200, 200, 200 });
}

TEST_F(SplitByRangeGeneratorTest, Stage14_ABAB_TwoRangesRepeated)
{
    // Two ranges each appearing twice: A, B, A, B.
    auto data = generateSample(
            {
                    { 0, 100, 150 },
                    { 500, 600, 150 },
                    { 0, 100, 150 },
                    { 500, 600, 150 },
            });
    printf("Stage 14: A-B-A-B pattern (two ranges repeated)\n");
    verifyExactBoundaries(data, { 150, 150, 150, 150 });
}

TEST_F(SplitByRangeGeneratorTest, Stage14_ABCBA_Palindrome)
{
    // Palindrome pattern: ranges go up then back down to same values.
    auto data = generateSample(
            {
                    { 0, 50, 100 },
                    { 500, 600, 100 },
                    { 2000, 2100, 100 },
                    { 500, 600, 100 },
                    { 0, 50, 100 },
            });
    printf("Stage 14: A-B-C-B-A palindrome pattern\n");
    verifyExactBoundaries(data, { 100, 100, 100, 100, 100 });
}

// =============================================================================
// Stage 15: Short segments between larger ones (confirmation window stress)
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage15_ShortMiddle_16elts)
{
    // Short segment (exactly minSegSize=16) between two large segments.
    // Requires M <= 3 to detect — M=4 can't fit a confirmation window.
    auto data = generateSample(
            {
                    { 5000, 6000, 200 },
                    { 0, 100, 16 },
                    { 8000, 9000, 200 },
            });
    printf("Stage 15: Short middle segment (16 elements)\n");
    verifyExactBoundaries(data, { 200, 16, 200 });
}

TEST_F(SplitByRangeGeneratorTest, Stage15_TwoShortMiddles)
{
    // Two short segments, each 16 elements, between larger ones.
    auto data = generateSample(
            {
                    { 5000, 6000, 100 },
                    { 0, 100, 16 },
                    { 8000, 9000, 100 },
                    { 200, 300, 16 },
                    { 12000, 13000, 100 },
            });
    printf("Stage 15: Two short middle segments (16 elements each)\n");
    verifyExactBoundaries(data, { 100, 16, 100, 16, 100 });
}

TEST_F(SplitByRangeGeneratorTest, Stage15_ShortFirst_16elts)
{
    // Short segment at the beginning.
    auto data = generateSample(
            {
                    { 0, 100, 16 },
                    { 5000, 6000, 200 },
            });
    printf("Stage 15: Short first segment (16 elements)\n");
    verifyExactBoundaries(data, { 16, 200 });
}

TEST_F(SplitByRangeGeneratorTest, Stage15_ShortLast_16elts)
{
    // Short segment at the end.
    auto data = generateSample(
            {
                    { 5000, 6000, 200 },
                    { 0, 100, 16 },
            });
    printf("Stage 15: Short last segment (16 elements)\n");
    verifyExactBoundaries(data, { 200, 16 });
}

TEST_F(SplitByRangeGeneratorTest, Stage15_ConsecutiveShort)
{
    // Two consecutive short (16-element) segments.
    auto data = generateSample(
            {
                    { 5000, 6000, 100 },
                    { 0, 100, 16 },
                    { 2000, 2100, 16 },
                    { 8000, 9000, 100 },
            });
    printf("Stage 15: Two consecutive short segments\n");
    verifyExactBoundaries(data, { 100, 16, 16, 100 });
}

// =============================================================================
// Stage 16: Repeated ranges with short separators
// =============================================================================

TEST_F(SplitByRangeGeneratorTest, Stage16_ABA_ShortB)
{
    // A-B-A where B is the minimum size (16 elements).
    auto data = generateSample(
            {
                    { 100, 200, 200 },
                    { 5000, 6000, 16 },
                    { 100, 200, 200 },
            });
    printf("Stage 16: A-B-A with short B (16 elements)\n");
    verifyExactBoundaries(data, { 200, 16, 200 });
}

TEST_F(SplitByRangeGeneratorTest, Stage16_ABAB_ShortB)
{
    // A-B-A-B where both B segments are short.
    auto data = generateSample(
            {
                    { 0, 100, 100 },
                    { 5000, 6000, 16 },
                    { 0, 100, 100 },
                    { 5000, 6000, 16 },
            });
    printf("Stage 16: A-B-A-B with short B segments\n");
    verifyExactBoundaries(data, { 100, 16, 100, 16 });
}

// =============================================================================
// Randomized generator: many test cases with varying parameters
//
// Default: 200 seeds (fast enough for CI).
// Override with env var SPLIT_BYRANGE_NUM_SEEDS for exhaustive local runs:
//   SPLIT_BYRANGE_NUM_SEEDS=10000 buck test ...
// =============================================================================

static unsigned getNumSeeds()
{
    const char* env = std::getenv("SPLIT_BYRANGE_NUM_SEEDS");
    if (env) {
        unsigned n = static_cast<unsigned>(std::strtoul(env, nullptr, 10));
        if (n > 0)
            return n;
    }
    return 200;
}

// Invariants that guarantee exact boundary detection for all generators:
//
// 1. Gap between adjacent ranges ≥ 2 × max(amp_left, amp_right).
//    The inter-range gap must dominate the intra-range amplitude on BOTH
//    sides. We pre-generate all amplitudes, then compute each gap based
//    on the actual adjacent pair — no global worst-case needed.
//
// 2. Each segment ≥ 256 elements (32 blocks at blockSize=8).
//    Well above the 7-block minimum for M=3 confirmation windows.
//
// 3. minSegSize=32 → blockSize=max(32/4,4)=8. With 8 elements per block,
//    the probability of the stability heuristic false-negative drops to
//    ~6e-9 per boundary. (With blockSize=4, it's ~2e-4.)
//    Custom minSegSize values are tested deterministically in Stage 13.
//
// These invariants are symmetric — ascending/descending/valley all use
// the same parameters.

static constexpr uint64_t AMP_MIN       = 50;
static constexpr uint64_t AMP_MAX       = 500;
static constexpr int FIXED_MIN_SEG_SIZE = 32;
static constexpr size_t MIN_SEG_ELTS    = 256;

// Helper: build ascending ranges from pre-generated parameters.
// Returns the number of ranges that fit in the value space.
// rangeData and expectedSizes are appended to.
static size_t buildAscendingRanges(
        DataGen& gen,
        uint64_t maxVal,
        int nbRanges,
        std::vector<uint64_t>& rangeData,
        std::vector<size_t>& expectedSizes)
{
    size_t const minSeg = MIN_SEG_ELTS;

    // Clamp ampMax so that nbRanges ranges can fit in [0, maxVal].
    // With N ranges of amplitude A: total = N*A + (N-1)*(2*A+1) =
    // A*(3N-2)+(N-1). So A ≤ (maxVal - N + 1) / (3N - 2).
    uint64_t ampMax = AMP_MAX;
    uint64_t ampMin = AMP_MIN;
    if (maxVal < UINT64_MAX && nbRanges >= 1) {
        uint64_t denom  = (uint64_t)(3 * nbRanges - 2);
        uint64_t numer  = (maxVal >= (uint64_t)(nbRanges - 1))
                 ? maxVal - (uint64_t)(nbRanges - 1)
                 : 0;
        uint64_t budget = numer / denom;
        // Allow smaller amplitudes for narrow types (e.g., u8).
        // Floor at 10 to ensure blocks have meaningful variation.
        ampMin = std::min(ampMin, std::max(budget / 2, (uint64_t)10));
        if (budget < ampMin)
            return 0; // value space too small for this many ranges
        ampMax = std::min(ampMax, budget);
    }

    // Phase 1: pre-generate all amplitudes and segment sizes so that we
    // know adjacent amplitudes when computing gaps.
    std::vector<size_t> segSizes;
    std::vector<uint64_t> amplitudes;
    for (int r = 0; r < nbRanges; r++) {
        segSizes.push_back(gen.usize_range("segSize", minSeg, minSeg * 3));
        amplitudes.push_back(gen.u64_range("amp", ampMin, ampMax));
    }

    // Phase 2: lay out ranges in ascending order. The gap between range i
    // and range i+1 is ≥ 2 × max(amp[i], amp[i+1]), ensuring the inter-
    // range separation always dominates the intra-range variation on BOTH
    // sides of every boundary.
    uint64_t cursor    = 0;
    size_t rangesBuilt = 0;
    for (int r = 0; r < nbRanges; r++) {
        uint64_t amplitude = amplitudes[r];
        if (cursor + amplitude > maxVal) {
            if (r == 0) {
                amplitude = maxVal > cursor ? maxVal - cursor : 0;
            } else {
                break;
            }
        }

        uint64_t lo = cursor;
        uint64_t hi = std::min(cursor + amplitude, maxVal);

        for (size_t i = 0; i < segSizes[r]; i++) {
            rangeData.push_back(gen.u64_range("val", lo, hi));
        }
        expectedSizes.push_back(segSizes[r]);
        rangesBuilt++;

        // Gap to next range: based on max of this and next amplitude.
        if (r + 1 < nbRanges) {
            uint64_t gapBase = std::max(amplitude, amplitudes[(size_t)r + 1]);
            uint64_t minGap  = gapBase * 2 + 1;
            uint64_t maxGap  = gapBase * 4 + 100;
            // Cap maxGap so remaining ranges can still fit.
            // Each remaining range needs at least ampMin + minGap space.
            int remaining = nbRanges - r - 1;
            if (remaining > 0 && maxVal < UINT64_MAX) {
                uint64_t reservePerRange = ampMin + ampMin * 2 + 1;
                uint64_t reserved = (uint64_t)remaining * reservePerRange;
                if (hi + minGap + reserved <= maxVal) {
                    uint64_t slack = maxVal - hi - reserved;
                    maxGap         = std::min(maxGap, slack);
                }
            }
            if (maxGap < minGap)
                maxGap = minGap;
            uint64_t gap = gen.u64_range("gap", minGap, maxGap);
            cursor       = hi + gap;
            if (cursor > maxVal)
                break;
        }
    }
    return rangesBuilt;
}

// Dispatch rangeData (uint64_t) to width-specific verification.
static void verifyMultiWidth(
        SplitByRangeGeneratorTest& test,
        size_t eltWidth,
        const std::vector<uint64_t>& rangeData,
        const std::vector<size_t>& expectedSizes)
{
    switch (eltWidth) {
        case 1: {
            std::vector<uint8_t> typed(rangeData.begin(), rangeData.end());
            test.verifyTypedExactBoundaries<uint8_t>(
                    typed, expectedSizes, FIXED_MIN_SEG_SIZE);
            break;
        }
        case 2: {
            std::vector<uint16_t> typed(rangeData.begin(), rangeData.end());
            test.verifyTypedExactBoundaries<uint16_t>(
                    typed, expectedSizes, FIXED_MIN_SEG_SIZE);
            break;
        }
        case 4: {
            std::vector<uint32_t> typed(rangeData.begin(), rangeData.end());
            test.verifyTypedExactBoundaries<uint32_t>(
                    typed, expectedSizes, FIXED_MIN_SEG_SIZE);
            break;
        }
        case 8: {
            test.verifyTypedExactBoundaries<uint64_t>(
                    rangeData, expectedSizes, FIXED_MIN_SEG_SIZE);
            break;
        }
    }
}

// Generate one ascending-range test case from a seed.
static bool runAscendingRangeTest(
        SplitByRangeGeneratorTest& test,
        unsigned seed)
{
    DataGen gen(seed);

    size_t const eltWidth = size_t(1) << (int)gen.u32_range("eltW", 0, 3);
    uint64_t const maxVal =
            (eltWidth == 8) ? UINT64_MAX : (uint64_t(1) << (eltWidth * 8)) - 1;
    int const nbRanges = (int)gen.u32_range("nbRanges", 1, 8);

    std::vector<uint64_t> rangeData;
    std::vector<size_t> expectedSizes;
    size_t built = buildAscendingRanges(
            gen, maxVal, nbRanges, rangeData, expectedSizes);

    if (built == 0)
        return false;

    verifyMultiWidth(test, eltWidth, rangeData, expectedSizes);
    return true;
}

// Generate one descending-range test case from a seed.
// Uses the same invariants as ascending (the algorithm is symmetric).
// Builds ascending ranges then reverses segment order — structurally different
// from ascending but with identical numeric parameters and multi-width
// coverage.
static bool runDescendingRangeTest(
        SplitByRangeGeneratorTest& test,
        unsigned seed)
{
    DataGen gen(seed);

    size_t const eltWidth = size_t(1) << (int)gen.u32_range("eltW", 0, 3);
    uint64_t const maxVal =
            (eltWidth == 8) ? UINT64_MAX : (uint64_t(1) << (eltWidth * 8)) - 1;
    int const nbRanges = (int)gen.u32_range("nbRanges", 2, 8);

    // Build ascending ranges to get segment boundaries and values.
    std::vector<uint64_t> ascData;
    std::vector<size_t> ascSizes;
    size_t built =
            buildAscendingRanges(gen, maxVal, nbRanges, ascData, ascSizes);

    if (built < 2)
        return false;

    // Reverse segment order to create descending pattern.
    std::vector<uint64_t> rangeData;
    std::vector<size_t> expectedSizes;
    size_t pos = ascData.size();
    for (size_t i = ascSizes.size(); i > 0; i--) {
        size_t sz = ascSizes[i - 1];
        pos -= sz;
        rangeData.insert(
                rangeData.end(),
                ascData.begin() + pos,
                ascData.begin() + pos + sz);
        expectedSizes.push_back(sz);
    }

    verifyMultiWidth(test, eltWidth, rangeData, expectedSizes);
    return true;
}

// Generate one valley-pattern test case from a seed.
// Valley = descending then ascending ranges (e.g., high-low-high).
// Uses the same invariants as ascending/descending (the algorithm is
// symmetric). Multi-width coverage matches ascending.
static bool runValleyRangeTest(SplitByRangeGeneratorTest& test, unsigned seed)
{
    DataGen gen(seed);

    size_t const eltWidth = size_t(1) << (int)gen.u32_range("eltW", 0, 3);
    uint64_t const maxVal =
            (eltWidth == 8) ? UINT64_MAX : (uint64_t(1) << (eltWidth * 8)) - 1;
    int const nbRanges = (int)gen.u32_range("nbRanges", 3, 8);

    // Build ascending ranges to get segment boundaries and values.
    std::vector<uint64_t> ascData;
    std::vector<size_t> ascSizes;
    size_t built =
            buildAscendingRanges(gen, maxVal, nbRanges, ascData, ascSizes);

    if (built < 3)
        return false;

    // Rearrange into valley: first half reversed, then second half in order.
    // E.g., for segments [A, B, C, D, E]: valley = [B, A, C, D, E]
    //   (first half [A,B] reversed → [B,A], then [C,D,E] in order)
    size_t mid = built / 2;

    // Compute segment start offsets in ascData.
    std::vector<size_t> offsets(built);
    offsets[0] = 0;
    for (size_t i = 1; i < built; i++) {
        offsets[i] = offsets[i - 1] + ascSizes[i - 1];
    }

    std::vector<uint64_t> rangeData;
    std::vector<size_t> expectedSizes;

    // First half reversed (indices mid-1, mid-2, ..., 0)
    for (size_t i = mid; i > 0; i--) {
        size_t idx = i - 1;
        rangeData.insert(
                rangeData.end(),
                ascData.begin() + offsets[idx],
                ascData.begin() + offsets[idx] + ascSizes[idx]);
        expectedSizes.push_back(ascSizes[idx]);
    }
    // Second half in order (indices mid, mid+1, ..., built-1)
    for (size_t i = mid; i < built; i++) {
        rangeData.insert(
                rangeData.end(),
                ascData.begin() + offsets[i],
                ascData.begin() + offsets[i] + ascSizes[i]);
        expectedSizes.push_back(ascSizes[i]);
    }

    verifyMultiWidth(test, eltWidth, rangeData, expectedSizes);
    return true;
}

TEST_F(SplitByRangeGeneratorTest, Randomized_AscendingRanges)
{
    unsigned const numSeeds = getNumSeeds();
    int tested              = 0;
    for (unsigned seed = 0; seed < numSeeds; seed++) {
        SetUp();
        try {
            if (runAscendingRangeTest(*this, seed)) {
                tested++;
            }
        } catch (const std::exception& e) {
            FAIL() << "Seed " << seed << " failed: " << e.what();
        }
    }
    printf("Randomized ascending: %d / %u seeds verified\n", tested, numSeeds);
    EXPECT_GE(tested, (int)(numSeeds * 95 / 100));
}

TEST_F(SplitByRangeGeneratorTest, Randomized_DescendingRanges)
{
    unsigned const numSeeds = getNumSeeds();
    int tested              = 0;
    for (unsigned seed = 0; seed < numSeeds; seed++) {
        SetUp();
        try {
            if (runDescendingRangeTest(*this, seed)) {
                tested++;
            }
        } catch (const std::exception& e) {
            FAIL() << "Seed " << seed << " failed: " << e.what();
        }
    }
    printf("Randomized descending: %d / %u seeds verified\n", tested, numSeeds);
    EXPECT_GE(tested, (int)(numSeeds * 95 / 100));
}

TEST_F(SplitByRangeGeneratorTest, Randomized_ValleyRanges)
{
    unsigned const numSeeds = getNumSeeds();
    int tested              = 0;
    for (unsigned seed = 0; seed < numSeeds; seed++) {
        SetUp();
        try {
            if (runValleyRangeTest(*this, seed)) {
                tested++;
            }
        } catch (const std::exception& e) {
            FAIL() << "Seed " << seed << " failed: " << e.what();
        }
    }
    printf("Randomized valley: %d / %u seeds verified\n", tested, numSeeds);
    EXPECT_GE(tested, (int)(numSeeds * 95 / 100));
}

} // namespace tests
} // namespace openzl
