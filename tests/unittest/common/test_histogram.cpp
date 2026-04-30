// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <limits>
#include <random>
#include <unordered_map>

#include <gtest/gtest.h>

#include "openzl/shared/histogram.h"
#include "tests/datagen/random_producer/compat_uniform_distribution.h"

using namespace ::testing;

namespace {
template <typename Int>
class HistogramTest : public Test {
   public:
    std::unordered_map<Int, size_t> buildHistogram(
            Int const* src,
            size_t srcSize) const
    {
        std::unordered_map<Int, size_t> histogram;
        for (size_t i = 0; i < srcSize; ++i) {
            histogram[src[i]]++;
        }
        return histogram;
    }

    void checkHistogram(
            ZL_Histogram const* histogram,
            size_t maxSymbolValue,
            Int const* src,
            size_t srcSize) const
    {
        ASSERT_EQ(histogram->elementSize, sizeof(Int));
        auto const expected = buildHistogram(src, srcSize);
        size_t numNonZero   = 0;
        size_t largest      = 0;
        size_t maxSymbol    = 0;
        for (size_t i = 0; i < maxSymbolValue + 1; ++i) {
            if (histogram->count[i] > largest) {
                largest = histogram->count[i];
            }
            auto it = expected.find((Int)i);
            if (it == expected.end()) {
                ASSERT_EQ(histogram->count[i], 0u);
            } else {
                maxSymbol = i;
                ASSERT_LE(i, histogram->maxSymbol);
                ASSERT_NE(it->second, 0u);
                ASSERT_EQ(histogram->count[i], it->second);
                ++numNonZero;
            }
        }
        ASSERT_EQ(numNonZero, expected.size());
        ASSERT_EQ(histogram->largestCount, largest);
        ASSERT_EQ(histogram->total, srcSize);
        ASSERT_EQ(histogram->maxSymbol, maxSymbol);
        ASSERT_EQ(histogram->cardinality, numNonZero);
    }
};

class IntNameGenerator {
   public:
    template <typename T>
    static std::string GetName(int)
    {
        if constexpr (std::is_same_v<T, uint8_t>)
            return "uint8_t";
        if constexpr (std::is_same_v<T, uint16_t>)
            return "uint16_t";
        return "unknown";
    }
};

using Ints = ::testing::Types<uint8_t, uint16_t>;
TYPED_TEST_SUITE(HistogramTest, Ints, IntNameGenerator);

TYPED_TEST(HistogramTest, Empty)
{
    auto histogram = ZL_Histogram_create(0);
    ZL_Histogram_build(histogram, nullptr, 0, sizeof(TypeParam));

    this->checkHistogram(histogram, 0, nullptr, 0);

    ZL_Histogram_destroy(histogram);
}

TYPED_TEST(HistogramTest, SingleZero)
{
    TypeParam val  = 0;
    auto histogram = ZL_Histogram_create(0);
    ZL_Histogram_build(histogram, &val, 1, sizeof(TypeParam));

    this->checkHistogram(histogram, 0, &val, 1);

    ZL_Histogram_destroy(histogram);
}

TYPED_TEST(HistogramTest, SingleMax)
{
    TypeParam val  = std::numeric_limits<TypeParam>::max();
    auto histogram = ZL_Histogram_create(val);
    ZL_Histogram_build(histogram, &val, 1, sizeof(TypeParam));

    this->checkHistogram(histogram, val, &val, 1);

    ZL_Histogram_destroy(histogram);
}

TYPED_TEST(HistogramTest, TwoValues)
{
    std::vector<TypeParam> vals;
    vals.push_back(1);
    for (size_t i = 0; i < 100; ++i) {
        vals.push_back(std::numeric_limits<TypeParam>::max() / 2);
    }
    vals.push_back(1);

    auto histogram =
            ZL_Histogram_create(std::numeric_limits<TypeParam>::max() / 2);
    ZL_Histogram_build(histogram, vals.data(), vals.size(), sizeof(TypeParam));

    this->checkHistogram(
            histogram,
            std::numeric_limits<TypeParam>::max() / 2,
            vals.data(),
            vals.size());

    ZL_Histogram_destroy(histogram);
}

TYPED_TEST(HistogramTest, RandomHistogram)
{
    using compat_dist =
            openzl::tests::datagen::compat_uniform_int_distribution<TypeParam>;
    std::mt19937 gen(0xdeadbeef);
    for (size_t i = 0; i < 100; ++i) {
        std::vector<TypeParam> vals;
        auto max = compat_dist{}(gen);
        auto const numVals =
                std::uniform_int_distribution<size_t>{ 0, 1000 }(gen);
        compat_dist dist{ 0, max };
        for (size_t j = 0; j < numVals; ++j) {
            vals.push_back(dist(gen));
        }
        auto histogram = ZL_Histogram_create(max);
        ZL_Histogram_build(
                histogram, vals.data(), vals.size(), sizeof(TypeParam));
        this->checkHistogram(histogram, max, vals.data(), vals.size());
        ZL_Histogram_destroy(histogram);
    }
}

} // namespace
