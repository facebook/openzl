// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <random>
#include <type_traits>
#include <vector>

#include "tests/datagen/DataProducer.h"
#include "tests/datagen/random_producer/RandWrapper.h"

namespace openzl::tests::datagen {

struct FixedWidthData {
    template <typename T>
    explicit FixedWidthData(const std::vector<T>& vec)
            : data((const char*)vec.data(), vec.size() * sizeof(T)),
              width(sizeof(T))
    {
        static_assert(std::is_integral_v<T>);
    }

    FixedWidthData(std::string data_, size_t width_)
            : data(std::move(data_)), width(width_)
    {
    }

    std::string data;
    size_t width;
};

class FixedWidthDataProducer : public DataProducer<FixedWidthData> {
   public:
    explicit FixedWidthDataProducer(
            std::shared_ptr<RandWrapper> rw,
            size_t /* eltWidth */)
            : DataProducer(), rw_(rw)
    {
    }

    virtual ~FixedWidthDataProducer() = default;

   protected:
    std::shared_ptr<RandWrapper> rw_;
};

} // namespace openzl::tests::datagen
