// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/shared/estimate.h"
#include "tests/datagen/random_producer/compat_uniform_distribution.h"

namespace {
template <typename Int>
std::vector<Int> generateFixedData(size_t cardinality)
{
    using compat_dist =
            openzl::tests::datagen::compat_uniform_int_distribution<Int>;
    std::mt19937 gen(42);
    std::unordered_set<Int> tokens;
    if (cardinality > (size_t)std::numeric_limits<Int>::max()) {
        for (size_t i = 0; i < cardinality; ++i) {
            tokens.insert(Int(i));
        }
    } else {
        compat_dist dis;
        while (tokens.size() < cardinality) {
            tokens.insert(dis(gen));
        }
    }
    std::vector<Int> values;
    while (values.size() <= 65536) {
        values.insert(values.end(), tokens.begin(), tokens.end());
    }
    std::shuffle(values.begin(), values.end(), gen);
    return values;
}

struct VariableData {
    std::vector<std::string> data;
    std::vector<void const*> ptrs;
    std::vector<size_t> sizes;
};

VariableData generateVariableData(size_t cardinality, size_t size)
{
    std::mt19937 gen(42);
    std::unordered_set<std::string> tokens;
    std::uniform_int_distribution<size_t> len(0, 100);
    openzl::tests::datagen::compat_uniform_int_distribution<int8_t> chr;

    while (tokens.size() < cardinality) {
        std::string x;
        size_t const l = len(gen);
        x.reserve(l);
        for (size_t i = 0; i < l; ++i)
            x.push_back(chr(gen));
        tokens.insert(std::move(x));
    }
    std::vector<std::string> values;
    while (values.size() < size) {
        values.insert(values.end(), tokens.begin(), tokens.end());
    }
    std::shuffle(values.begin(), values.end(), gen);
    VariableData data;
    data.ptrs.reserve(values.size());
    data.sizes.reserve(values.size());
    data.data = std::move(values);
    for (auto const& s : data.data) {
        data.ptrs.push_back(s.data());
        data.sizes.push_back(s.size());
    }
    return data;
}

void validateEstimate(
        ZL_CardinalityEstimate const& estimate,
        size_t cardinality,
        size_t earlyExit = size_t(-1))
{
    ASSERT_LE(estimate.lowerBound, estimate.estimateLowerBound);
    ASSERT_LE(estimate.estimateLowerBound, estimate.estimate);
    ASSERT_LE(estimate.estimate, estimate.estimateUpperBound);
    ASSERT_LE(estimate.estimateUpperBound, estimate.upperBound);

    ASSERT_GE(cardinality, estimate.lowerBound);
    ASSERT_LE(cardinality, estimate.upperBound);

    if (cardinality < earlyExit) {
        // These could fail on unlucky test data.
        ASSERT_GE(cardinality, estimate.estimateLowerBound);
        ASSERT_LE(cardinality, estimate.estimateUpperBound);
    }
}

template <typename Int>
void testEstimateFixedWithCardinality(size_t cardinality)
{
    auto const data = generateFixedData<Int>(cardinality);
    for (auto earlyExit : { 0, 128, 256, 1024, 65536, 131072, 1 << 30 }) {
        auto const estimate = ZL_estimateCardinality_fixed(
                data.data(), data.size(), sizeof(Int), size_t(earlyExit));
        validateEstimate(estimate, cardinality, size_t(earlyExit));
    }
}

template <typename Int>
void testEstimateFixed()
{
    for (size_t cardinality = 1; cardinality <= 131072; cardinality <<= 1) {
        if (cardinality > std::numeric_limits<Int>::max() + 1)
            break;
        testEstimateFixedWithCardinality<Int>(cardinality);
    }
}

void testEstimateVariable()
{
    for (size_t cardinality = 1; cardinality <= 131072; cardinality <<= 1) {
        auto const data = generateVariableData(cardinality, 2 * cardinality);
        auto const estimate = ZL_estimateCardinality_variable(
                data.ptrs.data(),
                data.sizes.data(),
                data.ptrs.size(),
                ZL_ESTIMATE_CARDINALITY_ANY);
        validateEstimate(estimate, cardinality);
    }
}

template <typename Int>
void testComputeUnsignedRange()
{
    using compat_dist =
            openzl::tests::datagen::compat_uniform_int_distribution<Int>;
    std::mt19937 gen(42);
    compat_dist bounds(
            std::numeric_limits<Int>::min(), std::numeric_limits<Int>::max());
    for (size_t i = 0; i < 1000; ++i) {
        auto const bound1 = bounds(gen);
        auto const bound2 = bounds(gen);
        auto const min    = std::min(bound1, bound2);
        auto const max    = std::max(bound1, bound2);
        compat_dist value(min, max);
        std::vector<Int> values;
        values.reserve(100);
        for (size_t j = 0; j < 100; ++j) {
            values.push_back(value(gen));
        }
        auto const range = ZL_computeUnsignedRange(
                values.data(), values.size(), sizeof(Int));
        ASSERT_GE(range.min, min);
        ASSERT_LE(range.max, max);
        ASSERT_EQ(range.min, *std::min_element(values.begin(), values.end()));
        ASSERT_EQ(range.max, *std::max_element(values.begin(), values.end()));
    }
}

template <typename Int>
std::vector<Int> genStridedData(int stride)
{
    using compat_dist =
            openzl::tests::datagen::compat_uniform_int_distribution<Int>;
    std::mt19937 gen(42);
    compat_dist dist;
    std::bernoulli_distribution match(0.5);
    std::bernoulli_distribution copyStride(0.9);
    std::poisson_distribution<int> offset(1.5);

    int const dataSize = std::max<int>(1000, 100 * stride);
    std::vector<Int> data;
    data.reserve((size_t)dataSize);
    for (int i = 0; i < dataSize; ++i) {
        data.push_back(dist(gen));
    }
    for (int i = stride; i < dataSize; ++i) {
        if (match(gen)) {
            auto const off = std::max<int>(offset(gen), 1);
            if (stride > 0 && copyStride(gen)) {
                int const m = stride * off <= i ? i - stride * off : i - stride;
                data[(size_t)i] = data[(size_t)m];
            } else {
                data[(size_t)i] = data[(size_t)std::max<int>(0, i - off)];
            }
        }
    }
    return data;
}

template <typename Int>
void testDimensionalityEstimate()
{
    {
        auto const data     = genStridedData<Int>(0);
        auto const estimate = ZL_estimateDimensionality(
                data.data(), data.size(), sizeof(Int));
        ASSERT_NE(estimate.dimensionality, ZL_DimensionalityStatus_likely2D);
    }
    std::array<int, 19> kStrides = { 2,  3,  4,  5,  6,  7,   8,   9,   10,  16,
                                     17, 27, 31, 36, 81, 128, 200, 256, 1024 };
    for (auto const stride : kStrides) {
        auto const data     = genStridedData<Int>(stride);
        auto const estimate = ZL_estimateDimensionality(
                data.data(), data.size(), sizeof(Int));
        if (sizeof(Int) != 1 || stride <= 256) {
            ASSERT_EQ(
                    estimate.dimensionality, ZL_DimensionalityStatus_likely2D);
        } else {
            ASSERT_NE(estimate.dimensionality, ZL_DimensionalityStatus_none);
            ASSERT_EQ((int)estimate.stride, stride);
        }
    }
}
} // namespace

