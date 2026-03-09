// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <unordered_map>

#include "tools/sddl2/compiler/optimizer/ConstFoldPass.h"
#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2 {
namespace {

ASTPtr makeNum(const SourceLocation& loc, int64_t val)
{
    return std::make_shared<ASTNum>(Token{ loc, val });
}

class ConstFoldImpl {
   public:
    explicit ConstFoldImpl(const detail::Logger& logger) : log_(logger) {}

    ASTVec optimize(const ASTVec& ast)
    {
        (void)log_;
        ASTVec result;
        result.reserve(ast.size());
        for (const auto& node : ast) {
            result.push_back(optimizeNode(node));
        }
        return result;
    }

   private:
    ASTPtr optimizeNode(const ASTPtr& node)
    {
        switch (node->converted_node_type()) {
            case ConvertedNodeType::NUM:
            case ConvertedNodeType::BUILTIN_FIELD:
                return node;
            case ConvertedNodeType::VAR:
                return optimize(*node->as_var());
            case ConvertedNodeType::BYTES:
                return optimize(*node->as_bytes());
            case ConvertedNodeType::ARRAY:
                return optimize(*node->as_array());
            case ConvertedNodeType::RECORD:
                return optimize(*node->as_record());
            case ConvertedNodeType::OP:
                return optimize(*node->as_op());
            default:
                throw InvariantViolation("Unsupported AST node type.");
        }
    }

    ASTPtr optimize(const ASTVar& var)
    {
        auto it = const_vars_.find(var.name());
        if (it != const_vars_.end()) {
            return makeNum(var.loc(), it->second);
        }
        return std::make_shared<ASTVar>(var);
    }

    ASTPtr optimize(const ASTBytes& bytes)
    {
        return Codegen(bytes.loc()).bytes(optimizeNode(bytes.len()));
    }

    ASTPtr optimize(const ASTArray& array)
    {
        if (!array.len()) {
            return Codegen(array.loc()).array(optimizeNode(array.field()));
        }
        return Codegen(array.loc())
                .array(optimizeNode(array.field()), optimizeNode(array.len()));
    }

    ASTPtr optimize(const ASTRecord& record)
    {
        auto saved_vars = const_vars_;
        ASTVec new_fields;
        new_fields.reserve(record.fields().size());
        for (const auto& field : record.fields()) {
            new_fields.push_back(optimizeNode(field));
        }
        const_vars_ = std::move(saved_vars);
        return Codegen(record.loc())
                .record(record.params(), std::move(new_fields));
    }

    ASTPtr optimize(const ASTOp& op)
    {
        ASTVec new_args;

        bool all_const = true;
        for (size_t i = 0; i < op.args().size(); ++i) {
            if ((op.op() == Op::ASSIGN || op.op() == Op::ASSUME) && i == 0) {
                new_args.push_back(op.args()[i]);
                continue;
            }
            auto optimized = optimizeNode(op.args()[i]);
            all_const      = all_const && optimized->as_num();
            new_args.push_back(std::move(optimized));
        }

        if (!all_const) {
            return std::make_shared<ASTOp>(
                    op.loc(), op.op(), std::move(new_args));
        }

        const auto& loc = op.loc();
        auto num_at     = [&](size_t idx) {
            return new_args.at(idx)->as_num()->val();
        };

        switch (op.op()) {
            case Op::NEG:
                return makeNum(loc, -num_at(0));
            case Op::LOG_NOT:
                return makeNum(loc, !num_at(0) ? 1 : 0);
            case Op::BIT_NOT:
                return makeNum(loc, ~num_at(0));
            case Op::ADD:
                return makeNum(loc, num_at(0) + num_at(1));
            case Op::SUB:
                return makeNum(loc, num_at(0) - num_at(1));
            case Op::MUL:
                return makeNum(loc, num_at(0) * num_at(1));
            case Op::DIV: {
                auto divisor = num_at(1);
                if (divisor == 0) {
                    throw SemanticError(op.loc(), "Division by zero!");
                }
                return makeNum(loc, num_at(0) / divisor);
            }
            case Op::MOD: {
                auto divisor = num_at(1);
                if (divisor == 0) {
                    throw SemanticError(op.loc(), "Modulo by zero!");
                }
                return makeNum(loc, num_at(0) % divisor);
            }
            case Op::EQ:
                return makeNum(loc, num_at(0) == num_at(1) ? 1 : 0);
            case Op::NE:
                return makeNum(loc, num_at(0) != num_at(1) ? 1 : 0);
            case Op::GT:
                return makeNum(loc, num_at(0) > num_at(1) ? 1 : 0);
            case Op::GE:
                return makeNum(loc, num_at(0) >= num_at(1) ? 1 : 0);
            case Op::LT:
                return makeNum(loc, num_at(0) < num_at(1) ? 1 : 0);
            case Op::LE:
                return makeNum(loc, num_at(0) <= num_at(1) ? 1 : 0);
            case Op::BIT_AND:
                return makeNum(loc, num_at(0) & num_at(1));
            case Op::BIT_OR:
                return makeNum(loc, num_at(0) | num_at(1));
            case Op::BIT_XOR:
                return makeNum(loc, num_at(0) ^ num_at(1));
            case Op::LOG_AND:
                return makeNum(loc, (num_at(0) && num_at(1)) ? 1 : 0);
            case Op::LOG_OR:
                return makeNum(loc, (num_at(0) || num_at(1)) ? 1 : 0);
            case Op::ASSIGN:
                const_vars_[op.args()[0]->as_var()->name()] = num_at(1);
                [[fallthrough]];
            case Op::EXPECT:
            case Op::CONSUME:
            case Op::ASSUME:
            case Op::SIZEOF:
            case Op::MEMBER:
            case Op::SEND:
            default:
                return std::make_shared<ASTOp>(
                        op.loc(), op.op(), std::move(new_args));
        }
    }

    const detail::Logger& log_;
    std::unordered_map<std::string, int64_t> const_vars_;
};

} // namespace

ConstFoldPass::ConstFoldPass(const detail::Logger& logger) : log_(logger) {}

ASTVec ConstFoldPass::optimize(const ASTVec& ast) const
{
    return ConstFoldImpl{ log_ }.optimize(ast);
}

} // namespace openzl::sddl2
