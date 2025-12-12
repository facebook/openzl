// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tests/datagen/distributions/Distribution.h"

namespace openzl::tests::datagen {

/**
 * @brief Distribution for generating vector lengths, skewed towards shorter
 * lengths. Distribution math cribbed from fuzz_utils.h
 * NB: Min vector length is inclusive, max is exclusive.
 */
class VecLengthDistribution : public Distribution<size_t> {
   public:
    explicit VecLengthDistribution(
            std::shared_ptr<RandWrapper> generator,
            size_t min,
            size_t max = kMaxVecLength)
            : Distribution<size_t>(generator), min_(min), max_(max)
    {
        if (max > kMaxVecLength) {
            throw std::runtime_error("VecLengthDistribution: max is too large");
        }
        if (min > max) {
            throw std::runtime_error("VecLengthDistribution: min > max");
        }
    }

    // TODO(csv): combine with fuzzer utils version
    size_t operator()(RandWrapper::NameType) override
    {
        uint8_t op     = this->rw_->u8("VecLengthDistribution:op");
        size_t len_val = this->rw_->u32("VecLengthDistribution:len_val");
        if (max_ == 0) {
            return 0;
        }
        // 128 / 256 = 50%
        if (op < 0b10000000) {
            return std::max(len_val % std::min(size_t(16), max_), min_);
        }
        // 64 / 256 = 25%
        if (op < 0b11000000) {
            return std::max(len_val % std::min(size_t(256), max_), min_);
        }
        // 48 / 256 = 18.75%
        if (op < 0b11110000) {
            return std::max(len_val % std::min(size_t(1024), max_), min_);
        }
        // 15 / 256 ~= 5.85%
        if (op < 0b11111111) {
            return std::max(len_val % std::min(size_t(4096), max_), min_);
        }
        return std::max(len_val % max_, min_);
    }

    void print(std::ostream& os) const override
    {
        os << "VecLengthDistribution(" << max_ << ")";
    }

   private:
    static constexpr size_t kMaxVecLength = (size_t)1u << 17;

    const size_t min_;
    const size_t max_;
};

} // namespace openzl::tests::datagen
