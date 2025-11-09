// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tools/sddl/compiler/AST.h"
#include "tools/sddl/compiler/Grouping.h"
#include "tools/sddl/compiler/Logger.h"

namespace openzl::sddl {

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

} // namespace openzl::sddl
