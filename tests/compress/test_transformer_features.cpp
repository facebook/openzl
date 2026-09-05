// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/compress/selectors/transformer/cardinality.h"
#include "openzl/compress/selectors/transformer/numeric_stats.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/xxhash.h"

namespace {

template <typename T, size_t Size>
const uint8_t* bytes(const std::array<T, Size>& values)
{
    return reinterpret_cast<const uint8_t*>(values.data());
}

uint64_t hashLittleEndianU64(uint64_t value)
{
    std::array<uint8_t, sizeof(value)> encoded = {};
    ZL_writeLE64(encoded.data(), value);
    return XXH3_64bits(encoded.data(), encoded.size());
}

double referenceSortedGapMode(std::vector<uint64_t> values)
{
    constexpr size_t maxValues = 512;
    constexpr size_t keepWidth = 384;
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    if (values.size() > maxValues) {
        values.resize(maxValues);
    }

    size_t const width = std::min(values.size(), keepWidth);
    if (width < 2) {
        return 0.0;
    }
    size_t const start = values.size() - width;
    size_t maxCount    = 0;
    for (size_t i = start + 1; i < values.size(); ++i) {
        uint64_t const gap = values[i] - values[i - 1];
        size_t count       = 0;
        for (size_t j = i; j < values.size(); ++j) {
            count += values[j] - values[j - 1] == gap;
        }
        maxCount = std::max(maxCount, count);
    }
    return static_cast<double>(maxCount) / static_cast<double>(width - 1);
}

double computeSortedGapMode(const std::vector<uint64_t>& values)
{
    constexpr uint64_t canary = UINT64_C(0xD15EA5E5CAFEBABE);
    std::array<uint64_t, TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES + 2> storage;
    storage.front()     = canary;
    storage.back()      = canary;
    double const result = TRS_numeric_compute_sorted_gap_mode(
            values.data(),
            values.size(),
            storage.data() + 1,
            TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES);
    EXPECT_EQ(storage.front(), canary);
    EXPECT_EQ(storage.back(), canary);
    return result;
}

TEST(TransformerFeaturesTest, CardinalityCountsSmallDistinctSet)
{
    const std::array<uint64_t, 8> values = { 1, 2, 3, 4, 1, 2, 3, 4 };
    EXPECT_EQ(TRS_estimate_cardinality_u64(values.data(), values.size()), 4);
}

TEST(TransformerFeaturesTest, CardinalityCountsOverlappingSequences)
{
    const std::array<uint32_t, 4> values = { 1, 2, 1, 2 };
    const std::array<uint8_t, 9> bytes   = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    EXPECT_EQ(
            TRS_estimate_pair_cardinality_u32(values.data(), values.size()), 2);
    EXPECT_EQ(TRS_estimate_d8_cardinality(bytes.data(), bytes.size(), 1), 1);
}

TEST(TransformerFeaturesTest, CardinalityCountsPresenceBitmaps)
{
    std::array<uint64_t, TRS_CARDINALITY_U8_BITMAP_WORDS> seenValues     = {};
    std::array<uint64_t, TRS_CARDINALITY_U8_PAIR_BITMAP_WORDS> seenPairs = {};
    seenValues[1 >> 6] |= uint64_t{ 1 } << (1 & 63);
    seenValues[2 >> 6] |= uint64_t{ 1 } << (2 & 63);
    seenValues[255 >> 6] |= uint64_t{ 1 } << (255 & 63);
    seenPairs[0x0102 >> 6] |= uint64_t{ 1 } << (0x0102 & 63);
    seenPairs[0xFFFF >> 6] |= uint64_t{ 1 } << (0xFFFF & 63);

    EXPECT_EQ(TRS_estimate_cardinality_u8_bitmap(seenValues.data(), 5), 3);
    EXPECT_EQ(TRS_estimate_cardinality_u16_bitmap(seenPairs.data(), 5), 2);
    EXPECT_EQ(TRS_estimate_pair_cardinality_u8_bitmap(seenPairs.data(), 5), 2);
}

TEST(TransformerFeaturesTest, NumericStatisticsRecognizeRegularSequences)
{
    const std::array<uint64_t, 4> values                   = { 1, 3, 5, 7 };
    std::array<TRS_NumericKmvEntry, TRS_NUMERIC_KMV_K> kmv = {};
    size_t kmvSize                                         = 0;
    for (uint64_t value : values)
        TRS_numeric_kmv_track_value(kmv.data(), &kmvSize, value);

    EXPECT_EQ(TRS_numeric_kmv_compute_gap_cv(kmv.data(), kmvSize), 0);
    EXPECT_DOUBLE_EQ(
            computeSortedGapMode(
                    std::vector<uint64_t>(values.begin(), values.end())),
            1.0);
    EXPECT_DOUBLE_EQ(TRS_numeric_compute_transition_gap_cv(4.0, 8.0, 2), 0.0);
}

TEST(TransformerFeaturesTest, NumericSortedGapModeCountsSeparatedMatches)
{
    const std::array<uint64_t, 5> values = { 6, 0, 4, 1, 3 };
    EXPECT_DOUBLE_EQ(
            computeSortedGapMode(
                    std::vector<uint64_t>(values.begin(), values.end())),
            0.5);
}

TEST(TransformerFeaturesTest, NumericSortedGapModeMatchesReference)
{
    constexpr std::array<size_t, 9> sizes = {
        0, 1, 2, 511, 512, 513, 1023, 1024, 4097,
    };

    for (size_t const size : sizes) {
        SCOPED_TRACE(size);
        std::vector<uint64_t> ascending(size);
        std::vector<uint64_t> repeated(size);
        std::vector<uint64_t> generated(size);
        uint64_t state = UINT64_C(0x9E3779B185EBCA87);
        for (size_t i = 0; i < size; ++i) {
            ascending[i] = i;
            repeated[i]  = (i * 17) % 73;
            state        = state * UINT64_C(6364136223846793005) + 1;
            generated[i] = state ^ ((uint64_t)i << (i % 31));
        }
        std::vector<uint64_t> descending = ascending;
        std::reverse(descending.begin(), descending.end());
        if (!generated.empty()) {
            generated.front() = 0;
            generated.back()  = UINT64_MAX;
        }

        for (const std::vector<uint64_t>* values :
             { &ascending, &descending, &repeated, &generated }) {
            EXPECT_DOUBLE_EQ(
                    computeSortedGapMode(*values),
                    referenceSortedGapMode(*values));
        }
    }
}

TEST(TransformerFeaturesTest, NumericKmvRetainsSmallestHashes)
{
    constexpr size_t inputSize = TRS_NUMERIC_KMV_K * 4;
    std::array<TRS_NumericKmvEntry, TRS_NUMERIC_KMV_K> kmv = {};
    std::array<uint64_t, inputSize> allHashes              = {};
    size_t kmvSize                                         = 0;

    for (size_t i = 0; i < inputSize; ++i) {
        uint64_t const value = (uint64_t)i;
        allHashes[i]         = hashLittleEndianU64(value);
        TRS_numeric_kmv_track_value(kmv.data(), &kmvSize, value);

        ASSERT_EQ(kmvSize, std::min(i + 1, (size_t)TRS_NUMERIC_KMV_K));
        for (size_t child = 1; child < kmvSize; ++child) {
            size_t const parent = (child - 1) / 2;
            ASSERT_GE(kmv[parent].hash, kmv[child].hash)
                    << "after inserting value " << value;
        }
    }

    std::array<uint64_t, TRS_NUMERIC_KMV_K> retainedHashes = {};
    for (size_t i = 0; i < kmvSize; ++i) {
        retainedHashes[i] = kmv[i].hash;
    }
    std::sort(allHashes.begin(), allHashes.end());
    std::sort(retainedHashes.begin(), retainedHashes.end());
    EXPECT_TRUE(
            std::equal(
                    retainedHashes.begin(),
                    retainedHashes.end(),
                    allHashes.begin()));
}

TEST(TransformerFeaturesTest, NumericStatisticsCountRepeatedFourByteWindows)
{
    const std::array<uint8_t, 8> data = {
        'a', 'b', 'c', 'd', 'a', 'b', 'c', 'd'
    };
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    EXPECT_EQ(
            TRS_numeric_compute_lz_matches_with_table(
                    data.data(),
                    data.size(),
                    1,
                    matchTable.data(),
                    matchTable.size()),
            1);
}

TEST(TransformerFeaturesTest, CanonicalizesBigEndianNumericBytes)
{
    const auto expectCanonical = [](const std::vector<uint8_t>& bigEndian,
                                    size_t eltWidth,
                                    const std::vector<uint8_t>& expected) {
        ASSERT_EQ(bigEndian.size(), expected.size());
        for (size_t i = 0; i < bigEndian.size(); ++i) {
            EXPECT_EQ(
                    TRS_numeric_canonical_byte_from_big_endian(
                            bigEndian.data(), i, eltWidth),
                    expected[i])
                    << "byte " << i << " at element width " << eltWidth;
        }
    };

    expectCanonical({ 0x12, 0x34 }, 1, { 0x12, 0x34 });
    expectCanonical({ 0x12, 0x34, 0xAB, 0xCD }, 2, { 0x34, 0x12, 0xCD, 0xAB });
    expectCanonical(
            { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF },
            4,
            { 0x67, 0x45, 0x23, 0x01, 0xEF, 0xCD, 0xAB, 0x89 });
    expectCanonical(
            { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF },
            8,
            { 0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01 });
}

TEST(TransformerFeaturesTest, NumericStatisticsEncodeDoubleFpBoundary)
{
    double const saturationInput = 0x1p32;
    double const below           = std::nextafter(saturationInput, 0.0);
    double const above           = std::nextafter(
            saturationInput, std::numeric_limits<double>::infinity());

    EXPECT_EQ(TRS_numeric_encode_double_fp(0.5), uint64_t{ 1 } << 31);
    EXPECT_EQ(
            TRS_numeric_encode_double_fp(below), UINT64_MAX - uint64_t{ 2047 });
    EXPECT_EQ(TRS_numeric_encode_double_fp(saturationInput), UINT64_MAX);
    EXPECT_EQ(TRS_numeric_encode_double_fp(above), UINT64_MAX);
}

TEST(TransformerFeaturesTest, CanonicalHashesMatchTrainingFeatureContract)
{
    /* Golden values for the canonical little-endian representation used by
     * the current Transformer training pipeline. */
    std::array<uint64_t, 512> values = {};
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = (uint64_t)i * UINT64_C(0x9E3779B185EBCA87)
                ^ ((uint64_t)i << (i % 29));
    }

