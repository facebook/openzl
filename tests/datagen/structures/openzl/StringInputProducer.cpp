// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tests/datagen/structures/openzl/StringInputProducer.h"

namespace openzl::tests::datagen::openzl {

std::ostream& operator<<(
        std::ostream& os,
        const StringInputProducer::Strategy& strategy)
{
    switch (strategy) {
        case StringInputProducer::Strategy::RoughlyEven:
            os << "RoughlyEven";
            break;
        case StringInputProducer::Strategy::SplitBySpace:
            os << "SplitBySpace";
            break;
    }
    return os;
}

PreStringInput StringInputProducer::operator()(
        RandWrapper::NameType name,
        uint32_t numFields)
{
    std::string input = stringProducer_(name);
    std::vector<uint32_t> fieldSizes;
    switch (strategy_) {
        case Strategy::SplitBySpace:
            fieldSizes = genSplitBySpaceSizes(input);
            break;
        case Strategy::RoughlyEven:
            fieldSizes = genSplitRoughlyEvenSizes(input, numFields);
            break;
    }
    // in case the field sizes is empty
    fieldSizes.reserve(1);
    return { std::move(input), std::move(fieldSizes) };
}

std::vector<uint32_t> StringInputProducer::genSplitBySpaceSizes(
        std::string_view input)
{
    if (input.size() == 0) {
        return { 0 };
    }

    std::vector<uint32_t> fieldSizes;
    fieldSizes.reserve(input.size());
    uint32_t fieldSize = 0;
    for (char c : input) {
        if (c != ' ') {
            ++fieldSize;
        } else {
            if (fieldSize > 0) {
                fieldSizes.push_back(fieldSize);
            }
            fieldSizes.push_back(1);
            fieldSize = 0;
        }
    }

    if (fieldSize > 0) {
        fieldSizes.push_back(fieldSize);
    }

    return fieldSizes;
}

std::vector<uint32_t> StringInputProducer::genSplitRoughlyEvenSizes(
        std::string_view input,
        uint32_t numFields)
{
    if (input.size() == 0) {
        return std::vector<uint32_t>(numFields, 0);
    }
    if (numFields == 0) {
        return {};
    }

    std::vector<uint32_t> fieldSizes;
    fieldSizes.reserve(numFields);
    uint32_t idealFieldSize      = (uint32_t)input.size() / numFields;
    int32_t deviationThreshold   = (int32_t)(idealFieldSize * 0.1);
    int32_t accumulatedDeviation = 0;
    for (uint32_t i = 0; i < numFields - 1; ++i) {
        int32_t deviation = rw_->i32_range(
                "StringInputProducer:genSplitRoughlyEvenSizes:deviation",
                -deviationThreshold - accumulatedDeviation,
                deviationThreshold - accumulatedDeviation);
        accumulatedDeviation += deviation;
        fieldSizes.push_back(idealFieldSize + deviation);
    }
    fieldSizes.push_back(idealFieldSize - accumulatedDeviation);

    return fieldSizes;
}

} // namespace openzl::tests::datagen::openzl
