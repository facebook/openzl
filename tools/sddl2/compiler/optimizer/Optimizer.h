// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <vector>

#include "tools/sddl2/compiler/Logger.h"
#include "tools/sddl2/compiler/optimizer/OptimizationPass.h"
#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2 {

class Optimizer {
   public:
    explicit Optimizer(const detail::Logger& logger);

    ASTVec optimize(const ASTVec& ast) const;

   private:
    std::vector<std::unique_ptr<const OptimizationPass>> passes_;
};

} // namespace openzl::sddl2
