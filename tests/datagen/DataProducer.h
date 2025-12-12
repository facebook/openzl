// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <ostream>

#include "tests/datagen/random_producer/RandWrapper.h"

namespace openzl::tests::datagen {

/**
 * Base class for all data-generating objects.
 * See subclass @ref Distribution for randomly-generated data.
 */
template <class RetType>
class DataProducer {
   public:
    virtual ~DataProducer()                                = default;
    virtual void print(std::ostream& os) const             = 0;
    virtual RetType operator()(RandWrapper::NameType name) = 0;

   protected:
    DataProducer() = default;
};

template <class RetType>
std::ostream& operator<<(std::ostream& os, const DataProducer<RetType>& dist)
{
    dist.print(os);
    return os;
}

} // namespace openzl::tests::datagen
