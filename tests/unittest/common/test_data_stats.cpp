// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <cmath>
#include <random>
#include <string>
#include <string_view>

#include "openzl/codecs/bitpack/common_bitpack_kernel.h"
#include "openzl/codecs/flatpack/common_flatpack.h"
#include "openzl/codecs/flatpack/encode_flatpack_kernel.h"
#include "openzl/shared/data_stats.h"

#define HUF_STATIC_LINKING_ONLY
#include "openzl/fse/huf.h"

namespace openzl {
namespace tests {
namespace {

/***********************************************************
 * UTILITIES
 ***********************************************************/
const double kErrorRatioThreshold = 0.001; // 0.1%
void compareWithErrorRatio(
        double v1,
        double v2,
        double errorThreshold = kErrorRatioThreshold)
{
    ASSERT_LE(abs(1 - v1 / v2), errorThreshold);
}

void compareWithErrorRatio(
        size_t v1,
        size_t v2,
        double errorThreshold = kErrorRatioThreshold)
{
    return compareWithErrorRatio((double)v1, (double)v2, errorThreshold);
}

void compareWithError(double v1, double v2, double error = 0.001)
{
    ASSERT_LE(abs(v1 - v2), error);
}

void compareWithError(size_t v1, size_t v2, size_t error = 16)
{
    ASSERT_LE((size_t)abs((int64_t)v1 - (int64_t)v2), error);
}

const size_t kRandomSeed = 100;
std::vector<uint8_t> generateUniformVec(
        const size_t n,
        const uint8_t min = 0,
        const uint8_t max = 255)
{
    std::mt19937_64 mt(kRandomSeed);
    std::uniform_int_distribution<uint8_t> dist(min, max);

    std::vector<uint8_t> vec(n);
    for (size_t i = 0; i < n; i++) {
        vec[i] = dist(mt);
    }
    return vec;
}

std::vector<uint8_t> generateNormalVec(
        const size_t n,
        const double mean    = 128,
        const uint8_t stddev = 40)
{
    std::mt19937_64 mt(kRandomSeed);
    std::normal_distribution<double> dist(mean, stddev);

    std::vector<uint8_t> vec(n);
    for (size_t i = 0; i < n; i++) {
        double sample = -1;
        while (sample < 0 || sample > 255) {
            sample = dist(mt);
        }
        vec[i] = (uint8_t)std::round(sample);
    }
    return vec;
}

/***********************************************************
 * General tests
 ***********************************************************/

TEST(DataStats, basicInit)
{
    uint8_t const buffer[] = { 1, 2, 3 };
    DataStatsU8 stats;
    memset(&stats, 0xFF, sizeof(stats));
    DataStatsU8_init(&stats, buffer, 3);

    ASSERT_EQ(stats.src, buffer);
    ASSERT_EQ(DataStatsU8_totalElements(&stats), sizeof(buffer));
    ASSERT_FALSE(stats.histogramInitialized);
    ASSERT_FALSE(stats.deltaHistogramInitialized);
    ASSERT_FALSE(stats.entropyInitialized);
    ASSERT_FALSE(stats.deltaEntropyInitialized);
    ASSERT_FALSE(stats.cardinalityInitialized);
}

TEST(DataStats, cardinality)
{
    uint8_t const buffer[] = { 1, 2, 3, 4, 5, 2, 4 };
    DataStatsU8 stats;
    DataStatsU8_init(&stats, buffer, sizeof(buffer));
    ASSERT_EQ(DataStatsU8_getCardinality(&stats), (size_t)5);
}

TEST(DataStats, maxElt)
{
    uint8_t const buffer[] = { 1, 17, 114, 32, 164, 242 };
    DataStatsU8 stats;
    DataStatsU8_init(&stats, buffer, sizeof(buffer));
    ASSERT_EQ(DataStatsU8_getMaxElt(&stats), 242);
}

/***********************************************************
 * Histogram tests
 ***********************************************************/

void testHistogram(
        std::string_view buffer,
        void (*additionalCheck)(unsigned int const*))
{
    // init
    DataStatsU8 stats;
    DataStatsU8_init(&stats, buffer.data(), buffer.size());

    ASSERT_FALSE(stats.histogramInitialized); // Check that histogram is not
                                              // initialized before
    unsigned int const* hist = DataStatsU8_getHistogram(&stats);
    ASSERT_TRUE(stats.histogramInitialized); // Check that histogram is
                                             // iniatlized after

    // Check total count and cardinality is correct
    size_t totalElements = 0;
    unsigned cardinality = 0;
    for (size_t i = 0; i < 256; i++) {
        if (hist[i] > 0) {
            totalElements += hist[i];
            cardinality++;
        }
    }
    ASSERT_EQ(totalElements, buffer.size());
    ASSERT_EQ(cardinality, stats.cardinality);

    // Naively calculate another histogram and compare
    unsigned int hist2[256] = { 0 };
    for (char const s : buffer) {
        hist2[(uint8_t)s]++;
    }
    for (size_t i = 0; i < 256; i++) {
        ASSERT_EQ(hist[i], hist2[i]);
    }

    additionalCheck(hist);
}

TEST(DataStats, histogramBasic)
{
    testHistogram("1234", [](auto* hist) {
        ASSERT_EQ(hist[(size_t)'1'], (unsigned int)1);
        ASSERT_EQ(hist[(size_t)'2'], (unsigned int)1);
        ASSERT_EQ(hist[(size_t)'3'], (unsigned int)1);
        ASSERT_EQ(hist[(size_t)'4'], (unsigned int)1);
    });
}

TEST(DataStats, histogramBasic2)
{
    testHistogram(std::string(1 << 20, '1') + '2', [](auto* hist) {
        ASSERT_EQ(hist[(size_t)'1'], (unsigned int)1 << 20);
        ASSERT_EQ(hist[(size_t)'2'], (unsigned int)1);
    });
}

TEST(DataStats, histogramEmpty)
{
    testHistogram("", [](auto* hist) { (void)hist; });
}

TEST(DataStats, histogramUniformRandom)
{
    std::vector<uint8_t> vec = generateUniformVec(1 << 20);
    std::string_view sv(reinterpret_cast<char*>(vec.data()), vec.size());
    testHistogram(sv, [](auto* hist) { (void)hist; });
}

TEST(DataStats, histogramNormalRandom)
{
    std::vector<uint8_t> vec = generateNormalVec(1 << 20);
    std::string_view sv(reinterpret_cast<char*>(vec.data()), vec.size());
    testHistogram(sv, [](auto* hist) { (void)hist; });
}

/***********************************************************
 * Entropy estimation tests
 ***********************************************************/

double calculateEntropy(std::string_view buffer)
{
    double entropy             = 0;
    const double totalElements = (double)buffer.size();
    size_t hist[256]           = { 0 };
    for (const auto s : buffer) {
        hist[(uint8_t)s]++;
    }
    for (int i = 0; i < 256; i++) {
        if (hist[i] == 0)
            continue;
        entropy += -(double)hist[i] * log2((double)hist[i]);
    }
    return entropy / totalElements + log2(totalElements);
}

double calculateEstEntropy(std::string_view buffer)
{
    DataStatsU8 stats;
    DataStatsU8_init(&stats, buffer.data(), buffer.size());
    return DataStatsU8_getEntropy(&stats);
}

double calculateEstDeltaEntropy(std::string_view buffer)
{
    DataStatsU8 stats;
    DataStatsU8_init(&stats, buffer.data(), buffer.size());
    return DataStatsU8_getDeltaEntropy(&stats);
}

TEST(DataStats, entropyEstimationEmpty)
{
    ASSERT_DOUBLE_EQ(calculateEstEntropy(""), 0);
}

TEST(DataStats, deltaEntropyEstimationEmpty)
{
    ASSERT_DOUBLE_EQ(calculateEstDeltaEntropy(""), 0);
}

TEST(DataStats, entropyEstimationSingleByte)
{
    ASSERT_DOUBLE_EQ(calculateEstEntropy("1"), 0);
}

TEST(DataStats, entropyEstimationSingleValue)
{
    ASSERT_DOUBLE_EQ(calculateEstEntropy(std::string(1 << 20, '1')), 0);
}

TEST(DataStats, deltaEntropyEstimationSingleValue)
{
    ASSERT_LE(calculateEstDeltaEntropy(std::string(1 << 20, '1')), 0.01);
}

TEST(DataStats, entropyEstimationTwoValues)
{
    compareWithErrorRatio(
            calculateEstEntropy(
                    std::string(1 << 10, '1') + std::string(1 << 10, '2')),
            1);
}

TEST(DataStats, deltaEntropyEstimationTwoValues)
{
    compareWithError(
            calculateEstDeltaEntropy(
                    std::string(1 << 10, '1') + std::string(1 << 10, '2')),
            -(2046. * log2(2046) + 2. * log2(2) - 2048. * log2(2048)) / 2048.,
            0.01);
}

TEST(DataStats, entropyEstimationThreeValues)
{
    compareWithErrorRatio(
            calculateEstEntropy(
                    std::string(1 << 10, '1') + std::string(1 << 10, '2')
                    + std::string(1 << 10, '3')),
            log2(3));
}

TEST(DataStats, entropyEstimationTwoValuesSkewed)
{
    compareWithErrorRatio(
            calculateEstEntropy(
                    std::string(3 << 10, '1') + std::string(1 << 10, '2')),
            -(0.75 * log2(0.75) + 0.25 * log2(0.25)));
}

class EntropyEstimationUniformDataTests
        : public testing::TestWithParam<std::tuple<size_t, uint8_t, uint8_t>> {
};
TEST_P(EntropyEstimationUniformDataTests, testUnifom)
{
    auto [n, min, max]       = GetParam();
    std::vector<uint8_t> vec = generateUniformVec(n, min, max);
    std::string_view sv(reinterpret_cast<char*>(vec.data()), vec.size());
    const double expectedEntropy  = calculateEntropy(sv);
    const double estimatedEntropy = calculateEstEntropy(sv);
    compareWithErrorRatio(expectedEntropy, estimatedEntropy);
}
INSTANTIATE_TEST_SUITE_P(
        DataStats,
        EntropyEstimationUniformDataTests,
        testing::Values(
                std::make_tuple(100, 0, 255),
                std::make_tuple(1 << 20, 0, 100),
                std::make_tuple(1 << 20, 0, 200),
                std::make_tuple(1 << 20, 0, 255),
                std::make_tuple(1 << 20, 0, 10),
                std::make_tuple(1 << 10, 0, 1)));

class EntropyEstimationNormalDataTests
        : public testing::TestWithParam<std::tuple<size_t, double, uint8_t>> {};
TEST_P(EntropyEstimationNormalDataTests, testNormal)
{
    auto [n, mean, stddev]   = GetParam();
    std::vector<uint8_t> vec = generateNormalVec(n, mean, stddev);
    std::string_view sv(reinterpret_cast<char*>(vec.data()), vec.size());
    const double expectedEntropy  = calculateEntropy(sv);
    const double estimatedEntropy = calculateEstEntropy(sv);
    compareWithErrorRatio(expectedEntropy, estimatedEntropy);
}
INSTANTIATE_TEST_SUITE_P(
        DataStats,
        EntropyEstimationNormalDataTests,
        testing::Values(
                std::make_tuple(100, 128, 1),
                std::make_tuple(1 << 20, 128, 1),
                std::make_tuple(100, 128, 10),
                std::make_tuple(1 << 20, 128, 10),
                std::make_tuple(100, 128, 30),
                std::make_tuple(1 << 20, 128, 30),
                std::make_tuple(100, 128, 60),
                std::make_tuple(1 << 20, 128, 60)));

/***********************************************************
 * Bitpacked size estimation tests
 ***********************************************************/

size_t estimateBitpackedSize(std::vector<uint8_t> buffer)
{
    DataStatsU8 stats;
    DataStatsU8_init(&stats, buffer.data(), buffer.size());
    return DataStatsU8_getBitpackedSize(&stats);
}

size_t computeBitpackedSize(std::vector<uint8_t> buffer, int nbBits)
{
    std::vector<uint8_t> out(ZS_bitpackEncodeBound(buffer.size(), nbBits));
    return ZS_bitpackEncode8(
            out.data(), out.size(), buffer.data(), buffer.size(), nbBits);
}

TEST(DataStats, BitpackedSizeEmpty)
{
    std::vector<uint8_t> buffer = {};
    ASSERT_EQ(estimateBitpackedSize(buffer), computeBitpackedSize(buffer, 1));
}

TEST(DataStats, BitpackedSizeSingleByte)
{
    std::vector<uint8_t> buffer = { 18 };
    ASSERT_EQ(estimateBitpackedSize(buffer), computeBitpackedSize(buffer, 5));
}

TEST(DataStats, BitpackedSizeMultipleBytes)
{
    std::vector<uint8_t> buffer = {
        15, 18, 200, 211, 1, 107, 115, 123, 232, 250
    };
    ASSERT_EQ(estimateBitpackedSize(buffer), computeBitpackedSize(buffer, 8));
}

TEST(DataStats, BitpackedSizeMultipleBytesSmallMaxElt)
{
    std::vector<uint8_t> buffer = { 1, 2, 3, 4, 5, 2, 4 };
    ASSERT_EQ(estimateBitpackedSize(buffer), computeBitpackedSize(buffer, 3));
}

/***********************************************************
 * Flatpacked size estimation tests
 ***********************************************************/

size_t estimateFlatpackedSize(std::vector<uint8_t> buffer)
{
    DataStatsU8 stats;
    DataStatsU8_init(&stats, buffer.data(), buffer.size());
    return DataStatsU8_getFlatpackedSize(&stats);
}

size_t computeFlatpackedSize(
        std::vector<uint8_t> buffer,
        size_t alphabetCapacity)
{
    std::vector<uint8_t> alphabet(alphabetCapacity);
    std::vector<uint8_t> out(ZS_flatpackEncodeBound(buffer.size()));
    ZS_FlatPackSize size = ZS_flatpackEncode(
            alphabet.data(),
            alphabetCapacity,
            out.data(),
            out.size(),
            buffer.data(),
            buffer.size());

    return ZS_FlatPack_packedSize(size, buffer.size())
            + ZS_FlatPack_alphabetSize(size);
}

bool compareFlatpackedSizes(
        std::vector<uint8_t> buffer,
        size_t alphabetCapacity)
{
    size_t const actual = computeFlatpackedSize(buffer, alphabetCapacity);
    size_t const est    = estimateFlatpackedSize(buffer);

    return actual == est || (actual - 1 <= est && est <= actual);
}

TEST(DataStats, FlatpackedSizeEmpty)
{
    std::vector<uint8_t> buffer = {};
    ASSERT_TRUE(compareFlatpackedSizes(buffer, 0));
}

TEST(DataStats, FlatpackedSizeSingleByte)
{
    std::vector<uint8_t> buffer = { 18 };
    ASSERT_TRUE(compareFlatpackedSizes(buffer, 1));
}

TEST(DataStats, FlatpackedSizeMultipleBytes)
{
    std::vector<uint8_t> buffer = {
        15, 18, 200, 211, 1, 107, 115, 123, 232, 250
    };
    ASSERT_TRUE(compareFlatpackedSizes(buffer, 10));
}

TEST(DataStats, FlatpackedSizeDuplicateBytes)
{
    std::vector<uint8_t> buffer = { 15,  18,  200, 211, 15,
                                    107, 115, 123, 211, 250 };
    ASSERT_TRUE(compareFlatpackedSizes(buffer, 8));
}

/***********************************************************
 * Constant size estimation tests
 ***********************************************************/

size_t estimateConstantSize(std::vector<uint8_t> buffer)
{
    DataStatsU8 stats;
    DataStatsU8_init(&stats, buffer.data(), buffer.size());
    return DataStatsU8_getConstantSize(&stats);
}

TEST(DataStats, ConstantSizeSingleByte)
{
    std::vector<uint8_t> buffer = { 1 };
    ASSERT_EQ(estimateConstantSize(buffer), (size_t)2);
}

TEST(DataStats, ConstantSizeMultipleBytes)
{
    std::vector<uint8_t> buffer(127, 1);
    ASSERT_EQ(estimateConstantSize(buffer), (size_t)2);
}

TEST(DataStats, ConstantSizeMultipleBytes2)
{
    std::vector<uint8_t> buffer(128, 1);
    ASSERT_EQ(estimateConstantSize(buffer), (size_t)3);
}

/***********************************************************
 * Huffman size estimation tests
 ***********************************************************/

size_t calculateHuffamnSize(std::string_view buffer)
{
    const size_t sizeBounds = HUF_compressBound(buffer.size());
    std::vector<uint8_t> dst(sizeBounds);
    return HUF_compress(dst.data(), sizeBounds, buffer.data(), buffer.size());
}

size_t calculateEstHuffmanSize(std::string_view buffer)
{
    DataStatsU8 stats;
    DataStatsU8_init(&stats, buffer.data(), buffer.size());
    return DataStatsU8_getHuffmanSize(&stats);
}

size_t calculateEstDeltaHuffmanSize(std::string_view buffer)
{
    DataStatsU8 stats;
    DataStatsU8_init(&stats, buffer.data(), buffer.size());
    return DataStatsU8_getDeltaHuffmanSize(&stats);
}

TEST(DataStats, HuffmanSizeEmpty)
{
    ASSERT_EQ(calculateEstHuffmanSize(""), (size_t)4);
}

TEST(DataStats, deltaHuffmanSizeEmpty)
{
    ASSERT_EQ(calculateEstDeltaHuffmanSize(""), (size_t)4);
}

TEST(DataStats, HuffmanSizeSingleByte)
{
    ASSERT_EQ(calculateEstHuffmanSize("1"), (size_t)4);
}

TEST(DataStats, HuffmanSizeSingleValue)
{
    compareWithError(
            calculateEstHuffmanSize(std::string(1 << 15, '1')), (1 << 15) / 8);
}

TEST(DataStats, deltaHuffmanSizeSingleValue)
{
    compareWithError(
            calculateEstDeltaHuffmanSize(std::string(1 << 20, '1')),
            (1 << 20) / 8);
}

TEST(DataStats, HuffmanSizeTwoValues)
{
    compareWithError(
            calculateEstHuffmanSize(
                    std::string(1 << 10, '1') + std::string(1 << 10, '2')),
            2 * (1 << 10) / 8);
}

TEST(DataStats, deltaHuffmanSizeTwoValues)
{
    compareWithError(
            calculateEstDeltaHuffmanSize(
                    std::string(1 << 10, '1') + std::string(1 << 10, '2')),
            2 * (1 << 10) / 8);
}

TEST(DataStats, HuffmanSizeThreeValues)
{
    compareWithError(
            calculateEstHuffmanSize(
                    std::string(1 << 10, '1') + std::string(1 << 10, '2')
                    + std::string(1 << 10, '3')),
            (1 << 10) * (1 + 2 + 2) / 8);
}

TEST(DataStats, HuffmanSizeTwoValuesSkewed)
{
    compareWithError(
            calculateEstHuffmanSize(
                    std::string(3 << 10, '1') + std::string(1 << 10, '2')),
            4 * (1 << 10) / 8);
}

class HuffmanSizeUniformDataTests
        : public testing::TestWithParam<std::tuple<size_t, uint8_t, uint8_t>> {
};
TEST_P(HuffmanSizeUniformDataTests, testUnifom)
{
    auto [n, min, max]       = GetParam();
    std::vector<uint8_t> vec = generateUniformVec(n, min, max);
    std::string_view sv(reinterpret_cast<char*>(vec.data()), vec.size());
    const size_t expected  = calculateHuffamnSize(sv);
    const size_t estimated = calculateEstHuffmanSize(sv);
    ASSERT_FALSE(HUF_isError(expected));
    if (expected == 0) {
        ASSERT_GE(estimated, (sv.size() * 9) / 10);
    } else {
        compareWithErrorRatio(expected, estimated);
    }
}
INSTANTIATE_TEST_SUITE_P(
        DataStats,
        HuffmanSizeUniformDataTests,
        testing::Values(
                std::make_tuple(100, 0, 255),
                std::make_tuple(1 << 15, 0, 100),
                std::make_tuple(1 << 15, 0, 200),
                std::make_tuple(1 << 15, 0, 255),
                std::make_tuple(1 << 15, 0, 10),
                std::make_tuple(1 << 15, 0, 1)));

class HuffmanSizeNormalDataTests
        : public testing::TestWithParam<std::tuple<size_t, double, uint8_t>> {};
TEST_P(HuffmanSizeNormalDataTests, testNormal)
{
    auto [n, mean, stddev]   = GetParam();
    std::vector<uint8_t> vec = generateNormalVec(n, mean, stddev);
    std::string_view sv(reinterpret_cast<char*>(vec.data()), vec.size());
    const double expectedEntropy  = calculateEntropy(sv);
    const double estimatedEntropy = calculateEstEntropy(sv);
    compareWithErrorRatio(expectedEntropy, estimatedEntropy);
}
INSTANTIATE_TEST_SUITE_P(
        DataStats,
        HuffmanSizeNormalDataTests,
        testing::Values(
                std::make_tuple(100, 128, 1),
                std::make_tuple(1000, 128, 1),
                std::make_tuple(1 << 15, 128, 1),
                std::make_tuple(100, 128, 10),
                std::make_tuple(1000, 128, 10),
                std::make_tuple(1 << 15, 128, 10),
                std::make_tuple(100, 128, 30),
                std::make_tuple(1000, 128, 30),
                std::make_tuple(1 << 15, 128, 30),
                std::make_tuple(100, 128, 60),
                std::make_tuple(1000, 128, 60),
                std::make_tuple(1 << 15, 128, 60)));

} // namespace
} // namespace tests
} // namespace openzl
