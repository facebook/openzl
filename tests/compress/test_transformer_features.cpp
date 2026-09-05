// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "openzl/compress/selectors/transformer/cardinality.h"

namespace {

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

} // namespace
