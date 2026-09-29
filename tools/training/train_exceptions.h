// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/cpp/Exception.hpp"

namespace openzl::training {

/** Thrown when a compressor has no graph that can be trained. */
class NoTrainableGraphError : public Exception {
   public:
    using Exception::Exception;
};

/**
 * Thrown by a trainer when it cannot produce a graph that supports the target
 * format version (e.g. all of its required codecs are below the target
 * version). The orchestrator catches this to fall the trainer back to zstd.
 */
class FormatVersionUnsupportedError : public Exception {
   public:
    using Exception::Exception;
};

} // namespace openzl::training
