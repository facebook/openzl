// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <algorithm>
#include "tools/training/clustering/sample_limiter.h"

namespace openzl {
namespace training {
namespace tests {
TEST(SampleLimiter, PickSamplesRespectsLimits)
{
    SampleLimiter limiter(1000 /*maxTotalSize*/, 300 /*maxFileSize*/);
    std::vector<size_t> sampleSizes = { 200, 100, 50,  150, 160,
                                        250, 300, 350, 220, 450 };
    size_t maxFileSize              = 300;
    size_t minTotalSize             = 600;
    auto stopCondition = [minTotalSize](size_t subsetSampleSize, size_t) {
        return subsetSampleSize > minTotalSize;
    };
    auto checkResultMeetsLimits =
            [&sampleSizes, maxFileSize, minTotalSize](
                    const std::vector<size_t>& pickedIndices) {
                size_t subsetSampleSize = 0;
                for (size_t i : pickedIndices) {
                    subsetSampleSize += sampleSizes[i];
                    EXPECT_LE(sampleSizes[i], maxFileSize);
                }
                EXPECT_LE(subsetSampleSize, minTotalSize + maxFileSize);
                // This is only expected because we have enough samples that are
                // less than maxFileSize to meet the minTotalSize requirement.
                EXPECT_GE(subsetSampleSize, minTotalSize);
            };
    for (size_t i = 0; i < 20; ++i) {
        auto pickedIndices = limiter.pickSampleIndicesWithLimits(
                sampleSizes, stopCondition, maxFileSize);
        checkResultMeetsLimits(pickedIndices);
    }
}

TEST(SampleLimiter, InsufficientSamples)
{
    SampleLimiter limiter(1000 /*maxTotalSize*/, 300 /*maxFileSize*/);
    std::vector<size_t> sampleSizes = { 100, 400, 200, 500 };
    size_t maxFileSize              = 300;
    size_t minTotalSize             = 600;
    auto stopCondition = [minTotalSize](size_t subsetSampleSize, size_t) {
        return subsetSampleSize > minTotalSize;
    };
    // If we cannot meet minTotalSize limit, we should use all samples which do
    // not exceed file size limit deterministically
    for (size_t i = 0; i < 20; ++i) {
        auto pickedIndices = limiter.pickSampleIndicesWithLimits(
                sampleSizes, stopCondition, maxFileSize);
        std::sort(pickedIndices.begin(), pickedIndices.end());
        EXPECT_EQ(pickedIndices.size(), 2);
        EXPECT_EQ(pickedIndices[0], 0);
        EXPECT_EQ(pickedIndices[1], 2);
    }
}

TEST(SampleLimiter, PickSelectedNumberOfSamples)
{
    SampleLimiter limiter(1000 /*maxTotalSize*/, 300 /*maxFileSize*/);
    std::vector<size_t> sampleSizes = { 00,  100, 50,  150, 160,
                                        250, 300, 350, 220, 450 };
    size_t maxFileSize              = 300;
    size_t numToPick                = 4;
    auto stopCondition              = [numToPick](size_t, size_t numPicked) {
        return numPicked == numToPick;
    };

    // Since we specify that we want to pick numToPick, and there are >
    // numToPick samples with size < maxFileSize, we should get exactly
    // numToPick indices.
    for (size_t i = 0; i < 20; ++i) {
        auto pickedIndices = limiter.pickSampleIndicesWithLimits(
                sampleSizes, stopCondition, maxFileSize);
        std::sort(pickedIndices.begin(), pickedIndices.end());
        EXPECT_EQ(pickedIndices.size(), 4);
    }
}

} // namespace tests
} // namespace training
} // namespace openzl
