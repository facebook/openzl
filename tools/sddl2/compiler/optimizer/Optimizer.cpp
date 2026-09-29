// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl2/compiler/optimizer/Optimizer.h"
#include "tools/sddl2/compiler/optimizer/ConstFoldPass.h"
#include "tools/sddl2/compiler/optimizer/DeadVarPass.h"

namespace openzl::sddl2 {

Optimizer::Optimizer(const detail::Logger& logger) : log_(logger)
{
    passes_.push_back(std::make_unique<ConstFoldPass>(logger));
    passes_.push_back(std::make_unique<DeadVarPass>(logger));
}

ASTVec Optimizer::optimize(const ASTVec& ast) const
{
    auto result = ast;
    for (const auto& pass : passes_) {
        result = pass->optimize(result);
    }

    auto& log = log_(1);
    log << "Optimized AST:" << std::endl;
    for (const auto& node : result) {
        node->print(log, 2);
    }
    log << std::endl;

    return result;
}

} // namespace openzl::sddl2
