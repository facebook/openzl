// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <stdexcept>
#include <string>

namespace openzl::tools::io {

/**
 * Exception thrown when an I/O operation fails.
 */
class IOException : public std::runtime_error {
   public:
    explicit IOException(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace openzl::tools::io
