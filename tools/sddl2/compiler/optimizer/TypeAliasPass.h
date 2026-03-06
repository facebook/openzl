// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tools/sddl2/compiler/Logger.h"
#include "tools/sddl2/compiler/optimizer/OptimizationPass.h"

namespace openzl::sddl2 {

class TypeAliasPass : public OptimizationPass {
   public:
    explicit TypeAliasPass(const detail::Logger& logger);

    ASTVec optimize(const ASTVec& ast) const override;

   private:
    const detail::Logger& log_;
};

} // namespace openzl::sddl2
