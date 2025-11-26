// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tools/sddl2/compiler/AST.h"
#include "tools/sddl2/compiler/Grouping.h"
#include "tools/sddl2/compiler/Logger.h"

namespace openzl::sddl2 {

/**
 * Takes a flat array of tokens and transforms it into an AST.
 */
class Parser {
   public:
    explicit Parser(const detail::Logger& logger);

    ASTVec parse(const GroupingVec& groups) const;

   private:
    const detail::Logger& log_;
};

} // namespace openzl::sddl2
