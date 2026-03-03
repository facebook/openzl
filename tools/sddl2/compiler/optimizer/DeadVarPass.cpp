// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <unordered_map>

#include "tools/sddl2/compiler/optimizer/DeadVarPass.h"
#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2 {
namespace {

bool isAssignment(Op op)
{
    return op == Op::ASSIGN || op == Op::ASSUME;
}

class DeadVarImpl {
   public:
    ASTVec optimize(const ASTVec& ast)
    {
        // Phase 1: collect all variable reads and record the last read index
        // for each variable name.
        for (const auto& node : ast) {
            recordLastRefs(node);
        }

        // Phase 2: optimize the AST
        ASTVec result;
        result.reserve(ast.size());
        for (const auto& node : ast) {
            // It's possible that the node is optimized away.
            auto optimized = optimizeNode(node);
            if (optimized) {
                result.push_back(optimized);
            }
        }
        return result;
    }

   private:
    void recordLastRefs(const ASTPtr& node)
    {
        switch (node->converted_node_type()) {
            case ConvertedNodeType::NUM:
            case ConvertedNodeType::BUILTIN_FIELD:
                return;
            case ConvertedNodeType::VAR: {
                auto var               = node->as_var();
                last_ref_[var->name()] = var;
                return;
            }
            case ConvertedNodeType::BYTES: {
                recordLastRefs(node->as_bytes()->len());
                return;
            }
            case ConvertedNodeType::ARRAY: {
                recordLastRefs(node->as_array()->field());
                if (node->as_array()->len()) {
                    recordLastRefs(node->as_array()->len());
                }
                return;
            }
            case ConvertedNodeType::RECORD: {
                for (const auto& field : node->as_record()->fields()) {
                    recordLastRefs(field);
                }
                return;
            }
            case ConvertedNodeType::OP: {
                const auto& op = *node->as_op();
                for (size_t i = 0; i < op.args().size(); ++i) {
                    if (isAssignment(op.op()) && i == 0) {
                        continue;
                    }
                    recordLastRefs(op.args()[i]);
                }
                return;
            }
        }
    }

    ASTPtr optimizeNode(const ASTPtr& node)
    {
        switch (node->converted_node_type()) {
            case ConvertedNodeType::NUM:
            case ConvertedNodeType::BUILTIN_FIELD:
            case ConvertedNodeType::RECORD:
                return node;
            case ConvertedNodeType::VAR:
                return optimize(node->as_var());
            case ConvertedNodeType::BYTES:
                return optimize(node->as_bytes());
            case ConvertedNodeType::ARRAY:
                return optimize(node->as_array());
            case ConvertedNodeType::OP:
                return optimize(node->as_op());
            default:
                throw InvariantViolation("Unsupported AST node type.");
        }
    }

    ASTPtr optimize(const ASTVar* var)
    {
        const auto& last_ref_it = last_ref_.find(var->name());
        if (last_ref_it == last_ref_.end()) {
            throw InvariantViolation("Variable not found: " + var->name());
        }
        const auto& last_ref = last_ref_it->second;
        if (last_ref == var) {
            return Codegen(var->loc()).var(var->name(), true /* is_last */);
        }
        return Codegen(var->loc()).var(var->name());
    }

    ASTPtr optimize(const ASTBytes* bytes)
    {
        return Codegen(bytes->loc()).bytes(optimizeNode(bytes->len()));
    }

    ASTPtr optimize(const ASTArray* arr)
    {
        if (!arr->len()) {
            return Codegen(arr->loc()).array(optimizeNode(arr->field()));
        }
        return Codegen(arr->loc())
                .array(optimizeNode(arr->field()), optimizeNode(arr->len()));
    }

    ASTPtr optimize(const ASTOp* op)
    {
        // If the variable is not referenced, we can remove the assignment
        if (isAssignment(op->op())
            && !last_ref_.count(op->args()[0]->as_var()->name())) {
            if (op->op() == Op::ASSIGN) {
                return nullptr;
            }
            if (op->op() == Op::ASSUME) {
                return Codegen(op->loc()).consume(op->args()[1]);
            }
        }

        ASTVec args;
        for (size_t i = 0; i < op->args().size(); ++i) {
            if (isAssignment(op->op()) && i == 0) {
                args.push_back(op->args()[i]);
                continue;
            }
            args.push_back(optimizeNode(op->args()[i]));
        }
        return Codegen(op->loc()).op(op->op(), std::move(args));
    }

    // Maps variable name → the list time it is referenced. Variables not in
    // this map are never referenced.
    std::unordered_map<std::string, const ASTVar*> last_ref_;
};

} // namespace

DeadVarPass::DeadVarPass(const detail::Logger& logger) : log_(logger) {}

ASTVec DeadVarPass::optimize(const ASTVec& ast) const
{
    log_(1) << "Running DeadVar Optimization Pass ..." << std::endl;
    return DeadVarImpl{}.optimize(ast);
}

} // namespace openzl::sddl2
