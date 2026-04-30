// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <random>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <folly/Conv.h>

#include "custom_transforms/parse/tests/parse_test_data.h"

using namespace ::testing;

namespace openzl::tests::parse {

namespace {
void testRoundTrip(std::vector<std::string> const& data)
{
    auto const [content, fieldSizes] = flatten(data);
    auto compressed                  = compress(data, Type::Int64);
    auto decompressed                = decompress(compressed, Type::Int64);
    EXPECT_EQ(decompressed, content);

    compressed   = compress(data, Type::Float64);
    decompressed = decompress(compressed, Type::Float64);
    EXPECT_EQ(decompressed, content);
}

TEST(TestParse, Basic)
{
    testRoundTrip({ "0", "1", "100", "200" });
    testRoundTrip({ "-1", "-5", "-10" });
    testRoundTrip({ "0", "-0", "0.5", "-0.5" });
    testRoundTrip({ "0.5e-5", "0.5e-6", "0.5e-7", "0.5e-8" });
    testRoundTrip({ "0.5E-5", "0.5E-6", "0.5E-7", "0.5E-8" });
    testRoundTrip({ "9223372036854775807", "-9223372036854775808" });
    testRoundTrip(
            {
                    "1",
                    "10",
                    "100",
                    "1000",
                    "10000",
                    "100000",
                    "1000000",
                    "10000000",
                    "100000000",
                    "1000000000",
                    "10000000000",
                    "100000000000",
                    "1000000000000",
                    "10000000000000",
                    "100000000000000",
                    "1000000000000000",
                    "10000000000000000",
                    "100000000000000000",
                    "1000000000000000000",
                    "10000000000000000000",
            });
    testRoundTrip(
            {
                    "-1",
                    "-10",
                    "-100",
                    "-1000",
                    "-10000",
                    "-100000",
                    "-1000000",
                    "-10000000",
                    "-100000000",
                    "-1000000000",
                    "-10000000000",
                    "-100000000000",
                    "-1000000000000",
                    "-10000000000000",
                    "-100000000000000",
                    "-1000000000000000",
                    "-10000000000000000",
                    "-100000000000000000",
                    "-1000000000000000000",
                    "-100000000000000000000",
            });
    testRoundTrip(
            { "0",
              "9",
              "99",
              "999",
              "9999",
              "99999",
              "999999",
              "9999999",
              "99999999",
              "999999999",
              "9999999999",
              "99999999999",
              "999999999999",
              "9999999999999",
              "99999999999999",
              "999999999999999",
              "9999999999999999",
              "99999999999999999",
              "999999999999999999",
              "9999999999999999999" });
    testRoundTrip(
            { "-9",
              "-99",
              "-999",
              "-9999",
              "-99999",
              "-999999",
              "-9999999",
              "-99999999",
              "-999999999",
              "-9999999999",
              "-99999999999",
              "-999999999999",
              "-9999999999999",
              "-99999999999999",
              "-999999999999999",
              "-9999999999999999",
              "-99999999999999999",
              "-999999999999999999",
              "-9999999999999999999" });
    testRoundTrip(
            { "37303787483182993275",
              "37303787483182993275",
              "37303787483182993275",
              "37303787483182993275" });
}

TEST(TestParse, Generated)
{
    std::mt19937 gen(0xdeadbeef);
    for (size_t length = 1; length < 1000; ++length) {
        auto data = genData(gen, length, Type::Int64);
        testRoundTrip(data);
        data = genData(gen, length, Type::Float64);
        testRoundTrip(data);
    }
}

TEST(TestParse, Random)
{
    std::mt19937 gen(0xdeadbeef);
    std::uniform_int_distribution<int> choiceDist(0, 2);
    std::uniform_int_distribution<int64_t> intDist;
    std::uniform_real_distribution<double> doubleDist;
    std::uniform_int_distribution<size_t> lenDist(0, 1000);
    for (size_t i = 0; i < 100; i++) {
        std::vector<std::string> data;
        auto const len = lenDist(gen);
        for (size_t l = 0; l < len; ++l) {
            auto const choice = choiceDist(gen);
            if (choice == 0) {
                data.push_back(folly::to<std::string>(intDist(gen)));
            } else if (choice == 1) {
                data.push_back(folly::to<std::string>(doubleDist(gen)));
            } else {
                data.push_back(folly::to<std::string>("a", intDist(gen)));
            }
        }
        testRoundTrip(data);
    }
}

} // namespace
} // namespace openzl::tests::parse
