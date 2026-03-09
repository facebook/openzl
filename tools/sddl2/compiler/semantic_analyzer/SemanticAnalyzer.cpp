// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <unordered_map>

#include "tools/sddl2/compiler/Exception.h"
#include "tools/sddl2/compiler/parser/AST.h"
#include "tools/sddl2/compiler/semantic_analyzer/SemanticAnalyzer.h"

namespace openzl::sddl2 {
namespace {

enum class ValueType { NUMERIC, ARRAY, RECORD, BYTES, BUILTIN_FIELD, NONE };

class SemanticAnalyzerImpl {
   public:
    explicit SemanticAnalyzerImpl(const detail::Logger& logger) : log_(logger)
    {
    }

    void analyze(const ASTVec& ast)
    {
        log_(1) << "Performing semantic analysis..." << std::endl;
        for (const auto& node : ast) {
            analyzeNode(node);
        }
    }

   private:
    void expectNumeric(ValueType type)
    {
        if (type != ValueType::NUMERIC) {
            throw SemanticError("Expected a numeric expression.");
        }
    }

    void expectFieldType(ValueType type)
    {
        if (type != ValueType::BUILTIN_FIELD && type != ValueType::ARRAY
            && type != ValueType::RECORD && type != ValueType::BYTES) {
            throw SemanticError("Expected a field type expression.");
        }
    }

    ValueType analyzeNode(const ASTPtr& node)
    {
        const auto type = node->converted_node_type();
        switch (type) {
            case ConvertedNodeType::NUM:
                return ValueType::NUMERIC;
            case ConvertedNodeType::BUILTIN_FIELD:
                return ValueType::BUILTIN_FIELD;
            case ConvertedNodeType::VAR:
                return analyze(*node->as_var());
            case ConvertedNodeType::BYTES:
                return analyze(*node->as_bytes());
            case ConvertedNodeType::ARRAY:
                return analyze(*node->as_array());
            case ConvertedNodeType::RECORD:
                return analyze(*node->as_record());
            case ConvertedNodeType::OP:
                return analyze(*node->as_op());
            default:
                throw InvariantViolation("Unsupported AST node type.");
        }
    }

    ValueType analyze(const ASTVar& var)
    {
        if (var_types_.find(var.name()) == var_types_.end()) {
            throw SemanticError(
                    var.loc(), "Undefined variable: '" + var.name() + "'");
        }
        return var_types_[var.name()];
    }

    ValueType analyze(const ASTBytes& bytes)
    {
        expectNumeric(analyzeNode(bytes.len()));
        return ValueType::BYTES;
    }

    ValueType analyze(const ASTArray& array)
    {
        expectFieldType(analyzeNode(array.field()));
        if (array.len()) {
            expectNumeric(analyzeNode(array.len()));
        }
        return ValueType::ARRAY;
    }

    ValueType analyze(const ASTRecord& record)
    {
        // Validate all fields are ASSUME ops
        auto saved_vars = var_types_;
        for (const auto& field : record.fields()) {
            auto assume = field->as_op();
            if (!assume || assume->op() != Op::ASSUME) {
                throw SemanticError(
                        field->loc(),
                        "Record field must be an assume operation!");
            }
            analyzeNode(field);
        }
        var_types_ = std::move(saved_vars);

        // TODO: support params
        if (!record.params().empty()) {
            throw SemanticError(
                    record.loc(), "Record params not yet supported!");
        }

        return ValueType::RECORD;
    }

    ValueType analyze(const ASTOp& op)
    {
        switch (op.op()) {
            case Op::ASSIGN: {
                // LHS must be a variable
                if (!op.args()[0]->as_var()) {
                    throw SemanticError(
                            op.args()[0]->loc(),
                            "Left-hand side of assignment must be a variable "
                            "name!");
                }
                // Analyze RHS first (so `x = x + 1` errors if x undefined)
                auto type = analyzeNode(op.args()[1]);
                var_types_[op.args()[0]->as_var()->name()] = type;
                break;
            }
            case Op::ASSUME: {
                // LHS must be a variable
                if (!op.args()[0]->as_var()) {
                    throw SemanticError(
                            op.args()[0]->loc(),
                            "Left-hand side of assume must be a variable "
                            "name!");
                }
                // Value must be a field type
                auto type = analyzeNode(op.args()[1]);
                expectFieldType(type);
                var_types_[op.args()[0]->as_var()->name()] = ValueType::NUMERIC;
                break;
            }
            case Op::MEMBER: {
                // TODO: implement
                return ValueType::NUMERIC;
            }
            case Op::CONSUME: {
                expectFieldType(analyzeNode(op.args()[0]));
                return ValueType::NONE;
            }
            case Op::SIZEOF: {
                expectFieldType(analyzeNode(op.args()[0]));
                return ValueType::NUMERIC;
            }
            case Op::EXPECT: {
                expectNumeric(analyzeNode(op.args()[0]));
                return ValueType::NONE;
            }
            case Op::NEG:
            case Op::LOG_NOT:
            case Op::BIT_NOT: {
                expectNumeric(analyzeNode(op.args()[0]));
                return ValueType::NUMERIC;
            }
            case Op::ADD:
            case Op::SUB:
            case Op::MUL:
            case Op::DIV:
            case Op::MOD:
            case Op::EQ:
            case Op::NE:
            case Op::GT:
            case Op::GE:
            case Op::LT:
            case Op::LE:
            case Op::BIT_AND:
            case Op::BIT_OR:
            case Op::BIT_XOR:
            case Op::LOG_AND:
            case Op::LOG_OR: {
                expectNumeric(analyzeNode(op.args()[0]));
                expectNumeric(analyzeNode(op.args()[1]));
                return ValueType::NUMERIC;
            }
            case Op::SEND:
            default:
                throw InvariantViolation(op.loc(), "Unsupported operation.");
        }
        return ValueType::NONE;
    }

    const detail::Logger& log_;
    std::unordered_map<std::string, ValueType> var_types_;
};

} // namespace

SemanticAnalyzer::SemanticAnalyzer(const detail::Logger& logger) : log_(logger)
{
}

void SemanticAnalyzer::analyze(const ASTVec& ast) const
{
    SemanticAnalyzerImpl{ log_ }.analyze(ast);
}

} // namespace openzl::sddl2