    EXPECT_EQ(TRS_estimate_cardinality_u64(values.data(), values.size()), 508);
    EXPECT_EQ(
            TRS_estimate_pair_cardinality_u64(values.data(), values.size()),
            511);
    EXPECT_EQ(
            TRS_estimate_d8_cardinality(
                    bytes(values), sizeof(values), sizeof(values[0])),
            4089);
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    EXPECT_EQ(
            TRS_numeric_compute_lz_matches_with_table(
                    bytes(values),
                    sizeof(values),
                    sizeof(values[0]),
                    matchTable.data(),
                    matchTable.size()),
            5);

    std::array<TRS_NumericKmvEntry, TRS_NUMERIC_KMV_K> kmv = {};
    size_t kmvSize                                         = 0;
    TRS_numeric_kmv_track_value(
            kmv.data(), &kmvSize, UINT64_C(0x0102030405060708));
    ASSERT_EQ(kmvSize, 1);
    EXPECT_EQ(kmv[0].hash, UINT64_C(10416736986388286110));

    std::vector<uint64_t> hllValues(70000);
    for (size_t i = 0; i < hllValues.size(); ++i) {
        hllValues[i] = (uint64_t)(i % 10000) * UINT64_C(0xD6E8FEB86659FD93);
    }
    EXPECT_EQ(
            TRS_estimate_cardinality_u64(hllValues.data(), hllValues.size()),
            10000);
}
} // namespace
