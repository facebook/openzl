// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "tests/datagen/DataGen.h"

using namespace ::testing;

namespace openzl::tests::datagen {

TEST(DataGenTest, RandVal)
{
    auto dg         = DataGen();
    const auto data = dg.template randVal<uint32_t>("val1");
    std::cout << data << std::endl;

    const auto data1 = dg.template randVal<float>("val2", 0.001);
    EXPECT_GT(data1, 0.0);

    const auto data2 = dg.template randVal<double>("val3", 0.001);
    EXPECT_GT(data2, 0.0);
}

TEST(DataGenTest, RandVec)
{
    auto dg         = DataGen();
    const auto data = dg.template randVector<uint32_t>("randVec", 0, 100, 1000);
    EXPECT_LT(data.size(), 1000);
    for (auto v : data) {
        EXPECT_GE(v, 0);
        EXPECT_LE(v, 100);
    }
}

TEST(DataGenTest, RandVecVec)
{
    auto dg         = DataGen();
    const auto data = dg.template randVectorVector<uint32_t>(
            "randVecVec", 0, 100, 1000, 100);
    EXPECT_LT(data.size(), 1000);
    for (const auto& vec : data) {
        EXPECT_LT(vec.size(), 100);
        for (auto v : vec) {
            EXPECT_GE(v, 0);
            EXPECT_LE(v, 100);
        }
    }
}

TEST(DataGenTest, RandLongVec)
{
    auto dg         = DataGen();
    const auto data = dg.template randLongVector<uint32_t>(
            "randLongVec", 0, 100, 1000, 1001);
    EXPECT_EQ(data.size(), 1000);
    for (auto v : data) {
        EXPECT_GE(v, 0);
        EXPECT_LE(v, 100);
    }

    EXPECT_THROW(
            {
                try {
                    const auto data2 = dg.template randLongVector<uint32_t>(
                            "randLongVec", 0, 100, 1000, 100);
                } catch (const std::runtime_error& e) {
                    EXPECT_STREQ(e.what(), "VecLengthDistribution: min > max");
                    throw;
                }
            },
            std::runtime_error);
}

TEST(DataGenTest, RandString)
{
    auto dg   = DataGen();
    auto data = dg.randString("randstring");
    EXPECT_LT(data.size(), 4096);
}

} // namespace openzl::tests::datagen
