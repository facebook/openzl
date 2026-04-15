// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tests/datagen/distributions/Distribution.h"

#include <cmath>

namespace openzl::tests::datagen {

/// Used to generate values that look like offsets in LZ, which typically follow
/// a log-uniform distribution.
/// See https://en.wikipedia.org/wiki/Reciprocal_distribution
template <typename RetType>
class LogUniformDistribution : public Distribution<RetType> {
   public:
    static_assert(std::is_integral<RetType>::value);
    static_assert(std::is_unsigned<RetType>::value);

    explicit LogUniformDistribution(
            std::shared_ptr<RandWrapper> rw,
            RetType min = 1,
            RetType max = std::numeric_limits<RetType>::max())
            : Distribution<RetType>(std::move(rw)),
              min_(min),
              max_(max),
              logMin_(std::log(min_)),
              logAMinusB_(std::log(max_) - std::log(min_))
    {
    }

    RetType operator()(RandWrapper::NameType) override
    {
        return (RetType)std::round(
                std::exp(
                        logMin_
                        + this->rw_->f32_range("float", 0, 1) * logAMinusB_));
    }

    void print(std::ostream& os) const override
    {
        os << "LogUniformDistribution(" << min_ << "," << max_ << ")";
    }

   private:
    RetType min_;
    RetType max_;
    float logMin_;
    float logAMinusB_;
};

} // namespace openzl::tests::datagen
