// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>

#include "tests/datagen/DataProducer.h"

namespace openzl::tests::datagen {

/**
 * Base class for hardcoded pre-prepared inputs.
 */
class RegistryRecord : public DataProducer<std::string> {
   public:
    explicit RegistryRecord() = default;

    virtual size_t size() const = 0;
};

} // namespace openzl::tests::datagen
