// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tests/datagen/distributions/Distribution.h"

namespace openzl::tests::datagen {

template <typename RetType>
class UniformDistribution : public Distribution<RetType> {
   public:
    explicit UniformDistribution(
            std::shared_ptr<RandWrapper> rw,
            RetType min = std::numeric_limits<RetType>::min(),
            RetType max = std::numeric_limits<RetType>::max())
            : Distribution<RetType>(rw), min_(min), max_(max)
    {
    }

    RetType operator()(RandWrapper::NameType) override
    {
        if constexpr (std::is_same_v<RetType, uint8_t>) {
            return this->rw_->range("UniformDistribution:u8", min_, max_);
        }
        if constexpr (std::is_same_v<RetType, uint16_t>) {
            return this->rw_->range("UniformDistribution:u16", min_, max_);
        }
        if constexpr (std::is_same_v<RetType, uint32_t>) {
            return this->rw_->range("UniformDistribution:u32", min_, max_);
        }
        if constexpr (std::is_same_v<RetType, uint64_t>) {
            return this->rw_->range("UniformDistribution:u64", min_, max_);
        }
        if constexpr (std::is_same_v<RetType, int8_t>) {
            return this->rw_->range("UniformDistribution:i8", min_, max_);
        }
        if constexpr (std::is_same_v<RetType, int16_t>) {
            return this->rw_->range("UniformDistribution:i16", min_, max_);
        }
        if constexpr (std::is_same_v<RetType, int32_t>) {
            return this->rw_->range("UniformDistribution:i32", min_, max_);
        }
        if constexpr (std::is_same_v<RetType, int64_t>) {
            return this->rw_->range("UniformDistribution:i64", min_, max_);
        }
        if constexpr (std::is_same_v<RetType, float>) {
            return this->rw_->range("UniformDistribution:f32", min_, max_);
        }
        if constexpr (std::is_same_v<RetType, double>) {
            return this->rw_->range("UniformDistribution:f64", min_, max_);
        }
        throw std::runtime_error("Unsupported type");
    }

    void print(std::ostream& os) const override
    {
        os << "UniformDistribution()";
    }

   private:
    RetType min_;
    RetType max_;
};

} // namespace openzl::tests::datagen
