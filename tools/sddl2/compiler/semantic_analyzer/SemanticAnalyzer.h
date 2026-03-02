// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tools/sddl2/compiler/Logger.h"
#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2 {
class SemanticAnalyzer {
   public:
    explicit SemanticAnalyzer(const detail::Logger& logger);

    void analyze(const ASTVec& ast) const;

   private:
    const detail::Logger& log_;
};

} // namespace openzl::sddl2
