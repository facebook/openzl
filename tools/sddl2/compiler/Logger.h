// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <ostream>

namespace openzl::sddl::detail {

/**
 * Helper class for logging which wraps a `std::ostream` and allows for
 * conditionally recording messages.
 */
class Logger {
   public:
    explicit Logger(std::ostream& os, int verbosity = 0);

    std::ostream& operator()(int level) const;

   private:
    std::ostream& os_;

    /**
     * No-op ostream that's returned when the log level check wants to suppress
     * the output.
     */
    std::ostream& null_;

    const int verbosity_;
};

} // namespace openzl::sddl::detail
