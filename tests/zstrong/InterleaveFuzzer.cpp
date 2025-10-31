// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tests/datagen/DataGen.h"
#include "tests/datagen/structures/openzl/StringInputProducer.h"
#include "tests/fuzz_utils.h"

#include "tests/zstrong/InterleaveTestFixture.h"

namespace openzl::tests {

using zstrong::tests::datagen::openzl::PreStringInput;
using zstrong::tests::datagen::openzl::StringInputProducer;

FUZZ_F(InterleaveTest, FuzzInterleaveRoundTrip)
{
    zstrong::tests::datagen::DataGen dg = zstrong::tests::fromFDP(f);
    // At least one is required, library version 20 can only support 2048 inputs
    uint16_t nbInputs = dg.u16_range("nbInputs", 0, 2048);
    // Flip a coin to decide if we should test with equal-sized inputs or not.
    // Inputs without equal-sized inputs are automatically invalid, but still
    // should not crash the program.
    bool equalSizedInputs = dg.coin("coin");

    std::vector<PreStringInput> preInputs;
    std::vector<Input> zlInputs;
    preInputs.reserve(nbInputs);
    zlInputs.reserve(nbInputs);

    if (equalSizedInputs) {
        uint32_t nbStrs = dg.u32_range("nbStrs", 0, UINT16_MAX);
        auto sip        = StringInputProducer(
                dg.getRandWrapper(),
                StringInputProducer::Strategy::RoughlyEven);
        for (size_t i = 0; i < nbInputs; ++i) {
            preInputs.push_back(sip("input", nbStrs));
            zlInputs.push_back(
                    Input::refString(preInputs[i].first, preInputs[i].second));
        }
    } else {
        auto sip = StringInputProducer(
                dg.getRandWrapper(),
                StringInputProducer::Strategy::SplitBySpace);
        for (size_t i = 0; i < nbInputs; ++i) {
            preInputs.push_back(sip("input"));
            zlInputs.push_back(
                    Input::refString(preInputs[i].first, preInputs[i].second));
        }
    }

    const auto isInputValid = [](const std::vector<Input>& inputs) {
        if (inputs.size() == 0 || inputs.size() > 256) {
            return false;
        }
        auto numStrings = inputs[0].numElts();
        if (numStrings == 0) {
            return false;
        }
        for (size_t i = 1; i < inputs.size(); ++i) {
            if (inputs[i].numElts() != numStrings) {
                return false;
            }
        }
        return true;
    };

    if (isInputValid(zlInputs)) {
        roundtrip(zlInputs);
    } else {
        roundtripCompressionMayFail(zlInputs);
    }
}

} // namespace openzl::tests
