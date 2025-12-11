// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <random>
#include <string>

namespace openzl::tests::datagen {

class InputExpander {
   public:
    static std::string expandSerial(const std::string& src, size_t targetSize);
    static std::string expandSerialWithMutation(
            const std::string& src,
            size_t targetSize,
            std::shared_ptr<std::mt19937> generator = {});
    static std::pair<std::string, std::vector<uint32_t>>
    expandStringWithMutation(
            const std::string& src,
            const std::vector<uint32_t>& segmentSizes,
            size_t targetSize,
            std::shared_ptr<std::mt19937> generator = {});

   private:
    // static functions only
    InputExpander() = default;
};

} // namespace openzl::tests::datagen
