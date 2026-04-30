// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/buffer.h"
#include "tests/utils.h"

namespace openzl {
namespace tests {
namespace {

TEST(BufferTest, testCreateDestroy)
{
    auto b = ZL_B_create(100);
    ASSERT_EQ(ZL_B_capacity(&b), 100u);
    auto wc = ZL_B_getWC(&b);
    ASSERT_EQ(ZL_WC_avail(wc), 100u);
    auto rc = ZL_B_getRC(&b);
    ASSERT_EQ(ZL_RC_avail(&rc), 0u);
    ZL_B_destroy(&b);
}

TEST(BufferTest, testCreateNull)
{
    auto b = ZL_B_createNull();
    ASSERT_TRUE(ZL_B_isNull(&b));
    ASSERT_EQ(ZL_B_capacity(&b), 0u);
    auto wc = ZL_B_getWC(&b);
    ASSERT_EQ(ZL_WC_begin(wc), nullptr);
    ASSERT_EQ(ZL_WC_ptr(wc), nullptr);
    ASSERT_EQ(ZL_WC_avail(wc), 0u);
    auto rc = ZL_B_getRC(&b);
    ASSERT_EQ(ZL_RC_ptr(&rc), nullptr);
    ASSERT_EQ(ZL_RC_avail(&rc), 0u);
    ZL_B_destroy(&b);
}

TEST(BufferTest, testCreateZeroLength)
{
    auto b = ZL_B_create(0);
    ASSERT_EQ(ZL_B_capacity(&b), 0u);
    auto wc = ZL_B_getWC(&b);
    ASSERT_EQ(ZL_WC_avail(wc), 0u);
    auto rc = ZL_B_getRC(&b);
    ASSERT_EQ(ZL_RC_avail(&rc), 0u);
    ZL_B_destroy(&b);
}

TEST(BufferTest, testValRoundtrip)
{
    auto b = ZL_B_create(100);

    const uint32_t val = 0x12345678;
    auto wc            = ZL_B_getWC(&b);
    ZL_WC_pushLE32(wc, val);

    auto rc = ZL_B_getRC(&b);
    ASSERT_EQ(ZL_RC_avail(&rc), 4u);
    ASSERT_EQ(ZL_RC_popLE32(&rc), val);

    ZL_B_destroy(&b);
}

TEST(BufferTest, testResize)
{
    auto b  = ZL_B_create(100);
    auto wc = ZL_B_getWC(&b);
    ASSERT_EQ(ZL_WC_capacity(wc), 100u);

    std::string str1 = "abcdef";
    std::string str2 = "ghijklmnop";
    auto strc1       = ZS_RC_wrapStr(str1);
    ZL_WC_moveAll(wc, &strc1);
    ASSERT_EQ(ZL_WC_size(wc), str1.size());

    ZL_B_resize(&b, 200);
    ASSERT_EQ(ZL_WC_capacity(wc), 200u);
    auto strc2 = ZS_RC_wrapStr(str2);
    ZL_WC_moveAll(wc, &strc2);

    ASSERT_EQ(ZL_WC_size(wc), str1.size() + str2.size());

    auto rc  = ZL_B_getRC(&b);
    auto str = ZS_RC_toStr(&rc);
    ASSERT_EQ(str, str1 + str2);

    ZL_B_destroy(&b);
}

TEST(BufferTest, testResizeFromNull)
{
    auto b = ZL_B_createNull();
    ASSERT_TRUE(ZL_B_isNull(&b));
    ASSERT_EQ(ZL_B_capacity(&b), 0u);
    ZL_B_resize(&b, 100);
    ASSERT_FALSE(ZL_B_isNull(&b));
    ASSERT_EQ(ZL_B_capacity(&b), 100u);
    ZL_B_destroy(&b);
}

TEST(BufferTest, testResizeToZero)
{
    auto b = ZL_B_create(100);
    ASSERT_EQ(ZL_B_capacity(&b), 100u);
    ASSERT_FALSE(ZL_B_isNull(&b));
    ZL_B_resize(&b, 0);
    ASSERT_EQ(ZL_B_capacity(&b), 0u);
    // ASSERT_TRUE(ZL_B_isNull(&b));  // I don't think that's always true. It
    // actually depends on underlying reallocator policy. This test fails on
    // MacOS.
    ZL_B_destroy(&b);
}

TEST(BufferTest, testMove)
{
    auto b1 = ZL_B_create(100);
    auto b2 = ZL_B_move(&b1);

    ZL_B_destroy(&b1);
    ZL_B_destroy(&b2);
}

} // namespace
} // namespace tests
} // namespace openzl
