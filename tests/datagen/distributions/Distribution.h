// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>

#include "tests/datagen/DataProducer.h"
#include "tests/datagen/random_producer/RandWrapper.h"

namespace openzl::tests::datagen {

/**
 * @brief The base class for all random distributions that uses a random source
 * to power the distribution sampling.
 */
template <typename RetType>
class Distribution : public DataProducer<RetType> {
   public:
    explicit Distribution(std::shared_ptr<RandWrapper> rw) : rw_(rw) {}

   protected:
    std::shared_ptr<RandWrapper> rw_;
};

} // namespace openzl::tests::datagen
