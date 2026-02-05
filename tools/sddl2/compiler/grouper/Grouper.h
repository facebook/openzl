// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tools/sddl2/compiler/Logger.h"
#include "tools/sddl2/compiler/grouper/Grouping.h"
#include "tools/sddl2/compiler/tokenizer/Token.h"

namespace openzl::sddl2 {

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

} // namespace openzl::sddl2
