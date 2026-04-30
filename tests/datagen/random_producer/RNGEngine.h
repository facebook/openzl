// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include "tests/datagen/random_producer/RandWrapper.h"

namespace openzl::tests::datagen {

/**
 * A simple RNG interface around RandWrapper that implements the same methods as
 * STL engines like linear_congruential_engine and mt19937. Useful for passing
 * into STL distributions and other places that expect MT19937 or similar.
 */
template <typename T>
class RNGEngine {
   public:
    using result_type = T;

    explicit RNGEngine(
            RandWrapper* rw,
            RandWrapper::NameType name = "RNGEngine:operator()")
            : rw_(rw), name_(name)
    {
        static_assert(
                std::is_same_v<result_type, uint32_t>
                || std::is_same_v<result_type, uint64_t>);
    }

    result_type operator()()
    {
        if constexpr (std::is_same_v<result_type, uint32_t>) {
            return rw_->u32(name_);
        }
        if constexpr (std::is_same_v<result_type, uint64_t>) {
            return rw_->u64(name_);
        }
    }
    static constexpr result_type min()
    {
        return std::numeric_limits<result_type>::min();
    }
    static constexpr result_type max()
    {
        return std::numeric_limits<result_type>::max();
    }

   private:
    RandWrapper* rw_;
    RandWrapper::NameType name_;
};

} // namespace openzl::tests::datagen
