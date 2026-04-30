// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <random>

#include "tests/datagen/random_producer/RandWrapper.h"
#include "tests/datagen/random_producer/compat_uniform_distribution.h"

namespace openzl::tests::datagen {

class PRNGWrapper : public RandWrapper {
   public:
    // PRNGWrapper() = delete;
    explicit PRNGWrapper(std::shared_ptr<std::mt19937> generator)
            : RandWrapper(RandWrapper::RandType::MT19937), generator_(generator)
    {
    }

    // these are semi-copied from fdp_impl.h
    // Impl note: uniform distribution objects are relatively cheap to create.
    // We cache the "default" ones, but there's no need to cache the ones for
    // range() calls with custom min/max.
    uint8_t u8(NameType) override
    {
        return u8dist_(*generator_);
    }
    uint32_t u32(NameType) override
    {
        return u32dist_(*generator_);
    }

    uint64_t u64(NameType) override
    {
        return u64dist_(*generator_);
    }

    float f32(NameType) override
    {
        return f32dist_(*generator_);
    }

    double f64(NameType) override
    {
        return f64dist_(*generator_);
    }

    size_t usize_range(NameType, size_t min, size_t max) override
    {
        return std::uniform_int_distribution<size_t>(min, max)(*generator_);
    }

    uint8_t u8_range(NameType, uint8_t min, uint8_t max) override
    {
        return compat_uniform_int_distribution<uint8_t>(min, max)(*generator_);
    }

    uint16_t u16_range(NameType, uint16_t min, uint16_t max) override
    {
        return compat_uniform_int_distribution<uint16_t>(min, max)(*generator_);
    }

    uint32_t u32_range(NameType, uint32_t min, uint32_t max) override
    {
        return compat_uniform_int_distribution<uint32_t>(min, max)(*generator_);
    }

    uint64_t u64_range(NameType, uint64_t min, uint64_t max) override
    {
        return compat_uniform_int_distribution<uint64_t>(min, max)(*generator_);
    }

    int8_t i8_range(NameType, int8_t min, int8_t max) override
    {
        return compat_uniform_int_distribution<int8_t>(min, max)(*generator_);
    }

    int16_t i16_range(NameType, int16_t min, int16_t max) override
    {
        return compat_uniform_int_distribution<int16_t>(min, max)(*generator_);
    }

    int32_t i32_range(NameType, int32_t min, int32_t max) override
    {
        return compat_uniform_int_distribution<int32_t>(min, max)(*generator_);
    }

    int64_t i64_range(NameType, int64_t min, int64_t max) override
    {
        return compat_uniform_int_distribution<int64_t>(min, max)(*generator_);
    }

    float f32_range(NameType, float min, float max) override
    {
        return std::uniform_real_distribution<float>(min, max)(*generator_);
    }

    double f64_range(NameType, double min, double max) override
    {
        return std::uniform_real_distribution<double>(min, max)(*generator_);
    }

    bool has_more_data() override
    {
        return true;
    }

    size_t num_remaining_bytes() const override
    {
        throw std::runtime_error(
                "num_remaining_bytes() only available for fuzzers");
    }

    std::vector<uint8_t> all_remaining_bytes() override
    {
        throw std::runtime_error(
                "all_remaining_bytes() only available for fuzzers");
    }

   private:
    std::shared_ptr<std::mt19937> generator_;
    compat_uniform_int_distribution<uint8_t> u8dist_;
    compat_uniform_int_distribution<uint32_t> u32dist_;
    compat_uniform_int_distribution<uint64_t> u64dist_;
    std::uniform_real_distribution<float> f32dist_;
    std::uniform_real_distribution<double> f64dist_;
};

} // namespace openzl::tests::datagen
