// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cassert>
#include <iostream>
#include <random>

#include "tests/datagen/random_producer/PRNGWrapper.h"
#include "tests/datagen/structures/StringProducer.h"
#include "tests/datagen/structures/VectorProducer.h"

namespace openzl::tests::datagen {

class DataGen {
   public:
    explicit DataGen(uint32_t seed = 0xeb5c0)
            : rw_(std::make_shared<PRNGWrapper>(
                      std::make_shared<std::mt19937>(seed)))
    {
    }

    explicit DataGen(std::shared_ptr<RandWrapper> rw) : rw_(rw) {}

    std::shared_ptr<RandWrapper> getRandWrapper()
    {
        return rw_;
    }

    std::string randString(RandWrapper::NameType name)
    {
        return randStringWithQuantizedLength(name, 1);
    }

    std::string randStringWithQuantizedLength(
            RandWrapper::NameType name,
            size_t quantizationBytes)
    {
        return (StringProducer(rw_))(name, quantizationBytes);
    }

    template <class Res>
    Res randVal(
            RandWrapper::NameType name,
            Res min = std::numeric_limits<Res>::lowest(),
            Res max = std::numeric_limits<Res>::max())
    {
        static_assert(
                std::is_arithmetic<Res>::value,
                "randVal requires an integral or float type");
        return UniformDistribution<Res>(rw_, min, max)(name);
    }

    template <class Res>
    std::vector<Res>
    randVector(RandWrapper::NameType name, Res min, Res max, size_t maxLength)
    {
        static_assert(
                std::is_arithmetic<Res>::value,
                "randVector requires an integral or float type");
        return (*VectorProducer<Res>::standardUniform(
                rw_, min, max, maxLength))(name);
    }

    template <class Res>
    std::vector<Res> randLongVector(
            RandWrapper::NameType name,
            Res min,
            Res max,
            size_t minLength,
            size_t maxLength)
    {
        static_assert(
                std::is_arithmetic<Res>::value,
                "randVector requires an integral or float type");
        return VectorProducer<Res>(
                std::make_unique<UniformDistribution<Res>>(rw_, min, max),
                std::make_unique<VecLengthDistribution>(
                        rw_, minLength, maxLength))(name);
    }

    template <class Res>
    std::vector<std::vector<Res>> randVectorVector(
            RandWrapper::NameType name,
            Res min,
            Res max,
            size_t outerMaxLength,
            size_t innerMaxLength)
    {
        static_assert(
                std::is_arithmetic<Res>::value,
                "randVectorVector requires an integral or float type");
        auto valueDist = VectorProducer<Res>::standardUniform(
                rw_, min, max, innerMaxLength);
        auto dist = VectorProducer<std::vector<Res>>(
                std::move(valueDist),
                std::make_unique<VecLengthDistribution>(
                        rw_, 0, outerMaxLength));
        std::cout << dist << std::endl;
        return dist(name);
    }

    template <class Res>
    Res choices(RandWrapper::NameType name, const std::vector<Res>& vec)
    {
        return vec.at(usize_range(name, 0, vec.size() - 1));
    }

    // Convenience functions for common types. Range limits are inclusive.
    bool boolean(RandWrapper::NameType name)
    {
        return u8(name) & 1;
    }
    bool coin(RandWrapper::NameType name, float p = 0.5)
    {
        return f32_range(name, 0.0, 1.0) < p;
    }
    uint8_t u8(RandWrapper::NameType name)
    {
        return UniformDistribution<uint8_t>(rw_)(name);
    }
    uint32_t u32(RandWrapper::NameType name)
    {
        return UniformDistribution<uint32_t>(rw_)(name);
    }
    uint64_t u64(RandWrapper::NameType name)
    {
        return UniformDistribution<uint64_t>(rw_)(name);
    }
    float f32(RandWrapper::NameType name)
    {
        return UniformDistribution<float>(rw_)(name);
    }
    double f64(RandWrapper::NameType name)
    {
        return UniformDistribution<double>(rw_)(name);
    }
    size_t usize_range(RandWrapper::NameType name, size_t min, size_t max)
    {
        return UniformDistribution<size_t>(rw_, min, max)(name);
    }
    uint8_t u8_range(RandWrapper::NameType name, uint8_t min, uint8_t max)
    {
        return UniformDistribution<uint8_t>(rw_, min, max)(name);
    }
    uint16_t u16_range(RandWrapper::NameType name, uint16_t min, uint16_t max)
    {
        return UniformDistribution<uint16_t>(rw_, min, max)(name);
    }
    uint32_t u32_range(RandWrapper::NameType name, uint32_t min, uint32_t max)
    {
        return UniformDistribution<uint32_t>(rw_, min, max)(name);
    }
    uint64_t u64_range(RandWrapper::NameType name, uint64_t min, uint64_t max)
    {
        return UniformDistribution<uint64_t>(rw_, min, max)(name);
    }
    int8_t i8_range(RandWrapper::NameType name, int8_t min, int8_t max)
    {
        return UniformDistribution<int8_t>(rw_, min, max)(name);
    }
    int16_t i16_range(RandWrapper::NameType name, int16_t min, int16_t max)
    {
        return UniformDistribution<int16_t>(rw_, min, max)(name);
    }
    int32_t i32_range(RandWrapper::NameType name, int32_t min, int32_t max)
    {
        return UniformDistribution<int32_t>(rw_, min, max)(name);
    }
    int64_t i64_range(RandWrapper::NameType name, int64_t min, int64_t max)
    {
        return UniformDistribution<int64_t>(rw_, min, max)(name);
    }
    float f32_range(RandWrapper::NameType name, float min, float max)
    {
        return UniformDistribution<float>(rw_, min, max)(name);
    }
    double f64_range(RandWrapper::NameType name, double min, double max)
    {
        return UniformDistribution<double>(rw_, min, max)(name);
    }

    // convenience functions for fuzzer inputs
    bool has_more_data()
    {
        return rw_->has_more_data();
    }

    std::vector<uint8_t> all_remaining_bytes()
    {
        return rw_->all_remaining_bytes();
    }

   private:
    std::shared_ptr<RandWrapper> rw_;
};

} // namespace openzl::tests::datagen
