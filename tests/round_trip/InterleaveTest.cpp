// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "tests/datagen/DataGen.h"
#include "tests/datagen/structures/openzl/StringInputProducer.h"
#include "tests/zstrong/InterleaveTestFixture.h"

using namespace ::testing;

namespace openzl::tests {

using zstrong::tests::datagen::openzl::PreStringInput;
using zstrong::tests::datagen::openzl::StringInputProducer;

TEST_F(InterleaveTest, MultipleInputs)
{
    auto dg         = zstrong::tests::datagen::DataGen();
    size_t nbInputs = dg.randVal("nbInputs", 5, 100);
    uint32_t nbStrs = dg.u32_range("nbStrs", 100, 200);

    auto sip = StringInputProducer(
            dg.getRandWrapper(), StringInputProducer::Strategy::RoughlyEven);
    std::vector<PreStringInput> preInputs;
    std::vector<Input> zlInputs;
    preInputs.reserve(nbInputs);
    zlInputs.reserve(nbInputs);
    while (nbInputs--) {
        auto stringInput = sip("input", nbStrs);
        // make sure the test doesn't run on empty data
        if (stringInput.first.size() == 0) {
            nbInputs--;
            continue;
        }
        preInputs.push_back(std::move(stringInput));
        zlInputs.push_back(
                Input::refString(
                        preInputs[preInputs.size() - 1].first,
                        preInputs[preInputs.size() - 1].second));
    }
    roundtrip(zlInputs);
}

TEST_F(InterleaveTest, SingleInput)
{
    auto dg         = zstrong::tests::datagen::DataGen();
    size_t nbInputs = 1;
    uint32_t nbStrs = dg.u32_range("nbStrs", 100, 200);

    auto sip = StringInputProducer(
            dg.getRandWrapper(), StringInputProducer::Strategy::RoughlyEven);
    std::vector<PreStringInput> preInputs;
    std::vector<Input> zlInputs;
    preInputs.reserve(nbInputs);
    zlInputs.reserve(nbInputs);
    for (size_t i = 0; i < nbInputs; ++i) {
        preInputs.push_back(sip("input", nbStrs));
        // make sure the test doesn't run on empty data
        EXPECT_NE(preInputs[i].first.size(), 0);
        zlInputs.push_back(
                Input::refString(preInputs[i].first, preInputs[i].second));
    }
    roundtrip(zlInputs);
}

TEST_F(InterleaveTest, MultipleDegenerateInputs)
{
    auto dg         = zstrong::tests::datagen::DataGen();
    size_t nbInputs = dg.randVal("nbInputs", 5, 100);
    uint32_t nbStrs = dg.u32_range("nbStrs", 100, 200);

    std::vector<PreStringInput> preInputs;
    std::vector<Input> zlInputs;
    preInputs.reserve(nbInputs);
    zlInputs.reserve(nbInputs);
    for (size_t i = 0; i < nbInputs; ++i) {
        preInputs.emplace_back("", std::vector<uint32_t>(nbStrs, 0));
        zlInputs.push_back(
                Input::refString(preInputs[i].first, preInputs[i].second));
    }
    roundtrip(zlInputs);
}

} // namespace openzl::tests
