// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <vector>

#include "tests/datagen/DataProducer.h"
#include "tests/datagen/structures/StringProducer.h"

namespace openzl::tests::datagen::openzl {

// Generator for string openzl::Input. Since Input is non-owning, we return a
// pair that can then be ref-ed by Input.
using PreStringInput = std::pair<std::string, std::vector<uint32_t>>;

class StringInputProducer : public DataProducer<PreStringInput> {
   public:
    enum class Strategy {
        SplitBySpace,
        RoughlyEven,
    };
    friend std::ostream& operator<<(
            std::ostream& os,
            const StringInputProducer::Strategy& strategy);

    explicit StringInputProducer(
            std::shared_ptr<RandWrapper> generator,
            Strategy strategy)
            : DataProducer<PreStringInput>(),
              rw_(generator),
              stringProducer_(generator),
              strategy_(strategy)
    {
    }

    PreStringInput operator()(RandWrapper::NameType name) override
    {
        return operator()(name, 0);
    }

    PreStringInput operator()(RandWrapper::NameType name, uint32_t numFields);

    void print(std::ostream& os) const override
    {
        os << "StringInputProducer(" << stringProducer_ << ", " << strategy_
           << ")";
    }

   private:
    /**
     * Split the input string by space, with a size 1 for each space between
     * strings.
     */
    std::vector<uint32_t> genSplitBySpaceSizes(std::string_view input);

    /**
     * Split the input string into roughly even sized fields, with guaranteed
     * deviation no more than 10% from ideal in aggregate and 20% per field
     */
    std::vector<uint32_t> genSplitRoughlyEvenSizes(
            std::string_view input,
            uint32_t numFields);

    std::shared_ptr<RandWrapper> rw_;
    StringProducer stringProducer_;
    Strategy strategy_;
};

} // namespace openzl::tests::datagen::openzl
