// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "contrib/gpu/testkit/bytes.h"
#include "contrib/gpu/testkit/nimble_workloads.h"
#include "openzl/openzl.hpp"
#include "openzl/zl_version.h"

namespace openzl::gpu::testkit {
namespace {

template <typename T>
size_t distinctCount(const std::vector<T>& values)
{
    return std::set<T>(values.begin(), values.end()).size();
}

std::string generateValues(const WorkloadSpec& spec, size_t numElts)
{
    return generateFrame(spec, numElts * elementByteWidth(spec.type));
}

std::string decompress(const std::string& frame)
{
    openzl::DCtx dctx;
    return dctx.decompressSerial(frame);
}

TEST(NimbleWorkloadsTest, sameSpecGivesSameBytesAndSeedChangesThem)
{
    const WorkloadSpec spec = {
        .type    = NimbleNumericType::F32,
        .pattern = FloatNormal{ .mean = 0, .stddev = 1 },
        .seed    = 7,
    };
    WorkloadSpec otherSeed = spec;
    otherSeed.seed         = 8;

    EXPECT_EQ(generateFrame(spec, 32768), generateFrame(spec, 32768));
    EXPECT_NE(generateFrame(spec, 32768), generateFrame(otherSeed, 32768));
}

TEST(NimbleWorkloadsTest, generateFrameReturnsExactlyTheRequestedBytes)
{
    const std::vector<WorkloadSpec> specs = {
        { .type    = NimbleNumericType::F32,
          .pattern = FloatNormal{ .mean = 0, .stddev = 1 } },
        { .type = NimbleNumericType::U32,
          .pattern =
                  PinnedValues{ .base = 0, .range = 1000, .cardinality = 20 } },
        { .type    = NimbleNumericType::F32,
          .pattern = PlannedFloats{ .literals = RandomFloatBits{} } },
    };

    for (const WorkloadSpec& spec : specs) {
        EXPECT_EQ(generateFrame(spec, 4096).size(), 4096);
    }
}

TEST(NimbleWorkloadsTest, generateFrameRejectsPartialElements)
{
    const WorkloadSpec spec = {
        .type    = NimbleNumericType::F32,
        .pattern = FloatNormal{ .mean = 0, .stddev = 1 },
    };

    EXPECT_THROW(generateFrame(spec, 4097), std::invalid_argument);
}

TEST(NimbleWorkloadsTest, floatNormalHasTheRequestedMeanAndStddev)
{
    const auto values = fromLittleEndianBytes<float>(generateValues(
            {
                    .type    = NimbleNumericType::F32,
                    .pattern = FloatNormal{ .mean = 3.0, .stddev = 2.0 },
            },
            100000));

    double sum   = 0;
    double sumSq = 0;
    for (const float value : values) {
        sum += value;
        sumSq += static_cast<double>(value) * value;
    }
    const double mean   = sum / values.size();
    const double stddev = std::sqrt(sumSq / values.size() - mean * mean);
    EXPECT_NEAR(mean, 3.0, 0.05);
    EXPECT_NEAR(stddev, 2.0, 0.05);
}

TEST(NimbleWorkloadsTest, rejectsPatternsThatDoNotMatchTheType)
{
    EXPECT_THROW(
            generateValues(
                    {
                            .type    = NimbleNumericType::U32,
                            .pattern = FloatNormal{ .mean = 0, .stddev = 1 },
                    },
                    10),
            std::invalid_argument);
    EXPECT_THROW(
            generateValues(
                    {
                            .type = NimbleNumericType::U64,
                            .pattern =
                                    PlannedFloats{ .literals =
                                                           RandomFloatBits{} },
                    },
                    10),
            std::invalid_argument);
}

TEST(NimbleWorkloadsTest, pinnedValuesHoldTheRangeEndsAndCardinality)
{
    const WorkloadSpec spec = {
        .type    = NimbleNumericType::U32,
        .pattern = PinnedValues{ .base        = 5000,
                                 .range       = 65535,
                                 .cardinality = 300 },
    };
    for (uint64_t seed = 1; seed <= 3; ++seed) {
        WorkloadSpec seeded = spec;
        seeded.seed         = seed;

        const auto values =
                fromLittleEndianBytes<uint32_t>(generateValues(seeded, 1000));

        const auto [low, high] =
                std::minmax_element(values.begin(), values.end());
        EXPECT_EQ(*low, 5000U);
        EXPECT_EQ(*high, 5000U + 65535U);
        EXPECT_EQ(distinctCount(values), 300U);
    }
}

TEST(NimbleWorkloadsTest, pinnedValuesForF32AreFloatBitPatterns)
{
    // 1.0f and the 255 floats above it.
    const WorkloadSpec spec = {
        .type    = NimbleNumericType::F32,
        .pattern = PinnedValues{ .base        = 0x3F800000,
                                 .range       = 255,
                                 .cardinality = 40 },
    };

    const auto bits =
            fromLittleEndianBytes<uint32_t>(generateValues(spec, 1000));

    const auto [low, high] = std::minmax_element(bits.begin(), bits.end());
    EXPECT_EQ(*low, 0x3F800000U);
    EXPECT_EQ(*high, 0x3F800000U + 255U);
    EXPECT_EQ(distinctCount(bits), 40U);
}

TEST(NimbleWorkloadsTest, pinnedValuesWithoutCardinalityAreDistinct)
{
    const auto values = fromLittleEndianBytes<uint64_t>(generateValues(
            {
                    .type    = NimbleNumericType::U64,
                    .pattern = PinnedValues{ .base        = 0,
                                             .range       = UINT64_MAX,
                                             .cardinality = 0 },
            },
            1000));

    EXPECT_EQ(distinctCount(values), values.size());
    EXPECT_EQ(*std::min_element(values.begin(), values.end()), 0U);
    EXPECT_EQ(*std::max_element(values.begin(), values.end()), UINT64_MAX);
}

TEST(NimbleWorkloadsTest, pinnedValuesFillASmallRangeWhenTheFrameIsLarger)
{
    const auto values = fromLittleEndianBytes<uint16_t>(generateValues(
            {
                    .type    = NimbleNumericType::U16,
                    .pattern = PinnedValues{ .base        = 0,
                                             .range       = 999,
                                             .cardinality = 0 },
            },
            5000));

    EXPECT_EQ(distinctCount(values), 1000U);
}

TEST(NimbleWorkloadsTest, pinnedValuesRejectImpossibleParameters)
{
    const auto generate = [](uint64_t base, uint64_t range, uint64_t k) {
        return generateValues(
                { .type    = NimbleNumericType::U16,
                  .pattern = PinnedValues{ .base        = base,
                                           .range       = range,
                                           .cardinality = k } },
                100);
    };

    EXPECT_THROW(generate(10, 65535, 0), std::invalid_argument);
    EXPECT_THROW(generate(0, 1000, 1), std::invalid_argument);
    EXPECT_THROW(generate(0, 50, 52), std::invalid_argument);
    EXPECT_THROW(generate(0, 1000, 101), std::invalid_argument);
}

TEST(NimbleWorkloadsTest, framesOfOneColumnShareTheAlphabet)
{
    const WorkloadSpec spec = {
        .type    = NimbleNumericType::U32,
        .pattern = PinnedValues{ .base        = 1000,
                                 .range       = 1 << 20,
                                 .cardinality = 50 },
    };

    // Every frame holds all 50 alphabet values.
    const auto first =
            fromLittleEndianBytes<uint32_t>(generateFrame(spec, 40000, 0));
    const auto second =
            fromLittleEndianBytes<uint32_t>(generateFrame(spec, 40000, 1));

    EXPECT_NE(first, second);
    EXPECT_EQ(
            std::set<uint32_t>(first.begin(), first.end()),
            std::set<uint32_t>(second.begin(), second.end()));
}

TEST(NimbleWorkloadsTest, makeFramesMatchesGenerateFrame)
{
    // PinnedValues also checks that makeFrames() builds the same column
    // tables as generateFrame().
    const std::vector<WorkloadSpec> specs = {
        { .type    = NimbleNumericType::F32,
          .pattern = FloatNormal{ .mean = 0, .stddev = 1 },
          .seed    = 10 },
        { .type    = NimbleNumericType::U32,
          .pattern = PinnedValues{ .base        = 1000,
                                   .range       = 1 << 20,
                                   .cardinality = 50 },
          .seed    = 10 },
    };

    for (const WorkloadSpec& spec : specs) {
        const std::vector<Frame> frames = makeFrames(spec, 8192, 3);

        ASSERT_EQ(frames.size(), 3U);
        for (size_t i = 0; i < frames.size(); ++i) {
            EXPECT_EQ(frames[i].raw, generateFrame(spec, 8192, i));
            EXPECT_EQ(decompress(frames[i].compressed), frames[i].raw);
        }
    }
}

TEST(NimbleWorkloadsTest, seedChangesThePinnedValuesAlphabet)
{
    WorkloadSpec spec = {
        .type    = NimbleNumericType::U32,
        .pattern = PinnedValues{ .base        = 1000,
                                 .range       = 1 << 20,
                                 .cardinality = 50 },
        .seed    = 1,
    };
    const auto first =
            fromLittleEndianBytes<uint32_t>(generateFrame(spec, 40000));
    spec.seed = 2;
    const auto second =
            fromLittleEndianBytes<uint32_t>(generateFrame(spec, 40000));

    EXPECT_NE(
            std::set<uint32_t>(first.begin(), first.end()),
            std::set<uint32_t>(second.begin(), second.end()));
}

TEST(NimbleWorkloadsTest, floatNormalRejectsInvalidParameters)
{
    for (const FloatNormal& p :
         { FloatNormal{ .mean = 0, .stddev = 0 },
           FloatNormal{ .mean = 0, .stddev = -1 },
           FloatNormal{ .mean = 0, .stddev = NAN },
           FloatNormal{ .mean = INFINITY, .stddev = 1 } }) {
        EXPECT_THROW(
                generateValues(
                        { .type = NimbleNumericType::F32, .pattern = p }, 10),
                std::invalid_argument);
    }
}

TEST(NimbleWorkloadsTest, pinnedValuesCheckTheFrameSizeBeforeTheAlphabet)
{
    // A 2^32-value alphabet fits the type but not a 100-value frame. The
    // check must come before the generator draws the alphabet.
    const WorkloadSpec spec = {
        .type    = NimbleNumericType::U32,
        .pattern = PinnedValues{ .base        = 0,
                                 .range       = UINT32_MAX,
                                 .cardinality = uint64_t{ 1 } << 32 },
    };

    EXPECT_THROW(generateValues(spec, 100), std::invalid_argument);
}

TEST(NimbleWorkloadsTest, makeFramesDoesNotDependOnThreads)
{
    const WorkloadSpec spec = {
        .type    = NimbleNumericType::U32,
        .pattern = PinnedValues{ .base        = 1000,
                                 .range       = 1 << 20,
                                 .cardinality = 50 },
    };

    const std::vector<Frame> one  = makeFrames(spec, 8192, 5, {}, 1);
    const std::vector<Frame> four = makeFrames(spec, 8192, 5, {}, 4);

    ASSERT_EQ(one.size(), 5U);
    ASSERT_EQ(four.size(), 5U);

    for (size_t i = 0; i < one.size(); ++i) {
        EXPECT_EQ(one[i].raw, four[i].raw);
        EXPECT_EQ(one[i].compressed, four[i].compressed);
    }
}

TEST(NimbleWorkloadsTest, makeFramesRethrowsAnErrorFromAWorkerThread)
{
    // The spec is valid, so the error comes from compressing each frame inside
    // the worker threads.
    const WorkloadSpec spec = {
        .type    = NimbleNumericType::U32,
        .pattern = PinnedValues{ .base        = 1000,
                                 .range       = 1 << 20,
                                 .cardinality = 50 },
    };

    EXPECT_THROW(
            makeFrames(
                    spec,
                    8192,
                    4,
                    { .formatVersion = ZL_MAX_FORMAT_VERSION + 1 },
                    4),
            openzl::Exception);
}

} // namespace
} // namespace openzl::gpu::testkit
