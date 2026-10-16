// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdint.h>
#include <array>

#include <gtest/gtest.h>

#include "openzl/shared/simd_wrapper.h"

using namespace ::testing;

namespace openzl {
namespace tests {

class SimdWrapperTest : public Test {
   public:
    void check(ZL_Vec128 v128, ZL_Vec128Fallback f128) const
    {
        std::array<uint8_t, 16> vecData;
        ZL_Vec128_write(&vecData[0], v128);
        std::array<uint8_t, 16> vecFallbackData;
        ZL_Vec128Fallback_write(&vecFallbackData[0], f128);
        for (size_t i = 0; i < 16; ++i) {
            EXPECT_EQ(vecData[i], vecFallbackData[i]);
        }
    }

    void check(ZL_Vec256 v256, ZL_Vec256Fallback f256) const
    {
        std::array<uint8_t, 32> vecData;
        ZL_Vec256_write(&vecData[0], v256);
        std::array<uint8_t, 32> vecFallbackData;
        ZL_Vec256Fallback_write(&vecFallbackData[0], f256);
        for (size_t i = 0; i < 32; ++i) {
            EXPECT_EQ(vecData[i], vecFallbackData[i]);
        }
    }
};

TEST_F(SimdWrapperTest, ReadAndWrite_128)
{
    std::array<uint8_t, 16> data = { 0, 1, 2,  3,  4,  5,  6,  7,
                                     8, 9, 10, 11, 12, 13, 14, 15 };
    auto v128                    = ZL_Vec128_read(&data[0]);
    auto f128                    = ZL_Vec128Fallback_read(&data[0]);
    check(v128, f128);
    std::array<uint8_t, 16> out;
    ZL_Vec128_write(&out[0], v128);
    for (size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(out[i], data[i]);
    }
    ZL_Vec128Fallback_write(&out[0], f128);
    for (size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(out[i], data[i]);
    }
}

TEST_F(SimdWrapperTest, Set8_128)
{
    auto v128 = ZL_Vec128_set8(0x7);
    auto f128 = ZL_Vec128Fallback_set8(0x7);
    check(v128, f128);
    EXPECT_EQ(f128.data[0], 0x7);
    EXPECT_EQ(f128.data[15], 0x7);
}

TEST_F(SimdWrapperTest, Cmp8_128)
{
    std::array<uint8_t, 16> data = { 0, 1, 2,  3,  4,  5,  6,  7,
                                     8, 9, 10, 11, 12, 13, 14, 15 };
    auto v1                      = ZL_Vec128_read(&data[0]);
    auto f1                      = ZL_Vec128Fallback_read(&data[0]);
    auto v2                      = ZL_Vec128_set8(0x7);
    auto f2                      = ZL_Vec128Fallback_set8(0x7);

    auto v128 = ZL_Vec128_cmp8(v1, v2);
    auto f128 = ZL_Vec128Fallback_cmp8(f1, f2);
    check(v128, f128);
    EXPECT_EQ(f128.data[0], 0);
    EXPECT_EQ(f128.data[7], 0xff);
}

TEST_F(SimdWrapperTest, And_128)
{
    auto v1 = ZL_Vec128_set8(0x7);
    auto f1 = ZL_Vec128Fallback_set8(0x7);
    auto v2 = ZL_Vec128_set8(0x12);
    auto f2 = ZL_Vec128Fallback_set8(0x12);

    auto v128 = ZL_Vec128_and(v1, v2);
    auto f128 = ZL_Vec128Fallback_and(f1, f2);
    check(v128, f128);
    EXPECT_EQ(f128.data[0], 0x2);
}

TEST_F(SimdWrapperTest, Mask8_128)
{
    std::array<uint8_t, 16> data;

    // Check every output bit individually.
    for (size_t bit = 0; bit < 16; ++bit) {
        data.fill(0);
        data[bit] = 0x80;

        auto v1    = ZL_Vec128_read(&data[0]);
        auto f1    = ZL_Vec128Fallback_read(&data[0]);
        auto vMask = ZL_Vec128_mask8(v1);
        auto fMask = ZL_Vec128Fallback_mask8(f1);

        EXPECT_EQ(vMask, fMask);
        EXPECT_EQ(vMask, 1u << bit);
    }

    // Check random byte patterns with a simple deterministic PRNG.
    uint32_t seed = 0xDEADBEEF;
    for (size_t n = 0; n < 128; ++n) {
        ZL_VecMask refMask = 0;

        for (size_t i = 0; i < 16; ++i) {
            seed    = seed * 1664525 + 1013904223;
            data[i] = seed >> 24;
            refMask |= (ZL_VecMask)(data[i] >> 7) << i;
        }

        auto v1    = ZL_Vec128_read(&data[0]);
        auto f1    = ZL_Vec128Fallback_read(&data[0]);
        auto vMask = ZL_Vec128_mask8(v1);
        auto fMask = ZL_Vec128Fallback_mask8(f1);

        EXPECT_EQ(vMask, fMask);
        EXPECT_EQ(vMask, refMask);
    }
}

TEST_F(SimdWrapperTest, ReadAndWrite_256)
{
    std::array<uint8_t, 32> data = { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                                     11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                     22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
    auto v256                    = ZL_Vec256_read(&data[0]);
    auto f256                    = ZL_Vec256Fallback_read(&data[0]);
    check(v256, f256);
    std::array<uint8_t, 32> out;
    ZL_Vec256_write(&out[0], v256);
    for (size_t i = 0; i < 32; ++i) {
        EXPECT_EQ(out[i], data[i]);
    }
    ZL_Vec256Fallback_write(&out[0], f256);
    for (size_t i = 0; i < 32; ++i) {
        EXPECT_EQ(out[i], data[i]);
    }
}

TEST_F(SimdWrapperTest, Set8_256)
{
    auto v256 = ZL_Vec256_set8(0x7);
    auto f256 = ZL_Vec256Fallback_set8(0x7);
    check(v256, f256);
}

TEST_F(SimdWrapperTest, Cmp8_256)
{
    std::array<uint8_t, 32> data = { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                                     11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                     22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
    auto v1                      = ZL_Vec256_read(&data[0]);
    auto f1                      = ZL_Vec256Fallback_read(&data[0]);
    auto v2                      = ZL_Vec256_set8(20);
    auto f2                      = ZL_Vec256Fallback_set8(20);

    auto v256 = ZL_Vec256_cmp8(v1, v2);
    auto f256 = ZL_Vec256Fallback_cmp8(f1, f2);
    check(v256, f256);
}

TEST_F(SimdWrapperTest, And_256)
{
    auto v1 = ZL_Vec256_set8(0x7);
    auto f1 = ZL_Vec256Fallback_set8(0x7);
    auto v2 = ZL_Vec256_set8(0x12);
    auto f2 = ZL_Vec256Fallback_set8(0x12);

    auto v256 = ZL_Vec256_and(v1, v2);
    auto f256 = ZL_Vec256Fallback_and(f1, f2);
    check(v256, f256);
}

TEST_F(SimdWrapperTest, Mask8_256)
{
    std::array<uint8_t, 32> data;

    // Check every output bit individually.
    for (size_t bit = 0; bit < 32; ++bit) {
        data.fill(0);
        data[bit] = 0x80;

        auto v1    = ZL_Vec256_read(&data[0]);
        auto f1    = ZL_Vec256Fallback_read(&data[0]);
        auto vMask = ZL_Vec256_mask8(v1);
        auto fMask = ZL_Vec256Fallback_mask8(f1);

        EXPECT_EQ(vMask, fMask);
        EXPECT_EQ(vMask, 1u << bit);
    }

    // Check random byte patterns with a simple deterministic PRNG.
    uint32_t seed = 0xDEADBEEF;
    for (size_t n = 0; n < 128; ++n) {
        ZL_VecMask refMask = 0;

        for (size_t i = 0; i < 32; ++i) {
            seed    = seed * 1664525 + 1013904223;
            data[i] = seed >> 24;
            refMask |= (ZL_VecMask)(data[i] >> 7) << i;
        }

        auto v1    = ZL_Vec256_read(&data[0]);
        auto f1    = ZL_Vec256Fallback_read(&data[0]);
        auto vMask = ZL_Vec256_mask8(v1);
        auto fMask = ZL_Vec256Fallback_mask8(f1);

        EXPECT_EQ(vMask, fMask);
        EXPECT_EQ(vMask, refMask);
    }
}

TEST_F(SimdWrapperTest, VecMask_next)
{
    ZL_VecMask mask = 0x01010111;
    EXPECT_EQ(0, ZL_VecMask_next(mask));
    mask &= mask - 1;
    EXPECT_EQ(4, ZL_VecMask_next(mask));
    mask &= mask - 1;
    EXPECT_EQ(8, ZL_VecMask_next(mask));
    mask &= mask - 1;
    EXPECT_EQ(16, ZL_VecMask_next(mask));
    mask &= mask - 1;
    EXPECT_EQ(24, ZL_VecMask_next(mask));
    mask &= mask - 1;
    EXPECT_EQ(0, mask);
}

TEST_F(SimdWrapperTest, VecMask_rotateRight)
{
    EXPECT_EQ(0x11000011, ZL_VecMask_rotateRight(0x10000111, 4, 32));
    EXPECT_EQ(0x01111000, ZL_VecMask_rotateRight(0x10000111, 16, 32));
    EXPECT_EQ(0x1011, ZL_VecMask_rotateRight(0x0111, 4, 16));
    EXPECT_EQ(0x1110, ZL_VecMask_rotateRight(0x0111, 12, 16));
}

} // namespace tests
} // namespace openzl
