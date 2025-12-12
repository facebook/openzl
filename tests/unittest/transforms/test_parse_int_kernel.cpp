// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <vector>

#include "openzl/codecs/parse_int/encode_parse_int_kernel.h"
#include "tests/datagen/random_producer/PRNGWrapper.h"
#include "tests/datagen/structures/IntegerStringProducer.h"

namespace {
TEST(ParseIntKernelTest, UnsafeVsFallbackSuccess)
{
    std::vector<std::string> inputs = { "0",     "100",    "200",
                                        "-3000", "-45000", "500000" };
    for (size_t i = 0; i < inputs.size(); ++i) {
        int64_t value;
        auto success = ZL_parseInt64Unsafe(
                &value, inputs[i].data(), inputs[i].data() + inputs[i].size());
        EXPECT_TRUE(success);
        int64_t fallbackValue;
        success = ZL_parseInt64_fallback(
                &fallbackValue,
                inputs[i].data(),
                inputs[i].data() + inputs[i].size());
        EXPECT_TRUE(success);
        EXPECT_EQ(value, fallbackValue);
    }
}

TEST(ParseIntKernelTest, UnsafeVsFallbackFailure)
{
    std::vector<std::string> inputs = { "100000000000000000000",
                                        "-100000000000000000000",
                                        "0xa0",
                                        "-01",
                                        "--2",
                                        "+5",
                                        "-0",
                                        "2.5" };
    for (size_t i = 0; i < inputs.size(); ++i) {
        int64_t value;
        auto success = ZL_parseInt64Unsafe(
                &value, inputs[i].data(), inputs[i].data() + inputs[i].size());
        EXPECT_FALSE(success);
        int64_t fallbackValue;
        success = ZL_parseInt64_fallback(
                &fallbackValue,
                inputs[i].data(),
                inputs[i].data() + inputs[i].size());
        EXPECT_FALSE(success);
    }
}

TEST(ParseIntKernelTest, GeneratedRandom)
{
    auto rw = std::make_shared<openzl::tests::datagen::PRNGWrapper>(
            std::make_shared<std::mt19937>());
    auto gen = openzl::tests::datagen::IntegerStringProducer(rw);
    for (size_t i = 0; i < 1000; i++) {
        for (auto& intStr : gen("intstring_vec")) {
            int64_t value;
            auto success = ZL_parseInt64Unsafe(
                    &value, intStr.data(), intStr.data() + intStr.size());
            EXPECT_TRUE(success);
            int64_t fallbackValue;
            success = ZL_parseInt64_fallback(
                    &fallbackValue,
                    intStr.data(),
                    intStr.data() + intStr.size());
            EXPECT_TRUE(success);
            EXPECT_EQ(value, fallbackValue);
        }
    }
}
} // namespace
