// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl2/compiler/optimizer/Optimizer.h"
#include "tools/sddl2/compiler/optimizer/ConstFoldPass.h"

namespace openzl::sddl2 {

Optimizer::Optimizer(const detail::Logger& logger)
{
    passes_.push_back(std::make_unique<ConstFoldPass>(logger));
}

ASTVec Optimizer::optimize(const ASTVec& ast) const
{
    auto result = ast;
    for (const auto& pass : passes_) {
        result = pass->optimize(result);
    }
    return result;
}

} // namespace openzl::sddl2
