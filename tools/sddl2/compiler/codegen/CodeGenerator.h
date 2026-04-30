// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>

#include "tools/sddl2/compiler/Logger.h"
#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2 {
class CodeGenerator {
   public:
    explicit CodeGenerator(const detail::Logger& logger);

    std::string generate(const ASTVec& ast) const;

   private:
    const detail::Logger& log_;
};

} // namespace openzl::sddl2
