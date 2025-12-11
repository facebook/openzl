// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>

#include "tests/datagen/distributions/UniformDistribution.h"
#include "tests/datagen/random_producer/PRNGWrapper.h"

using namespace ::testing;

namespace openzl::tests::datagen {

namespace {

const double ksCriticalValue = 0.0466; // at 5% significance level
// one-sample Kolmogorov-Smirnov statistic for the uniform distribution on [min,
// max]
template <typename T>
double ksStat(std::vector<T>& data, T min, T max)
{
    static_assert(
            std::is_floating_point<T>::value, "KS only works for real RVs");
    EXPECT_GT(data.size(), 0);
    EXPECT_LT(min, max);
    std::sort(data.begin(), data.end());

    // KS assumes real-valued RVs, so we can't have duplicates
    for (size_t i = 1; i < data.size(); ++i) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
        // there's a bug in Clang where 'diagnostic ignored' doesn't work with
        // macros
        bool e = data[i] == data[i - 1];
        EXPECT_FALSE(e);
#pragma GCC diagnostic pop
    }
    double n = data.size();

    // we exploit the linearity of the uniform CDF to limit our suprenum search
    // to a set of interesting points where the EDF jumps
    using Point = std::pair<T, double>;
    std::vector<Point> interesting;
    for (size_t i = 0; i < data.size(); ++i) {
        interesting.push_back({ data[i], i / n });
        interesting.push_back({ data[i], (i + 1) / n });
    }

    double sup = 0.0;
    for (const auto& p : interesting) {
        double expected = ((double)p.first - min) / ((double)max - min);
        sup             = std::max(sup, std::abs(expected - p.second));
    }
    return sup;
}

const long nbCSBuckets = 50;
const double csCriticalValue =
        64; // at 5% significance level, from a X^2_47 distribution

// one-sample chi-square GOF statistic for the uniform distribution on [min,
// max]
template <typename T>
double csStat(std::vector<T>& data, T min, T max)
{
    static_assert(std::is_integral<T>::value, "Use KS for real RVs");
    EXPECT_GT(data.size(), 0);
    EXPECT_LT(min, max);
    std::sort(data.begin(), data.end());

    std::vector<size_t> buckets;

    for (size_t i = 1; i < nbCSBuckets; ++i) {
        double bucketMax = (double)(max - min) * i / nbCSBuckets + min;
        T bucketMaxRound = (T)bucketMax;
        auto it = std::upper_bound(data.begin(), data.end(), bucketMaxRound);
        buckets.push_back(it - data.begin());
    }
    buckets.push_back(data.size());
    for (size_t i = nbCSBuckets - 1; i > 0; --i) {
        buckets[i] -= buckets[i - 1];
    }
    auto check = std::accumulate(buckets.begin(), buckets.end(), 0ull);
    EXPECT_EQ(check, data.size());

    double statistic = 0.0;
    for (const auto o_i : buckets) {
        double e_i  = (double)data.size() / nbCSBuckets;
        double diff = o_i - e_i;
        statistic += diff * diff / e_i;
    }
    return statistic;
}

} // namespace

TEST(UniformDistributionTest, FloatKS)
{
    auto rw = std::make_shared<PRNGWrapper>(
            std::make_shared<std::mt19937>(0xdeadbeef));
    std::vector<float> data;
    UniformDistribution<float> dist(rw, 0, 100);
    for (size_t i = 0; i < 1000; ++i) {
        data.push_back(dist("gabagoo"));
    }
    EXPECT_LT(ksStat<float>(data, 0, 100), ksCriticalValue);
}

TEST(UniformDistributionTest, IntegralChiSquare)
{
    auto rw = std::make_shared<PRNGWrapper>(
            std::make_shared<std::mt19937>(0xeb5c0));
    std::vector<uint32_t> data;
    UniformDistribution<uint32_t> dist(rw, 0, 100);
    for (size_t i = 0; i < 1000; ++i) {
        data.push_back(dist("gabagoo"));
    }

    EXPECT_LT(csStat<uint32_t>(data, 0, 100), csCriticalValue);
}

} // namespace openzl::tests::datagen
