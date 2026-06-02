// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "openzl/codecs/sparse_num/decode_sparse_num_kernel.h"
#include "openzl/codecs/sparse_num/encode_sparse_num_kernel.h"

namespace {

TEST(SparseNumKernelTest, DecodeAcceptsZeroLiteral)
{
    std::array<uint8_t, 2> const distances = { 3, 1 };
    std::array<uint8_t, 1> const values    = { 0 };
    std::array<uint8_t, 5> expected        = { 0, 0, 0, 0, 0 };
    std::array<uint8_t, 5> output          = {};

    size_t const outputCount = ZL_sparseNumDecodeOutputCount(
            distances.data(), distances.size(), sizeof(uint8_t), values.size());

    ASSERT_EQ(outputCount, output.size());
    ZL_sparseNumDecode(
            output.data(),
            output.size() * sizeof(uint8_t),
            distances.data(),
            distances.size(),
            sizeof(uint8_t),
            0,
            values.data(),
            values.size(),
            sizeof(uint8_t));

    EXPECT_EQ(output, expected);
}

TEST(SparseNumKernelTest, DecodeWithExplicitDominant)
{
    uint64_t const dominant                = 7;
    std::array<uint8_t, 3> const distances = { 2, 0, 1 };
    std::array<uint8_t, 2> const literals  = { 1, 2 };
    std::array<uint8_t, 5> const expected  = { 7, 7, 1, 2, 7 };
    std::array<uint8_t, 5> output          = {};

    size_t const outputCount = ZL_sparseNumDecodeOutputCount(
            distances.data(),
            distances.size(),
            sizeof(uint8_t),
            literals.size());

    ASSERT_EQ(outputCount, output.size());
    ZL_sparseNumDecode(
            output.data(),
            output.size() * sizeof(uint8_t),
            distances.data(),
            distances.size(),
            sizeof(uint8_t),
            dominant,
            literals.data(),
            literals.size(),
            sizeof(uint8_t));

    EXPECT_EQ(output, expected);
}

TEST(SparseNumKernelTest, DecodeWithWideExplicitDominant)
{
    uint64_t const dominant                = 1024;
    std::array<uint8_t, 2> const distances = { 1, 2 };
    std::array<uint16_t, 2> const values   = { 5, 6 };
    std::array<uint16_t, 5> const expected = { 1024, 5, 1024, 1024, 6 };
    std::array<uint16_t, 5> output         = {};

    size_t const outputCount = ZL_sparseNumDecodeOutputCount(
            distances.data(), distances.size(), sizeof(uint8_t), values.size());

    ASSERT_EQ(outputCount, output.size());
    ZL_sparseNumDecode(
            output.data(),
            output.size() * sizeof(uint16_t),
            distances.data(),
            distances.size(),
            sizeof(uint8_t),
            dominant,
            values.data(),
            values.size(),
            sizeof(uint16_t));

    EXPECT_EQ(output, expected);
}

TEST(SparseNumKernelTest, EncodeWithSmallDominant)
{
    std::array<uint8_t, 7> const input = { 7, 7, 1, 7, 2, 7, 7 };
    uint64_t const dominant            = ZL_sparseNumSelectDominant(
            input.data(), input.size(), sizeof(uint8_t));

    ASSERT_EQ(dominant, 7);

    ZL_SparseNumEncodeInfo const info = ZL_sparseNumComputeEncodeInfo(
            input.data(), input.size(), sizeof(uint8_t), dominant);
    ASSERT_EQ(info.numDistances, 3);
    ASSERT_EQ(info.numValues, 2);
    ASSERT_EQ(info.distanceWidth, sizeof(uint8_t));

    std::array<uint8_t, 3> distances = {};
    std::array<uint8_t, 2> values    = {};
    ZL_sparseNumEncode(
            distances.data(),
            values.data(),
            input.data(),
            input.size(),
            sizeof(uint8_t),
            info.distanceWidth,
            dominant);

    EXPECT_EQ(distances, (std::array<uint8_t, 3>{ 2, 1, 2 }));
    EXPECT_EQ(values, (std::array<uint8_t, 2>{ 1, 2 }));
}

TEST(SparseNumKernelTest, EncodeWithWideDominant)
{
    std::array<uint16_t, 5> const input = { 1024, 5, 1024, 1024, 6 };
    uint64_t const dominant             = 1024;

    ZL_SparseNumEncodeInfo const info = ZL_sparseNumComputeEncodeInfo(
            input.data(), input.size(), sizeof(uint16_t), dominant);
    ASSERT_EQ(info.numDistances, 2);
    ASSERT_EQ(info.numValues, 2);
    ASSERT_EQ(info.distanceWidth, sizeof(uint8_t));

    std::array<uint8_t, 2> distances = {};
    std::array<uint16_t, 2> values   = {};
    ZL_sparseNumEncode(
            distances.data(),
            values.data(),
            input.data(),
            input.size(),
            sizeof(uint16_t),
            info.distanceWidth,
            dominant);

    EXPECT_EQ(distances, (std::array<uint8_t, 2>{ 1, 2 }));
    EXPECT_EQ(values, (std::array<uint16_t, 2>{ 5, 6 }));
}

} // namespace
