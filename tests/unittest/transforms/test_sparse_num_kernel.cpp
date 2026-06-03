// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "openzl/codecs/sparse_num/decode_sparse_num_kernel.h"

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
            values.data(),
            values.size(),
            sizeof(uint8_t));

    EXPECT_EQ(output, expected);
}

} // namespace
