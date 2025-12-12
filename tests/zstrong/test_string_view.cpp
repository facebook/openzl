// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "openzl/shared/string_view.h"
#include "tests/datagen/DataGen.h"

namespace openzl {
namespace tests {
class StringViewTest : public ::testing::Test {
   protected:
    datagen::DataGen dataGen_;
};

TEST_F(StringViewTest, BasicEquality)
{
    for (size_t i = 0; i < 1000; ++i) {
        std::vector<uint8_t> sample =
                dataGen_.randLongVector<uint8_t>("sv_sample", 0, 255, 1, 1000);
        std::vector<uint8_t> sampleCopy;
        std::copy(sample.begin(), sample.end(), std::back_inserter(sampleCopy));
        auto sv1 = StringView_init((char*)sample.data(), sample.size());
        auto sv2 = StringView_init((char*)sampleCopy.data(), sampleCopy.size());
        EXPECT_TRUE(StringView_eq(&sv1, &sv2));
    }
    std::string s1("Hello");
    std::string s2("Not Hello");
    auto sv1 = StringView_init(s1.data(), s1.size());
    auto sv2 = StringView_init(s2.data(), s2.size());
    EXPECT_FALSE(StringView_eq(&sv1, &sv2));
}

TEST_F(StringViewTest, CstrInitialization)
{
    char s1[] = "cstr sample";
    std::string s2("cstr sample");
    auto sv1 = StringView_initFromCStr(s1);
    auto sv2 = StringView_init(s2.data(), s2.size());
    EXPECT_TRUE(StringView_eq(&sv1, &sv2));
}

TEST_F(StringViewTest, DataAdvances)
{
    std::string s("123456789");
    auto sv_base = StringView_init(s.data(), s.size());
    auto sv      = StringView_init(s.data(), s.size());
    for (size_t i = 1; i < 9; i++) {
        StringView_advance(&sv, 1);
        auto subView = StringView_substr(&sv_base, i, sv_base.size - i);
        EXPECT_TRUE(StringView_eq(&sv, &subView));
    }
}

TEST_F(StringViewTest, DataIsByReference)
{
    std::string sample("Hello");
    auto sv = StringView_init(sample.data(), sample.size());
    EXPECT_EQ(strcmp((const char*)sv.data, "Hello"), 0);
    sample.clear();
    EXPECT_EQ(strcmp((const char*)sv.data, ""), 0);
}

TEST_F(StringViewTest, SubstringView)
{
    std::string s1("Hello");
    std::string s2("Not Hello");
    auto sv1     = StringView_init(s1.data(), s1.size());
    auto sv2     = StringView_init(s2.data(), s2.size());
    auto sv2Sub1 = StringView_substr(&sv2, 4, 5);
    auto sv2Sub2 = StringView_substr(&sv2, 4, 4);
    auto sv2Sub3 = StringView_substr(&sv2, 3, 5);
    EXPECT_TRUE(StringView_eq(&sv1, &sv2Sub1));
    EXPECT_FALSE(StringView_eq(&sv1, &sv2Sub2));
    EXPECT_FALSE(StringView_eq(&sv1, &sv2Sub3));

    std::string s3("aabbaabbaa");
    std::string s4("abaabbaabb");
    auto sv3     = StringView_init(s3.data(), s3.size());
    auto sv4     = StringView_init(s4.data(), s4.size());
    auto sv3Sub1 = StringView_substr(&sv3, 0, 6);
    auto sv3Sub2 = StringView_substr(&sv3, 4, 6);
    auto sv4Sub1 = StringView_substr(&sv4, 2, 6);
    EXPECT_TRUE(StringView_eq(&sv3Sub1, &sv4Sub1));
    EXPECT_TRUE(StringView_eq(&sv3Sub1, &sv3Sub2));
}
} // namespace tests
} // namespace openzl
