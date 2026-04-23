// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stdexcept>

namespace zstrong::thrift {

/// Thrown when the parser encounters an error that should be classified as a
/// user error.
class ThriftParserUserError : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

/// Thrown when the parser encounters malformed input data (e.g. illegal type
/// values that violate the protocol specification).
class ThriftMalformedInputError : public ThriftParserUserError {
   public:
    using ThriftParserUserError::ThriftParserUserError;
};

/// Thrown when the parser encounters valid thrift but unhandled input (e.g. a
/// type that is recognized but not supported by this parser implementation).
class ThriftUnhandledInputError : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

/// Thrown when the configuration is incompatible with the input getting parsed.
class InvalidConfigError : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

} // namespace zstrong::thrift
