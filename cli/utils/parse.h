// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stdexcept>
#include <string>

namespace openzl::cli {

/**
 * Exception thrown when an invalid argument is provided.
 */
class InvalidArgsException : public std::runtime_error {
   public:
    explicit InvalidArgsException(const std::string& msg)
            : std::runtime_error(msg)
    {
    }
};

/**
 * A general uncategorized exception thrown when the CLI is used incorrectly.
 */
class CLIException : public std::runtime_error {
   public:
    explicit CLIException(const std::string& msg) : std::runtime_error(msg) {}
};

namespace util {

/**
 * Print a human-readable size string.
 * E.g. 1024 -> "1.02 KB"
 */
std::string sizeString(size_t sz);

/**
 * Convert a string to an integer. Supports trailing size suffixes (e.g. "1K" ->
 * 1000). Checks that the value is well-formed and any suffixes are valid.
 * @throws InvalidArgsException if the string is invalid, has an unknown suffix,
 * or the result overflows the return type.
 */
int checkedstoi(const std::string& str);
long checkedstol(const std::string& str);
unsigned long checkedstoul(const std::string& str);
long long checkedstoll(const std::string& str);
unsigned long long checkedstoull(const std::string& str);

} // namespace util
} // namespace openzl::cli
