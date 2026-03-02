// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2 {

class OptimizationPass {
   public:
    virtual ~OptimizationPass() = default;

    virtual ASTVec optimize(const ASTVec& ast) const = 0;
};

} // namespace openzl::sddl2
