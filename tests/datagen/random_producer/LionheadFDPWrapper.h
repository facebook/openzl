// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <limits>
#include <random>
#include <vector>

#include "tests/datagen/random_producer/RNGEngine.h"
#include "tests/datagen/random_producer/RandWrapper.h"

namespace zstrong::tests::datagen {

// A non-templated wrapper around StructuredFDP that provides a consistent
// interface for downstream distributions to use.
template <class FDP>
class LionheadFDPWrapper : public RandWrapper {
   public:
    explicit LionheadFDPWrapper(FDP& fdp)
            : RandWrapper(RandWrapper::RandType::StructuredFDP), fdp_(&fdp)
    {
    }

    // these are just copied from fdp_impl.h
    bool should_continue()
    {
        return fdp_->should_continue();
    }
    bool has_more_data() override
    {
        return fdp_->has_more_data();
    }
    size_t remaining_input_length()
    {
        return fdp_->remaining_input_length();
    }
    std::vector<uint8_t> all_remaining_bytes() override
    {
        return fdp_->all_remaining_bytes();
    }

    // these are semi-copied from fdp_impl.h
    uint8_t u8(NameType name) override
    {
        return fdp_->u8(name);
    }
    uint32_t u32(NameType name) override
    {
        return fdp_->u32(name);
    }
    uint64_t u64(NameType name) override
    {
        return fdp_->u64(name);
    }
    float f32(NameType name) override
    {
        return fdp_->f32(name);
    }
    double f64(NameType name) override
    {
        return fdp_->f64(name);
    }
    size_t usize_range(NameType name, size_t min, size_t max) override
    {
        return fdp_->usize_range(name, min, max);
    }
    uint8_t u8_range(NameType name, uint8_t min, uint8_t max) override
    {
        return fdp_->u8_range(name, min, max);
    }
    uint16_t u16_range(NameType name, uint16_t min, uint16_t max) override
    {
        return fdp_->u16_range(name, min, max);
    }
    uint32_t u32_range(NameType name, uint32_t min, uint32_t max) override
    {
        return fdp_->u32_range(name, min, max);
    }
    uint64_t u64_range(NameType name, uint64_t min, uint64_t max) override
    {
        return fdp_->u64_range(name, min, max);
    }
    int8_t i8_range(NameType name, int8_t min, int8_t max) override
    {
        return fdp_->i8_range(name, min, max);
    }
    int16_t i16_range(NameType name, int16_t min, int16_t max) override
    {
        return fdp_->i16_range(name, min, max);
    }
    int32_t i32_range(NameType name, int32_t min, int32_t max) override
    {
        return fdp_->i32_range(name, min, max);
    }
    int64_t i64_range(NameType name, int64_t min, int64_t max) override
    {
        return fdp_->i64_range(name, min, max);
    }
    float f32_range(NameType name, float min, float max) override
    {
        // lionhead doesn't support floating point ranges yet, so we do
        // something cursed. This is guaranteed to be correct since the standard
        // assures that generating a 32-bit float only requires one call to
        // operator() from a 32-bit engine.
        auto rng = RNGEngine<uint32_t>(this, name);
        return (max - min)
                * std::generate_canonical<
                        float,
                        std::numeric_limits<float>::digits>(rng)
                + min;
    }
    double f64_range(NameType name, double min, double max) override
    {
        auto rng = RNGEngine<uint64_t>(this, name);
        return (max - min)
                * std::generate_canonical<
                        double,
                        std::numeric_limits<double>::digits>(rng)
                + min;
    }

   private:
    FDP* fdp_;
};

} // namespace zstrong::tests::datagen
