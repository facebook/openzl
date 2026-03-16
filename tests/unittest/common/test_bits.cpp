// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <random>

#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"

namespace {

TEST(BitsTest, bits)
{
    ASSERT_NE(ZL_32bits(), ZL_64bits());
}

TEST(BitsTest, isLittleEndian)
{
    uint32_t const x = 1;
    char const* c    = reinterpret_cast<char const*>(&x);
    if (ZL_isLittleEndian()) {
        ASSERT_EQ(c[0], 1);
        ASSERT_EQ(ZL_Endianness_host(), ZL_Endianness_little);
    } else {
        ASSERT_EQ(c[3], 1);
        ASSERT_EQ(ZL_Endianness_host(), ZL_Endianness_big);
    }
}

TEST(BitsTest, popcount64)
{
    for (uint64_t x = 0; x < 100000; ++x) {
        ASSERT_EQ(ZL_popcount64(x), ZL_popcount64_fallback(x));
    }
}

TEST(BitsTest, clz64)
{
    for (uint64_t x = 1; x < 100000; ++x) {
        ASSERT_EQ(ZL_clz64(x), ZL_clz64_fallback(x));
        uint64_t const y = uint64_t(-1) - x;
        ASSERT_EQ(ZL_clz64(y), ZL_clz64_fallback(y));
    }
}

TEST(BitsTest, nextPow2)
{
    for (uint64_t x = 1; x < 100000; ++x) {
        ASSERT_EQ(ZL_nextPow2(x), ZL_nextPow2_fallback(x));
    }
}

TEST(BitsTest, clz32)
{
    for (uint32_t x = 1; x < 100000; ++x) {
        ASSERT_EQ(ZL_clz32(x), ZL_clz32_fallback(x));
    }
}

TEST(BitsTest, ctz32)
{
    for (uint32_t x = 1; x < 100000; ++x) {
        ASSERT_EQ(ZL_ctz32(x), ZL_ctz32_fallback(x));
    }
}
TEST(BitsTest, ctz64)
{
    for (uint64_t x = 1; x < 100000; ++x) {
        ASSERT_EQ(ZL_ctz64(x), ZL_ctz64_fallback(x));
        uint64_t const y = uint64_t(-1) - x;
        ASSERT_EQ(ZL_ctz64(y), ZL_ctz64_fallback(y));
    }
}

TEST(BitsTest, highbit32)
{
    ASSERT_EQ(ZL_highbit32(1), 0);
    ASSERT_EQ(ZL_highbit32(2), 1);
    ASSERT_EQ(ZL_highbit32(3), 1);
    ASSERT_EQ(ZL_highbit32(4), 2);
    ASSERT_EQ(ZL_highbit32((uint32_t)-1), 31);
}

TEST(BitsTest, swap)
{
    uint32_t const x16 = 0x0011;
    uint32_t const x32 = 0x00112233;
    uint64_t const x64 = 0x0011223344556677ull;
    ASSERT_EQ(ZL_swap16(x16), 0x1100u);
    ASSERT_EQ(ZL_swap32(x32), 0x33221100u);
    ASSERT_EQ(ZL_swap64(x64), 0x7766554433221100ull);
    if (ZL_32bits()) {
        ASSERT_EQ(ZL_swap32(x32), ZL_swapST(x32));
    } else {
        ASSERT_EQ(ZL_swap64(x64), ZL_swapST(x64));
    }
}

#if !ZL_HAS_IEEE_754
#    error "IEEE 754 not supported"
#endif

TEST(BitsTest, convertIntToDouble)
{
    auto testInt = [](int64_t x) {
        bool const canConvert = ZL_canConvertIntToDouble(x);
        ZL_IEEEDouble const convertedUnchecked =
                ZL_convertIntToDoubleUnchecked(x);
        ZL_IEEEDouble convertedChecked;
        bool const conversionSucceeded =
                ZL_convertIntToDouble(&convertedChecked, x);
        double const converted  = (double)x;
        uint64_t const expected = ZL_read64(&converted);
        int64_t const roundTripped =
                ZL_convertDoubleToIntUnchecked(convertedChecked);
        if (canConvert) {
            EXPECT_TRUE(conversionSucceeded);
            EXPECT_EQ(expected, convertedUnchecked.value);
            EXPECT_EQ(expected, convertedChecked.value);
            EXPECT_EQ(x, roundTripped);
        }
        return canConvert;
    };
    int64_t constexpr kLastSuccess = int64_t(1) << 53;
    ASSERT_TRUE(testInt(kLastSuccess));
    ASSERT_TRUE(testInt(-kLastSuccess));
    ASSERT_FALSE(testInt(kLastSuccess + 1));
    ASSERT_FALSE(testInt(-kLastSuccess - 1));
    ASSERT_FALSE(testInt(std::numeric_limits<int64_t>::min()));
    ASSERT_FALSE(testInt(std::numeric_limits<int64_t>::min() + 1));
    ASSERT_FALSE(testInt(std::numeric_limits<int64_t>::max()));
    ASSERT_FALSE(testInt(std::numeric_limits<int64_t>::max() - 1));

    for (int64_t x = -100; x < 100; ++x) {
        ASSERT_TRUE(testInt(x));
    }
    for (int64_t x = 1; x <= kLastSuccess; x *= 2) {
        ASSERT_TRUE(testInt(x));
        ASSERT_TRUE(testInt(x - 1));
        ASSERT_TRUE(testInt(-x));
        ASSERT_TRUE(testInt(-x + 1));
    }
    for (int64_t x = kLastSuccess + 1; x < kLastSuccess + 1000; ++x) {
        ASSERT_FALSE(testInt(x));
        ASSERT_FALSE(testInt(-x));
    }
}

TEST(BitsTest, convertDoubleToInt)
{
    auto testDouble = [](double x) {
        ZL_IEEEDouble const dbl{ ZL_read64(&x) };
        int64_t const convertedUnchecked = ZL_convertDoubleToIntUnchecked(dbl);
        int64_t convertedChecked         = 0xfaceb00c;
        bool const conversionSucceeded =
                ZL_convertDoubleToInt(&convertedChecked, dbl);
        if (std::abs(x) <= int64_t(1) << 62) {
            int64_t const converted = (int64_t)x;
            if (conversionSucceeded) {
                EXPECT_EQ(converted, convertedUnchecked);
                EXPECT_EQ(converted, convertedChecked);
                double const roundTripped = (double)converted;
                EXPECT_EQ(ZL_read64(&x), ZL_read64(&roundTripped));
                EXPECT_TRUE(ZL_canConvertIntToDouble(converted));
                EXPECT_EQ(
                        dbl.value,
                        ZL_convertIntToDoubleUnchecked(converted).value);
            }
        } else {
            EXPECT_FALSE(conversionSucceeded);
        }
        if (!conversionSucceeded) {
            EXPECT_EQ(convertedChecked, 0xfaceb00c);
        }
        return std::pair<bool, int64_t>{ conversionSucceeded,
                                         convertedChecked };
    };
    for (int64_t x = -100; x < 100; ++x) {
        ASSERT_EQ(testDouble((double)x), std::pair(true, x));
    }
    int64_t constexpr kLastSuccess = int64_t(1) << 53;
    ASSERT_EQ(testDouble((double)kLastSuccess), std::pair(true, kLastSuccess));
    ASSERT_EQ(
            testDouble((double)-kLastSuccess), std::pair(true, -kLastSuccess));
    ASSERT_EQ(
            testDouble((double)(kLastSuccess + 1)),
            std::pair(true, kLastSuccess));
    ASSERT_EQ(
            testDouble((double)(-kLastSuccess - 1)),
            std::pair(true, -kLastSuccess));
    ASSERT_FALSE(testDouble((double)(kLastSuccess + 2)).first);
    ASSERT_FALSE(testDouble((double)(-kLastSuccess - 2)).first);
    ASSERT_EQ(
            testDouble((double)std::numeric_limits<int64_t>::min()).first,
            false);
    ASSERT_EQ(
            testDouble((double)std::numeric_limits<int64_t>::max()).first,
            false);
    ASSERT_EQ(
            testDouble((double)std::numeric_limits<uint64_t>::max() - 1).first,
            false);
    ASSERT_EQ(
            testDouble((double)std::numeric_limits<uint64_t>::max()).first,
            false);
    ASSERT_EQ(
            testDouble(-(double)std::numeric_limits<uint64_t>::max()).first,
            false);
    ASSERT_EQ(testDouble(std::numeric_limits<double>::min()).first, false);
    ASSERT_EQ(-testDouble(std::numeric_limits<double>::min()).first, false);
    ASSERT_EQ(testDouble(std::numeric_limits<double>::max()).first, false);
    ASSERT_EQ(-testDouble(std::numeric_limits<double>::max()).first, false);
    ASSERT_EQ(testDouble(std::numeric_limits<double>::lowest()).first, false);
    ASSERT_EQ(-testDouble(std::numeric_limits<double>::lowest()).first, false);
    ASSERT_EQ(testDouble(std::numeric_limits<double>::epsilon()).first, false);
    ASSERT_EQ(-testDouble(std::numeric_limits<double>::epsilon()).first, false);
    ASSERT_EQ(testDouble(std::numeric_limits<double>::infinity()).first, false);
    ASSERT_EQ(
            -testDouble(std::numeric_limits<double>::infinity()).first, false);
    ASSERT_EQ(
            testDouble(std::numeric_limits<double>::quiet_NaN()).first, false);
    ASSERT_EQ(
            -testDouble(std::numeric_limits<double>::quiet_NaN()).first, false);
    ASSERT_EQ(
            testDouble(std::numeric_limits<double>::signaling_NaN()).first,
            false);
    ASSERT_EQ(
            -testDouble(std::numeric_limits<double>::signaling_NaN()).first,
            false);
    ASSERT_EQ(
            testDouble(std::numeric_limits<double>::denorm_min()).first, false);
    ASSERT_EQ(
            -testDouble(std::numeric_limits<double>::denorm_min()).first,
            false);
    ASSERT_EQ(testDouble(0.5).first, false);
    ASSERT_EQ(testDouble(0.99999).first, false);
    ASSERT_EQ(testDouble(-0.5).first, false);
    ASSERT_EQ(testDouble(-0.99999).first, false);
}
TEST(BitsTest, bitDeposit32)
{
    // Zero mask -> zero result
    ASSERT_EQ(ZL_bitDeposit32(0xFFFFFFFF, 0), 0u);
    // All-ones mask -> identity
    ASSERT_EQ(ZL_bitDeposit32(0x12345678, 0xFFFFFFFF), 0x12345678u);
    // Scatter low bits into mask positions
    // src=0b111 deposits into mask=0b10101010: bits {1,3,5} set -> 0b00101010
    ASSERT_EQ(ZL_bitDeposit32(0b111, 0b10101010), 0b00101010u);
    ASSERT_EQ(ZL_bitDeposit32(0b1010, 0b11110000), 0b10100000u);
    // Single bit
    ASSERT_EQ(ZL_bitDeposit32(1, 0x80000000u), 0x80000000u);
    ASSERT_EQ(ZL_bitDeposit32(0, 0x80000000u), 0u);
    // Verify fallback matches optimized
    std::mt19937 rng32(42);
    std::uniform_int_distribution<uint32_t> dist32;
    for (int i = 0; i < 10000; ++i) {
        uint32_t src  = dist32(rng32);
        uint32_t mask = dist32(rng32);
        ASSERT_EQ(
                ZL_bitDeposit32(src, mask),
                ZL_bitDeposit32_fallback(src, mask));
    }
}

TEST(BitsTest, bitDeposit64)
{
    ASSERT_EQ(ZL_bitDeposit64(0xFFFFFFFFFFFFFFFFull, 0), 0ull);
    ASSERT_EQ(
            ZL_bitDeposit64(0x123456789ABCDEFull, 0xFFFFFFFFFFFFFFFFull),
            0x123456789ABCDEFull);
    ASSERT_EQ(ZL_bitDeposit64(0b111, 0b10101010), 0b00101010ull);
    ASSERT_EQ(ZL_bitDeposit64(1, 0x8000000000000000ull), 0x8000000000000000ull);
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint64_t> dist64;
    for (int i = 0; i < 10000; ++i) {
        uint64_t src  = dist64(rng);
        uint64_t mask = dist64(rng);
        ASSERT_EQ(
                ZL_bitDeposit64(src, mask),
                ZL_bitDeposit64_fallback(src, mask));
    }
}

TEST(BitsTest, bitExtract32)
{
    // Zero mask -> zero result
    ASSERT_EQ(ZL_bitExtract32(0xFFFFFFFF, 0), 0u);
    // All-ones mask -> identity
    ASSERT_EQ(ZL_bitExtract32(0x12345678, 0xFFFFFFFF), 0x12345678u);
    // Gather bits from mask positions into contiguous low bits
    // mask=0b10101010 selects bits {1,3,5,7}; src=0b10101000 has {3,5,7} set
    ASSERT_EQ(ZL_bitExtract32(0b10101000, 0b10101010), 0b1110u);
    ASSERT_EQ(ZL_bitExtract32(0b10100000, 0b11110000), 0b1010u);
    // Single bit
    ASSERT_EQ(ZL_bitExtract32(0x80000000u, 0x80000000u), 1u);
    ASSERT_EQ(ZL_bitExtract32(0u, 0x80000000u), 0u);
    // Verify fallback matches optimized
    std::mt19937 rng32(123);
    std::uniform_int_distribution<uint32_t> dist32;
    for (int i = 0; i < 10000; ++i) {
        uint32_t src  = dist32(rng32);
        uint32_t mask = dist32(rng32);
        ASSERT_EQ(
                ZL_bitExtract32(src, mask),
                ZL_bitExtract32_fallback(src, mask));
    }
}

TEST(BitsTest, bitExtract64)
{
    ASSERT_EQ(ZL_bitExtract64(0xFFFFFFFFFFFFFFFFull, 0), 0ull);
    ASSERT_EQ(
            ZL_bitExtract64(0x123456789ABCDEFull, 0xFFFFFFFFFFFFFFFFull),
            0x123456789ABCDEFull);
    ASSERT_EQ(ZL_bitExtract64(0b10101000, 0b10101010), 0b1110ull);
    ASSERT_EQ(
            ZL_bitExtract64(0x8000000000000000ull, 0x8000000000000000ull),
            1ull);
    std::mt19937 rng(123);
    std::uniform_int_distribution<uint64_t> dist64;
    for (int i = 0; i < 10000; ++i) {
        uint64_t src  = dist64(rng);
        uint64_t mask = dist64(rng);
        ASSERT_EQ(
                ZL_bitExtract64(src, mask),
                ZL_bitExtract64_fallback(src, mask));
    }
}

TEST(BitsTest, bitDepositExtractRoundTrip32)
{
    // PEXT(PDEP(src, mask), mask) == src & ((1 << popcount(mask)) - 1)
    std::mt19937 rng(456);
    std::uniform_int_distribution<uint32_t> dist32;
    for (int i = 0; i < 10000; ++i) {
        uint32_t src       = dist32(rng);
        uint32_t mask      = dist32(rng);
        int nbits          = ZL_popcount64(mask);
        uint32_t srcMasked = src & ((nbits >= 32) ? ~0u : ((1u << nbits) - 1));
        ASSERT_EQ(ZL_bitExtract32(ZL_bitDeposit32(src, mask), mask), srcMasked);
    }
}

TEST(BitsTest, bitDepositExtractRoundTrip64)
{
    // PEXT(PDEP(src, mask), mask) == src & ((1 << popcount(mask)) - 1)
    std::mt19937 rng(456);
    std::uniform_int_distribution<uint64_t> dist64;
    for (int i = 0; i < 10000; ++i) {
        uint64_t src  = dist64(rng);
        uint64_t mask = dist64(rng);
        int nbits     = ZL_popcount64(mask);
        uint64_t srcMasked =
                src & ((nbits >= 64) ? ~0ull : ((1ull << nbits) - 1));
        ASSERT_EQ(ZL_bitExtract64(ZL_bitDeposit64(src, mask), mask), srcMasked);
    }
}

} // namespace
