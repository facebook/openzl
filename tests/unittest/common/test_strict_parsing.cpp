// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "cli/utils/util.h"

using namespace openzl::cli;

// Tests for parseStrictInt
TEST(StrictParsingTest, ParseStrictInt_AcceptsValidInputs)
{
    EXPECT_EQ(util::parseStrictInt("0"), 0);
    EXPECT_EQ(util::parseStrictInt("123"), 123);
    EXPECT_EQ(util::parseStrictInt("-456"), -456);
    EXPECT_EQ(util::parseStrictInt("2147483647"), 2147483647); // INT_MAX
}

TEST(StrictParsingTest, ParseStrictInt_RejectsTrailingCharacters)
{
    EXPECT_THROW(util::parseStrictInt("123abc"), InvalidArgsException);
    EXPECT_THROW(util::parseStrictInt("456xyz"), InvalidArgsException);
    EXPECT_THROW(util::parseStrictInt("10extra"), InvalidArgsException);
}

TEST(StrictParsingTest, ParseStrictInt_RejectsInvalidInputs)
{
    EXPECT_THROW(util::parseStrictInt(""), InvalidArgsException);      // empty
    EXPECT_THROW(util::parseStrictInt("abc"), InvalidArgsException);   // no digits
}

// Tests for parseStrictULong
TEST(StrictParsingTest, ParseStrictULong_AcceptsValidInputs)
{
    EXPECT_EQ(util::parseStrictULong("0"), 0UL);
    EXPECT_EQ(util::parseStrictULong("123"), 123UL);
    EXPECT_EQ(util::parseStrictULong("1000000"), 1000000UL);
}

TEST(StrictParsingTest, ParseStrictULong_RejectsTrailingCharacters)
{
    EXPECT_THROW(util::parseStrictULong("100abc"), InvalidArgsException);
    EXPECT_THROW(util::parseStrictULong("50threads"), InvalidArgsException);
}

TEST(StrictParsingTest, ParseStrictULong_RejectsInvalidInputs)
{
    EXPECT_THROW(util::parseStrictULong(""), InvalidArgsException);
    // Note: std::stoul accepts negative numbers (wraps to large positive), so we don't reject them
}

// Tests for parseStrictULL
TEST(StrictParsingTest, ParseStrictULL_AcceptsValidInputs)
{
    EXPECT_EQ(util::parseStrictULL("0"), 0ULL);
    EXPECT_EQ(util::parseStrictULL("123"), 123ULL);
    EXPECT_EQ(util::parseStrictULL("9999999999"), 9999999999ULL);
}

TEST(StrictParsingTest, ParseStrictULL_RejectsTrailingCharacters)
{
    EXPECT_THROW(util::parseStrictULL("1000abc"), InvalidArgsException);
    EXPECT_THROW(util::parseStrictULL("2000MB"), InvalidArgsException);  // common mistake
}

TEST(StrictParsingTest, ParseStrictULL_RejectsInvalidInputs)
{
    EXPECT_THROW(util::parseStrictULL(""), InvalidArgsException);
    // Note: std::stoull accepts negative numbers (wraps to large positive), so we don't reject them
}

TEST(StrictParsingTest, Issue225_RegressionTest)
{
    EXPECT_THROW(util::parseStrictInt("123abc"), InvalidArgsException);
    EXPECT_THROW(util::parseStrictULong("456def"), InvalidArgsException);
    EXPECT_THROW(util::parseStrictULL("789ghi"), InvalidArgsException);
}
