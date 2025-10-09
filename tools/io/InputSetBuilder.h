// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "tools/io/InputSet.h"

namespace openzl::tools::io {

/**
 * Helper class to build up an input set from a bunch of path arguments.
 */
class InputSetBuilder {
public:
  explicit InputSetBuilder(bool recursive, bool verbose = false);

  InputSetBuilder &add_path(std::string path) &;
  InputSetBuilder &&add_path(std::string path) &&;

  InputSetBuilder &add_path(std::optional<std::string> path_opt) &;
  InputSetBuilder &&add_path(std::optional<std::string> path_opt) &&;

  std::unique_ptr<InputSet> build() &&;

  std::unique_ptr<InputSet> build_static() &&;

private:
  const bool recursive_;
  const bool verbose_;

  std::vector<std::unique_ptr<InputSet>> input_sets_;
};
} // namespace openzl::tools::io
