// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>

#include "tools/sddl/compiler/Grouping.h"
#include "tools/sddl/compiler/Logger.h"
#include "tools/sddl/compiler/Token.h"

namespace openzl::sddl {

/**
 * Takes a flat array of tokens and groups it into nested expressions and lists
 * by parsing the framing characters / separators.
 */
class Grouper {
   public:
    explicit Grouper(const detail::Logger& logger);

    GroupingVec group(const std::vector<Token>& tokens) const;

   private:
    const detail::Logger& log_;
};

} // namespace openzl::sddl