TEST(Estimate, FixedU8)
{
    testEstimateFixed<uint8_t>();
}

TEST(Estimate, FixedU16)
{
    testEstimateFixed<uint16_t>();
}

TEST(Estimate, FixedU32)
{
    testEstimateFixed<uint32_t>();
}

TEST(Estimate, FixedU64)
{
    testEstimateFixed<uint64_t>();
}

TEST(Estimate, Variable)
{
    testEstimateVariable();
}

TEST(Estimate, ComputeUnsignedRange8)
{
    testComputeUnsignedRange<uint8_t>();
}

TEST(Estimate, ComputeUnsignedRange16)
{
    testComputeUnsignedRange<uint16_t>();
}

TEST(Estimate, ComputeUnsignedRange32)
{
    testComputeUnsignedRange<uint32_t>();
}

TEST(Estimate, ComputeUnsignedRange64)
{
    testComputeUnsignedRange<uint64_t>();
}

TEST(Estimate, DimensionalityEstimateU8)
{
    testDimensionalityEstimate<uint8_t>();
}

TEST(Estimate, DimensionalityEstimateU16)
{
    testDimensionalityEstimate<uint16_t>();
}

TEST(Estimate, DimensionalityEstimateU32)
{
    testDimensionalityEstimate<uint32_t>();
}

TEST(Estimate, DimensionalityEstimateU64)
{
    testDimensionalityEstimate<uint64_t>();
}

TEST(Estimate, GuessFloatWidth)
{
    std::mt19937 gen(42);
    std::normal_distribution<double> dist;
    std::vector<double> data64;
    data64.reserve(8192);
    for (size_t i = 0; i < 8192; ++i) {
        data64.push_back(dist(gen));
    }
    std::vector<float> data32(data64.begin(), data64.end());
    std::vector<uint16_t> data16(8192);
    std::vector<uint8_t> data8(8192);

    for (size_t i = 0; i < 8192; ++i) {
        memcpy(&data16[i], ((const char*)&data32[i]) + 2, 2);
        memcpy(&data8[i], ((const char*)&data32[i]) + 3, 1);
    }

    ASSERT_EQ(ZL_guessFloatWidth(data8.data(), data8.size()), 1u);
    ASSERT_EQ(ZL_guessFloatWidth(data16.data(), data16.size() * 2), 2u);
    ASSERT_EQ(ZL_guessFloatWidth(data32.data(), data32.size() * 4), 4u);
    ASSERT_EQ(ZL_guessFloatWidth(data64.data(), data64.size() * 8), 8u);

    for (size_t i = 0; i < 8192; ++i) {
        memcpy(&data16[i], &data32[i], 2);
        data8[i] = (char)data32[i];
    }

    ASSERT_EQ(ZL_guessFloatWidth(data8.data(), data8.size()), 1u);
    ASSERT_EQ(ZL_guessFloatWidth(data16.data(), data16.size() * 2), 1u);

    openzl::tests::datagen::compat_uniform_int_distribution<uint8_t> dist8;
    for (size_t i = 0; i < 8192; ++i) {
        data8[i] = dist8(gen);
    }
    ASSERT_EQ(ZL_guessFloatWidth(data8.data(), data8.size()), 1u);
}
