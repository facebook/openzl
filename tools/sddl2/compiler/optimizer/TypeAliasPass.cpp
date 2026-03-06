// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <unordered_map>

#include "tools/sddl2/compiler/optimizer/TypeAliasPass.h"
#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2 {
namespace {

bool isFieldType(const ASTPtr& node)
{
    return node->as_builtin_field() || node->as_bytes() || node->as_array()
            || node->as_record();
}

class TypeAliasImpl {
   public:
    explicit TypeAliasImpl(const detail::Logger& logger) : log_(logger) {}

    ASTVec optimize(const ASTVec& ast)
    {
        log_(1) << "Performing type alias resolution..." << std::endl;
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
        auto it = type_aliases_.find(var.name());
        if (it != type_aliases_.end()) {
            return it->second;
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
        auto saved_aliases = type_aliases_;
        ASTVec new_fields;
        new_fields.reserve(record.fields().size());
        for (const auto& field : record.fields()) {
            new_fields.push_back(optimizeNode(field));
        }
        type_aliases_ = std::move(saved_aliases);
        return Codegen(record.loc())
                .record(record.params(), std::move(new_fields));
    }

    ASTPtr optimize(const ASTOp& op)
    {
        ASTVec new_args;
        for (size_t i = 0; i < op.args().size(); ++i) {
            if ((op.op() == Op::ASSIGN || op.op() == Op::ASSUME) && i == 0) {
                new_args.push_back(op.args()[i]);
                continue;
            }
            new_args.push_back(optimizeNode(op.args()[i]));
        }

        // Record type aliases from ASSIGN nodes
        if (op.op() == Op::ASSIGN && isFieldType(new_args[1])) {
            type_aliases_[op.args()[0]->as_var()->name()] = new_args[1];
        }

        return std::make_shared<ASTOp>(op.loc(), op.op(), std::move(new_args));
    }

    const detail::Logger& log_;
    std::unordered_map<std::string, ASTPtr> type_aliases_;
};

} // namespace

TypeAliasPass::TypeAliasPass(const detail::Logger& logger) : log_(logger) {}

ASTVec TypeAliasPass::optimize(const ASTVec& ast) const
{
    return TypeAliasImpl{ log_ }.optimize(ast);
}

} // namespace openzl::sddl2
