// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "openzl/common/assertion.h"
#include "openzl/compress/selectors/transformer/wide_arith.h"

namespace {

void expectWideEq(TRS_WideU128 actual, TRS_WideU128 expected)
{
    EXPECT_EQ(actual.hi, expected.hi);
    EXPECT_EQ(actual.lo, expected.lo);
}

TEST(TransformerWideArithmeticTest, PreservesHighBits)
{
    const TRS_WideU128 product = TRS_wide_u128_mul_u64(
            std::numeric_limits<uint64_t>::max(), uint64_t{ 2 });
    EXPECT_EQ(product.hi, 1);
    EXPECT_EQ(product.lo, std::numeric_limits<uint64_t>::max() - 1);
    EXPECT_EQ(
            TRS_wide_u128_div_u64_to_u64(product, 2),
            std::numeric_limits<uint64_t>::max());
}

TEST(TransformerWideArithmeticTest, SaturatesAbsoluteValueToU64)
{
    const TRS_WideI128 value = { .hi = 1, .lo = 0 };
    EXPECT_EQ(
            TRS_wide_i128_abs_to_u64(value),
            std::numeric_limits<uint64_t>::max());
}

TEST(TransformerWideArithmeticTest, SaturatesScaledDivisionResults)
{
    const TRS_WideU128 nativeProductOverflow = {
        .hi = uint64_t{ 1 } << 32,
        .lo = 0,
    };

    EXPECT_EQ(
            TRS_wide_u128_scaled_div_to_u64(
                    TRS_wide_u128_from_u64(uint64_t{ 1 } << 32), 1),
            std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(
            TRS_wide_u128_scaled_div_to_u64(nativeProductOverflow, 1),
            std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(
            TRS_wide_i128_scaled_div_to_i64(
                    { .hi = nativeProductOverflow.hi,
                      .lo = nativeProductOverflow.lo },
                    1),
            std::numeric_limits<int64_t>::max());
    EXPECT_EQ(
            TRS_wide_i128_scaled_div_to_i64(
                    TRS_wide_i128_from_i64(std::numeric_limits<int64_t>::max()),
                    1),
            std::numeric_limits<int64_t>::max());
    EXPECT_EQ(
            TRS_wide_i128_scaled_div_to_i64(
                    TRS_wide_i128_from_i64(std::numeric_limits<int64_t>::min()),
                    1),
            std::numeric_limits<int64_t>::min());
}

TEST(TransformerWideArithmeticTest, SaturatesWideValuesAtIntegerBounds)
{
    const uint64_t signBit = uint64_t{ 1 } << 63;
    const uint64_t u64Max  = std::numeric_limits<uint64_t>::max();

    EXPECT_EQ(
            TRS_wide_u128_saturate_to_u64({ .hi = 0, .lo = 42 }),
            uint64_t{ 42 });
    EXPECT_EQ(TRS_wide_u128_saturate_to_u64({ .hi = 1, .lo = 0 }), u64Max);

    EXPECT_EQ(
            TRS_wide_i128_saturate_to_i64(
                    { .hi = 0,
                      .lo = (uint64_t)std::numeric_limits<int64_t>::max() }),
            std::numeric_limits<int64_t>::max());
    EXPECT_EQ(
            TRS_wide_i128_saturate_to_i64({ .hi = u64Max, .lo = signBit }),
            std::numeric_limits<int64_t>::min());
    EXPECT_EQ(
            TRS_wide_i128_saturate_to_i64({ .hi = 1, .lo = 0 }),
            std::numeric_limits<int64_t>::max());
    EXPECT_EQ(
            TRS_wide_i128_saturate_to_i64({ .hi = u64Max - 1, .lo = 0 }),
            std::numeric_limits<int64_t>::min());
    EXPECT_EQ(
            TRS_wide_i128_saturate_to_i64({ .hi = 0, .lo = signBit }),
            std::numeric_limits<int64_t>::max());
    EXPECT_EQ(
            TRS_wide_i128_saturate_to_i64({ .hi = u64Max, .lo = signBit - 1 }),
            std::numeric_limits<int64_t>::min());
}

TEST(TransformerWideArithmeticTest, AbsoluteValueHandlesFullWideRange)
{
    const uint64_t signBit = uint64_t{ 1 } << 63;
    const uint64_t u64Max  = std::numeric_limits<uint64_t>::max();

    expectWideEq(TRS_wide_i128_abs({ .hi = 1, .lo = 2 }), { .hi = 1, .lo = 2 });
    expectWideEq(
            TRS_wide_i128_abs({ .hi = u64Max, .lo = 0 }), { .hi = 1, .lo = 0 });
    expectWideEq(
            TRS_wide_i128_abs({ .hi = signBit, .lo = 0 }),
            { .hi = signBit, .lo = 0 });
}

TEST(TransformerWideArithmeticTest, DoubleConversionPreservesSignAndHighBits)
{
    const uint64_t signBit = uint64_t{ 1 } << 63;
    const uint64_t u64Max  = std::numeric_limits<uint64_t>::max();

    EXPECT_DOUBLE_EQ(TRS_wide_u128_to_double({ .hi = 0, .lo = 0 }), 0.0);
    EXPECT_DOUBLE_EQ(TRS_wide_u128_to_double({ .hi = 0, .lo = 1 }), 1.0);
    EXPECT_DOUBLE_EQ(TRS_wide_u128_to_double({ .hi = 1, .lo = 0 }), 0x1p64);

    EXPECT_DOUBLE_EQ(TRS_wide_i128_to_double(TRS_wide_i128_from_i64(-1)), -1.0);
    EXPECT_DOUBLE_EQ(TRS_wide_i128_to_double({ .hi = 1, .lo = 0 }), 0x1p64);
    EXPECT_DOUBLE_EQ(
            TRS_wide_i128_to_double({ .hi = u64Max, .lo = 0 }), -0x1p64);
    EXPECT_DOUBLE_EQ(
            TRS_wide_i128_to_double({ .hi = signBit, .lo = 0 }), -0x1p127);
}

TEST(TransformerWideArithmeticTest, RejectsDivisionOverflow)
{
    const TRS_WideU128 unsignedOverflow = { .hi = 1, .lo = 0 };
    const TRS_WideU128 one              = { .hi = 0, .lo = 1 };
    const TRS_WideU128 zero             = { 0, 0 };
    const TRS_WideI128 signedOverflow   = {
          .hi = 0,
          .lo = uint64_t{ 1 } << 63,
    };

#if ZL_ENABLE_ASSERT
    EXPECT_DEATH((void)TRS_wide_u128_div_u64_to_u64(unsignedOverflow, 1), "");
    EXPECT_DEATH(
            (void)TRS_wide_u128_div_u128_to_u64(unsignedOverflow, one), "");
    EXPECT_DEATH((void)TRS_wide_u128_scaled_div_to_u64(zero, 0), "");
    EXPECT_DEATH((void)TRS_wide_i128_div_u64_to_i64(signedOverflow, 1), "");
#else
    EXPECT_EQ(
            TRS_wide_u128_div_u64_to_u64(unsignedOverflow, 1),
            std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(
            TRS_wide_u128_div_u128_to_u64(unsignedOverflow, one),
            std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(
            TRS_wide_u128_scaled_div_to_u64(zero, 0),
            std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(
            TRS_wide_i128_div_u64_to_i64(signedOverflow, 1),
            std::numeric_limits<int64_t>::max());
#endif
}

#if defined(OPENZL_TEST_COMPILER_HAS_INT128_DIVISION)
#    define TRS_TEST_HAVE_NATIVE_INT128 OPENZL_TEST_COMPILER_HAS_INT128_DIVISION
#elif defined(__SIZEOF_INT128__) && !defined(_MSC_VER)
#    define TRS_TEST_HAVE_NATIVE_INT128 1
#else
#    define TRS_TEST_HAVE_NATIVE_INT128 0
#endif

#if TRS_TEST_HAVE_NATIVE_INT128

using NativeU128 = __uint128_t;
using NativeI128 = __int128;

TRS_WideU128 wideFromNative(NativeU128 value)
{
    return { .hi = (uint64_t)(value >> 64), .lo = (uint64_t)value };
}

TRS_WideI128 wideFromNative(NativeI128 value)
{
    const NativeU128 bits = (NativeU128)value;
    return { .hi = (uint64_t)(bits >> 64), .lo = (uint64_t)bits };
}

void expectWideEq(TRS_WideU128 actual, NativeU128 expected)
{
    const TRS_WideU128 expectedWide = wideFromNative(expected);
    EXPECT_EQ(actual.hi, expectedWide.hi);
    EXPECT_EQ(actual.lo, expectedWide.lo);
}

void expectWideEq(TRS_WideI128 actual, NativeI128 expected)
{
    const TRS_WideI128 expectedWide = wideFromNative(expected);
    EXPECT_EQ(actual.hi, expectedWide.hi);
    EXPECT_EQ(actual.lo, expectedWide.lo);
}

uint64_t nativeScaledDiv(NativeU128 sum, size_t count)
{
    const NativeU128 quotient = sum / count;
    if (quotient > (std::numeric_limits<uint64_t>::max() >> 32)) {
        return std::numeric_limits<uint64_t>::max();
    }

    const NativeU128 remainder = sum % count;
    return (uint64_t)((quotient << 32) + ((remainder << 32) / count));
}

int64_t nativeSignedScaledDiv(NativeI128 sum, size_t count)
{
    return (int64_t)((sum * ((NativeI128)1 << 32)) / (NativeI128)count);
}

TEST(TransformerWideArithmeticTest, UnsignedOperationsMatchNative128)
{
    constexpr std::array<uint64_t, 7> values = {
        0,
        1,
        std::numeric_limits<uint32_t>::max(),
        uint64_t{ 1 } << 32,
        uint64_t{ 1 } << 63,
        std::numeric_limits<uint64_t>::max() - 1,
        std::numeric_limits<uint64_t>::max(),
    };

    for (const uint64_t lhs : values) {
        SCOPED_TRACE(lhs);
        expectWideEq(TRS_wide_u128_from_u64(lhs), (NativeU128)lhs);
        expectWideEq(
                TRS_wide_u128_shl32(TRS_wide_u128_from_u64(lhs)),
                (NativeU128)lhs << 32);

        for (const uint64_t rhs : values) {
            SCOPED_TRACE(rhs);
            expectWideEq(
                    TRS_wide_u128_add_u64(TRS_wide_u128_from_u64(lhs), rhs),
                    (NativeU128)lhs + rhs);

            const TRS_WideU128 product = TRS_wide_u128_mul_u64(lhs, rhs);
            expectWideEq(product, (NativeU128)lhs * rhs);
            if (rhs != 0) {
                EXPECT_EQ(TRS_wide_u128_div_u64_to_u64(product, rhs), lhs);
            }
        }
    }
}

TEST(TransformerWideArithmeticTest, WideDivisionMatchesNative128)
{
    struct DivisionCase {
        NativeU128 numerator;
        NativeU128 denominator;
    };
    const std::array<DivisionCase, 7> cases = {
        DivisionCase{ .numerator = 0, .denominator = 1 },
        DivisionCase{ .numerator   = ((NativeU128)5 << 64) + 10,
                      .denominator = ((NativeU128)2 << 64) + 3 },
        DivisionCase{ .numerator   = ~(NativeU128)0,
                      .denominator = (NativeU128)1 << 64 },
        DivisionCase{ .numerator   = (NativeU128)1 << 64,
                      .denominator = ((NativeU128)1 << 64) + 1 },
        DivisionCase{ .numerator   = ((NativeU128)1 << 127) - 1,
                      .denominator = (NativeU128)1 << 127 },
        DivisionCase{ .numerator   = (NativeU128)1 << 127,
                      .denominator = (NativeU128)1 << 127 },
        DivisionCase{ .numerator   = ~(NativeU128)0,
                      .denominator = ((NativeU128)1 << 127) + 1 },
    };

    for (const auto& testCase : cases) {
        EXPECT_EQ(
                TRS_wide_u128_div_u128_to_u64(
                        wideFromNative(testCase.numerator),
                        wideFromNative(testCase.denominator)),
                (uint64_t)(testCase.numerator / testCase.denominator));
    }
}

TEST(TransformerWideArithmeticTest, ScaledDivisionMatchesNative128)
{
    struct ScaledDivisionCase {
        NativeU128 sum;
        size_t count;
    };
    const std::array<ScaledDivisionCase, 5> cases = {
        ScaledDivisionCase{ .sum = 0, .count = 1 },
        ScaledDivisionCase{ .sum = 1, .count = 3 },
        ScaledDivisionCase{ .sum = 5, .count = 2 },
        ScaledDivisionCase{ .sum   = std::numeric_limits<uint32_t>::max(),
                            .count = 7 },
        ScaledDivisionCase{ .sum = (NativeU128)1 << 64, .count = 3 },
    };

    for (const auto& testCase : cases) {
        EXPECT_EQ(
                TRS_wide_u128_scaled_div_to_u64(
                        wideFromNative(testCase.sum), testCase.count),
                nativeScaledDiv(testCase.sum, testCase.count));
    }
}

TEST(TransformerWideArithmeticTest, SignedScaledDivisionMatchesNative128)
{
    struct ScaledDivisionCase {
        NativeI128 sum;
        size_t count;
    };
    const std::array<ScaledDivisionCase, 7> cases = {
        ScaledDivisionCase{ .sum = 0, .count = 1 },
        ScaledDivisionCase{ .sum = 1, .count = 3 },
        ScaledDivisionCase{ .sum = -1, .count = 3 },
        ScaledDivisionCase{ .sum = 5, .count = 2 },
        ScaledDivisionCase{ .sum = -5, .count = 2 },
        ScaledDivisionCase{ .sum   = std::numeric_limits<int32_t>::max(),
                            .count = 7 },
        ScaledDivisionCase{ .sum   = std::numeric_limits<int32_t>::min(),
                            .count = 7 },
    };

    for (const auto& testCase : cases) {
        EXPECT_EQ(
                TRS_wide_i128_scaled_div_to_i64(
                        wideFromNative(testCase.sum), testCase.count),
                nativeSignedScaledDiv(testCase.sum, testCase.count));
    }
}

TEST(TransformerWideArithmeticTest, SignedOperationsMatchNative128)
{
    constexpr std::array<int64_t, 7> values = {
        std::numeric_limits<int64_t>::min(),
        -1,
        0,
        1,
        std::numeric_limits<int32_t>::max(),
        std::numeric_limits<int64_t>::max() - 1,
        std::numeric_limits<int64_t>::max(),
    };

    for (const int64_t lhs : values) {
        const TRS_WideI128 wideLhs = TRS_wide_i128_from_i64(lhs);
        SCOPED_TRACE(lhs);
        expectWideEq(wideLhs, (NativeI128)lhs);
        EXPECT_EQ(
                TRS_wide_abs_i64_to_u64(lhs),
                (uint64_t)((lhs < 0) ? -(NativeI128)lhs : (NativeI128)lhs));
        EXPECT_EQ(TRS_wide_i128_div_u64_to_i64(wideLhs, 3), lhs / 3);

        for (const int64_t rhs : values) {
            SCOPED_TRACE(rhs);
            expectWideEq(
                    TRS_wide_i128_add_i64(wideLhs, rhs), (NativeI128)lhs + rhs);
            expectWideEq(
                    TRS_wide_i128_sub_i64(wideLhs, rhs), (NativeI128)lhs - rhs);
            EXPECT_EQ(
                    TRS_wide_i128_lt(wideLhs, TRS_wide_i128_from_i64(rhs)),
                    lhs < rhs);
            EXPECT_EQ(
                    TRS_wide_i128_gt(wideLhs, TRS_wide_i128_from_i64(rhs)),
                    lhs > rhs);
            EXPECT_EQ(
                    TRS_wide_i64_abs_diff_to_u64(lhs, rhs),
                    (uint64_t)(((NativeI128)lhs < (NativeI128)rhs)
                                       ? (NativeI128)rhs - lhs
                                       : (NativeI128)lhs - rhs));
        }
    }
}

#endif // TRS_TEST_HAVE_NATIVE_INT128

} // namespace
