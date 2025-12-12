// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tests/datagen/distributions/Distribution.h"

namespace openzl::tests::datagen {

template <typename RetType>
class ConstantDistribution : public Distribution<RetType> {
   public:
    explicit ConstantDistribution(RetType value) : value_(value) {}

    RetType operator()(RandWrapper::NameType) override
    {
        return value_;
    }

    void print(std::ostream& os) const override
    {
        os << "ConstantDistribution(" << value_ << ")";
    }

   private:
    const RetType value_;
};

} // namespace openzl::tests::datagen
