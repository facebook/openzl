// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string>

#include <gtest/gtest.h>

#include "cli/utils/util.h"

using namespace openzl::cli::util;
using openzl::cli::InvalidArgsException;

namespace {

// --- checkedstoi ---

TEST(UtilTest, StoiPlainIntegers)
{
    EXPECT_EQ(checkedstoi("0"), 0);
    EXPECT_EQ(checkedstoi("42"), 42);
    EXPECT_EQ(checkedstoi("-1"), -1);
    EXPECT_EQ(checkedstoi("100"), 100);
}

TEST(UtilTest, StoiDecimalSuffixes)
{
    EXPECT_EQ(checkedstoi("1K"), 1000);
    EXPECT_EQ(checkedstoi("1KB"), 1000);
    EXPECT_EQ(checkedstoi("2M"), 2000000);
    EXPECT_EQ(checkedstoi("2MB"), 2000000);
}

TEST(UtilTest, StoiBinarySuffixes)
{
    EXPECT_EQ(checkedstoi("1KiB"), 1024);
    EXPECT_EQ(checkedstoi("1MiB"), 1048576);
}

TEST(UtilTest, StoiEmptyString)
{
    EXPECT_THROW(checkedstoi(""), InvalidArgsException);
}

TEST(UtilTest, StoiInvalidNumber)
{
    EXPECT_THROW(checkedstoi("abc"), InvalidArgsException);
}

TEST(UtilTest, StoiInvalidSuffix)
{
    EXPECT_THROW(checkedstoi("1X"), InvalidArgsException);
    EXPECT_THROW(checkedstoi("1foo"), InvalidArgsException);
}

TEST(UtilTest, StoiOverflow)
{
    EXPECT_THROW(checkedstoi("3G"), InvalidArgsException);
}

TEST(UtilTest, StoulDecimalSuffixes)
{
    EXPECT_EQ(checkedstoul("1K"), 1000UL);
    EXPECT_EQ(checkedstoul("1KB"), 1000UL);
    EXPECT_EQ(checkedstoul("2M"), 2000000UL);
    EXPECT_EQ(checkedstoul("2MB"), 2000000UL);
    EXPECT_EQ(checkedstoul("3G"), 3000000000UL);
    EXPECT_EQ(checkedstoul("3GB"), 3000000000UL);
    if constexpr (sizeof(unsigned long) >= 8) {
        EXPECT_EQ(checkedstoul("1T"), 1000000000000UL);
        EXPECT_EQ(checkedstoul("1TB"), 1000000000000UL);
    } else {
        EXPECT_THROW(checkedstoul("1T"), InvalidArgsException);
        EXPECT_THROW(checkedstoul("1TB"), InvalidArgsException);
    }
}

TEST(UtilTest, StoulBinarySuffixes)
{
    EXPECT_EQ(checkedstoul("1KiB"), 1024UL);
    EXPECT_EQ(checkedstoul("1MiB"), 1048576UL);
    EXPECT_EQ(checkedstoul("1GiB"), 1073741824UL);
    if constexpr (sizeof(unsigned long) >= 8) {
        EXPECT_EQ(checkedstoul("1TiB"), 1099511627776UL);
    } else {
        EXPECT_THROW(checkedstoul("1TiB"), InvalidArgsException);
    }
}

TEST(UtilTest, StoullOverflow)
{
    EXPECT_THROW(checkedstoull("18446744073709551615T"), InvalidArgsException);
}

} // namespace
