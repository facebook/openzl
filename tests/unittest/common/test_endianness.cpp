// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/codecs/conversion/common_endianness_kernel.h"
#include "openzl/shared/bits.h"
#include "tests/utils.h"

using namespace openzl::tests;

namespace {

const std::string input = "abcdefghijklmnopqrstuvwx";
const std::string swap2 = "badcfehgjilknmporqtsvuxw";
const std::string swap4 = "dcbahgfelkjiponmtsrqxwvu";
const std::string swap8 = "hgfedcbaponmlkjixwvutsrq";

TEST(EndiannessTest, bigToLittle)
{
    std::string output;
    output.reserve(input.size());
    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_little, ZL_Endianness_big, 2);
    }
    ASSERT_EQ(output, swap2);
    output.clear();

    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_little, ZL_Endianness_big, 4);
    }
    ASSERT_EQ(output, swap4);
    output.clear();

    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_little, ZL_Endianness_big, 8);
    }
    ASSERT_EQ(output, swap8);
}

TEST(EndiannessTest, littleToBig)
{
    std::string output;
    output.reserve(input.size());
    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_big, ZL_Endianness_little, 2);
    }
    ASSERT_EQ(output, swap2);
    output.clear();

    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_big, ZL_Endianness_little, 4);
    }
    ASSERT_EQ(output, swap4);
    output.clear();

    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_big, ZL_Endianness_little, 8);
    }
    ASSERT_EQ(output, swap8);
}

TEST(EndiannessTest, same)
{
    std::string output;
    output.reserve(input.size());
    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_little, ZL_Endianness_little, 2);
    }
    ASSERT_EQ(output, input);
    output.clear();

    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_big, ZL_Endianness_big, 2);
    }
    ASSERT_EQ(output, input);
    output.clear();

    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_little, ZL_Endianness_little, 4);
    }
    ASSERT_EQ(output, input);
    output.clear();

    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_big, ZL_Endianness_big, 4);
    }
    ASSERT_EQ(output, input);
    output.clear();

    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_little, ZL_Endianness_little, 8);
    }
    ASSERT_EQ(output, input);
    output.clear();

    {
        auto rc = ZS_RC_wrapStr(input);
        auto wc = ZS_WC_StrWrapper(output);

        ZS_Endianness_transform(
                *wc, &rc, ZL_Endianness_big, ZL_Endianness_big, 8);
    }
    ASSERT_EQ(output, input);
}

} // namespace
