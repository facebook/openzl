// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <unordered_map>

#include "tools/sddl2/compiler/optimizer/DeadVarPass.h"
#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2 {
namespace {

class DeadVarImpl {
   public:
    ASTVec optimize(const ASTVec& ast)
    {
        // Phase 1: collect all variable reads and record the last reference to
        // a variable
        recordLastRefs(ast);

        // Phase 2: optimize the AST
        return optimizeVec(ast);
    }

   private:
    void recordLastRefs(const ASTVec& vec)
    {
        for (const auto& node : vec) {
            recordLastRefs(node);
        }
    }
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
            case ConvertedNodeType::CALL: {
                auto call = node->as_call();
                recordLastRefs(call->target());
                recordLastRefs(call->args());
                return;
            }
            case ConvertedNodeType::RECORD: {
                recordLastRefs(node->as_record()->fields());
                return;
            }
            case ConvertedNodeType::OP: {
                const auto& op = *node->as_op();
                if (op.op() == Op::ASSIGN || op.op() == Op::ASSUME) {
                    recordLastRefs(op.args()[1]);
                    return;
                }
                if (op.op() == Op::MEMBER) {
                    recordLastRefs(op.args()[0]);
                    return;
                }
                recordLastRefs(op.args());
                return;
            }
            case ConvertedNodeType::WHEN: {
                // TODO: implement
                return;
            }
        }
    }

    ASTVec optimizeVec(const ASTVec& vec)
    {
        ASTVec result;
        result.reserve(vec.size());
        for (const auto& node : vec) {
            // It's possible that the node is optimized away.
            auto optimized = optimizeNode(node);
            if (optimized) {
                result.push_back(optimized);
            }
        }
        return result;
    };

    ASTPtr optimizeNode(const ASTPtr& node)
    {
        switch (node->converted_node_type()) {
            case ConvertedNodeType::NUM:
            case ConvertedNodeType::BUILTIN_FIELD:
            case ConvertedNodeType::RECORD:
                return node;
            case ConvertedNodeType::VAR:
                return optimizeVar(node->as_var());
            case ConvertedNodeType::BYTES:
                return optimizeBytes(node->as_bytes());
            case ConvertedNodeType::ARRAY:
                return optimizeArray(node->as_array());
            case ConvertedNodeType::CALL:
                return optimizeCall(node->as_call());
            case ConvertedNodeType::OP:
                return optimizeOp(node->as_op());
            case ConvertedNodeType::WHEN:
                // TODO: implement
                return node;
            default:
                throw InvariantViolation("Unsupported AST node type.");
        }
    }

    ASTPtr optimizeVar(const ASTVar* var)
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

    ASTPtr optimizeBytes(const ASTBytes* bytes)
    {
        return Codegen(bytes->loc()).bytes(optimizeNode(bytes->len()));
    }

    ASTPtr optimizeArray(const ASTArray* arr)
    {
        if (!arr->len()) {
            return Codegen(arr->loc()).array(optimizeNode(arr->field()));
        }
        return Codegen(arr->loc())
                .array(optimizeNode(arr->field()), optimizeNode(arr->len()));
    }

    ASTPtr optimizeCall(const ASTCall* call)
    {
        return Codegen(call->loc())
                .call(optimizeNode(call->target()), optimizeVec(call->args()));
    }

    ASTPtr optimizeOp(const ASTOp* op)
    {
        // If the variable is not referenced, we can remove the assignment
        if (op->op() == Op::ASSIGN) {
            if (!last_ref_.count(op->args()[0]->as_var()->name())) {
                return nullptr;
            }
            return Codegen(op->loc()).op(
                    Op::ASSIGN, op->args()[0], optimizeNode(op->args()[1]));
        }

        if (op->op() == Op::ASSUME) {
            if (!last_ref_.count(op->args()[0]->as_var()->name())) {
                return Codegen(op->loc()).consume(op->args()[1]);
            }
            return Codegen(op->loc()).op(
                    Op::ASSUME, op->args()[0], optimizeNode(op->args()[1]));
        }

        if (op->op() == Op::MEMBER) {
            return Codegen(op->loc()).member(
                    optimizeNode(op->args()[0]), op->args()[1]);
        }

        // Other ops
        return Codegen(op->loc()).op(op->op(), optimizeVec(op->args()));
    }

    // Maps variable name → the last time it is referenced. Variables not in
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
