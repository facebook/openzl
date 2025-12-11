// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/stream.h"
#include "openzl/compress/selectors/ml/features.h"
#include "tests/zstrong/test_zstrong_fixture.h"

using namespace ::testing;

namespace {

const double kEpsilon               = 1e-6;
const size_t kDefaultVectorCapacity = 2048;

/**
 * Generate a stream using streamData and verify that the resulting features
 * from the stream match the expected values in featureMap.
 */

void verifyIntegerFeatures(
        const ZL_Input* stream,
        const std::map<std::string, double>& featureMap)
{
    VECTOR(LabeledFeature) features = VECTOR_EMPTY(kDefaultVectorCapacity);
    const ZL_Report report = FeatureGen_integer(stream, &features, nullptr);
    ASSERT_FALSE(ZL_errorCode(report));

    for (size_t i = 0; i < VECTOR_SIZE(features); ++i) {
        std::string currentLabel = VECTOR_AT(features, i).label;
        if (featureMap.find(currentLabel) == featureMap.end()) {
            continue;
        }

        EXPECT_NEAR(
                VECTOR_AT(features, i).value,
                featureMap.at(currentLabel),
                abs(featureMap.at(currentLabel)) * kEpsilon);
    }

    VECTOR_DESTROY(features);
}

template <typename T>
void generateStreamAndVerifyIntegerFeatures(
        const std::vector<T>& streamData,
        const std::map<std::string, double>& featureMap)
{
    ASSERT_TRUE(std::is_arithmetic<T>::value);
    openzl::tests::WrappedStream<T> stream(streamData, ZL_Type_numeric);

    verifyIntegerFeatures(stream.getStream(), featureMap);
}
} // namespace

TEST(FeaturesTest, IntFeatureGeneratorTest)
{
    const std::vector<int> streamData = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    const std::map<std::string, double> featureMap = {
        { "nbElts", streamData.size() },
        { "eltWidth", sizeof(int) },
        { "cardinality", 10 },
        { "cardinality_upper", streamData.size() },
        { "cardinality_lower", 9 },
        { "range_size", 9 },
        { "mean", 4.5 },
        { "variance", 9.1666667 }, // Sample Variance
        { "stddev", 3.02765 },
        { "kurtosis", -1.224242 },
        { "skewness", 0 },

    };
    generateStreamAndVerifyIntegerFeatures(streamData, featureMap);
}

TEST(FeaturesTest, SkewedIntFeatureGeneratorTest)
{
    const std::vector<int> streamData = { 0, 1, 2, 3, 3, 4, 4, 5, 5, 5,
                                          6, 6, 6, 6, 6, 7, 7, 8, 9 };
    const std::map<std::string, double> featureMap = {
        { "nbElts", streamData.size() },
        { "eltWidth", sizeof(int) },
        { "cardinality", 10 },
        { "cardinality_upper", 13 },
        { "cardinality_lower", 9 },
        { "range_size", 9 },
        { "mean", 4.894736 },
        { "variance", 5.432748 },
        { "stddev", 2.330825 },
        { "skewness", -0.410622 },
        { "kurtosis", -0.36711987 },

    };
    generateStreamAndVerifyIntegerFeatures(streamData, featureMap);
}

TEST(FeaturesTest, EmptyIntFeatureGeneratorTest)
{
    const std::vector<int> streamData              = {};
    const std::map<std::string, double> featureMap = {
        { "nbElts", streamData.size() },
        { "eltWidth", sizeof(int) },
        { "cardinality", 0 },
        { "cardinality_upper", 0 },
        { "cardinality_lower", 0 },
        { "range_size", 0 },
        { "mean", 0 },
        { "variance", 0 },
        { "stddev", 0 },
        { "skewness", 0 },
        { "kurtosis", 0 },
    };
    generateStreamAndVerifyIntegerFeatures(streamData, featureMap);
}

TEST(FeaturesTest, SingleIntFeatureGeneratorTest)
{
    const std::vector<int> streamData              = { 5 };
    const std::map<std::string, double> featureMap = {
        { "nbElts", streamData.size() },
        { "eltWidth", sizeof(int) },
        { "cardinality", 1 },
        { "cardinality_upper", streamData.size() },
        { "cardinality_lower", 1 },
        { "range_size", 0 },
        { "mean", 5 },
        { "variance", 0 },
        { "stddev", 0 },
        { "skewness", 0 },
        { "kurtosis", 0 },
    };
    generateStreamAndVerifyIntegerFeatures(streamData, featureMap);
}

TEST(FeaturesTest, MomentsTest)
{
    std::vector<uint64_t> streamData               = { 1, 1, 1, 1, 0, 1, 2, 3 };
    const std::map<std::string, double> featureMap = {
        { "mean", 1.25 },
        { "variance", 0.7857142857142857 },
        { "stddev", 0.8864052604279183 },
        { "skewness", 0.8223036670302644 },
        { "kurtosis", 0.2148760330578514 },
    };

    generateStreamAndVerifyIntegerFeatures(streamData, featureMap);
}

TEST(FeaturesTest, MomentsTestStableLarge)
{
    std::vector<uint64_t> streamData(1 << 24, (uint64_t)-1);
    streamData.push_back(0);
    streamData.push_back(1);
    streamData.push_back(2);
    streamData.push_back(3);

    const std::map<std::string, double> featureMap = {
        { "mean", 1.844673967566409e+19 },
        { "variance", 8.11296045646944e+31 },
        { "stddev", 9007197375693196.0 },
        { "skewness", -2047.99951171875 },
        { "kurtosis", 4194300.0000002384 },
    };

    generateStreamAndVerifyIntegerFeatures(streamData, featureMap);
}

TEST(FeaturesTest, TestMomentsStableSmall)
{
    std::vector<uint64_t> streamData(1 << 24, 1);
    streamData.push_back(0);
    streamData.push_back(1);
    streamData.push_back(2);
    streamData.push_back(3);

    const std::map<std::string, double> featureMap = {
        { "mean", 1.0000001192092611 },
        { "variance", 3.576277904926602e-07 },
        { "stddev", 0.0005980198913854456 },
        { "skewness", 2229.5797976466847 },
        { "kurtosis", 8388605.888889026 },
    };

    generateStreamAndVerifyIntegerFeatures(streamData, featureMap);
}

TEST(FeaturesTest, TestMomentsUint8)
{
    std::vector<uint64_t> streamData;
    for (int i = 0; i < 256; ++i) {
        streamData.insert(streamData.end(), i, i);
    }

    const std::map<std::string, double> featureMap = {
        { "mean", 170.33333333333334 },
        { "variance", 3626.666666666667 },
        { "stddev", 60.221812216726484 },
        { "skewness", -0.5656951738787298 },
        { "kurtosis", -0.6000551487484294 },
    };

    generateStreamAndVerifyIntegerFeatures(streamData, featureMap);
}
