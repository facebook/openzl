// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace openzl::training {

class SerializedCompressorInternal {
   public:
    explicit SerializedCompressorInternal(std::string&& str);
    ~SerializedCompressorInternal() = default;

    SerializedCompressorInternal(const SerializedCompressorInternal& other) =
            delete;
    SerializedCompressorInternal& operator=(
            const SerializedCompressorInternal& other) = delete;

    SerializedCompressorInternal(
            SerializedCompressorInternal&& other) noexcept = default;
    SerializedCompressorInternal& operator=(
            SerializedCompressorInternal&& other) noexcept = default;

    std::string_view operator*() const;

   private:
    std::string storage_;
};

} // namespace openzl::training
