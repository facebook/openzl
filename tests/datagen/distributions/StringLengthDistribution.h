// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tests/datagen/distributions/VecLengthDistribution.h"

namespace openzl::tests::datagen {

constexpr size_t kDefaultMaxStringLength = 4096;

/**
 * @brief A distribution that generates a random length in range [0, max)
 * following roughly the same rules as @ref VecLengthDistribution.
 */
class StringLengthDistribution : public VecLengthDistribution {
   public:
    explicit StringLengthDistribution(
            std::shared_ptr<RandWrapper> generator,
            size_t maxLength = kDefaultMaxStringLength)
            : VecLengthDistribution(generator, 0, maxLength)
    {
    }

    size_t operator()(RandWrapper::NameType name) override
    {
        return this->VecLengthDistribution::operator()(name);
    }

    void print(std::ostream& os) const override
    {
        os << "StringLengthDistribution(";
        this->VecLengthDistribution::print(os);
        os << ")";
    }
};

} // namespace openzl::tests::datagen
