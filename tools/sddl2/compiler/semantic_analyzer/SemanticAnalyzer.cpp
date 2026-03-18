// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "tools/sddl2/compiler/Exception.h"
#include "tools/sddl2/compiler/parser/AST.h"
#include "tools/sddl2/compiler/semantic_analyzer/SemanticAnalyzer.h"

namespace openzl::sddl2 {
namespace {

enum class TypeKind {
    NUMERIC,
    CONSUMED_RECORD,
    NONE,
    ARRAY,
    RECORD,
    BYTES,
    BUILTIN_FIELD,
};

struct Type {
    TypeKind kind           = TypeKind::NONE;
    const ASTNode* type_def = nullptr;
};

void expectNumeric(const Type& t)
{
    if (t.kind != TypeKind::NUMERIC) {
        throw SemanticError("Expected a numeric expression.");
    }
}

void expectFieldType(const Type& t)
{
    if (!(t.kind == TypeKind::ARRAY || t.kind == TypeKind::RECORD
          || t.kind == TypeKind::BYTES || t.kind == TypeKind::BUILTIN_FIELD)) {
        throw SemanticError("Expected a field type expression.");
    }
}

const ASTVar* someVar(const ASTPtr& node)
{
    if (auto var = node->as_var()) {
        return var;
    }
    throw SemanticError("Expected a variable name.");
}

bool hasLoadInstruction(Symbol sym)
{
    static const std::unordered_set<Symbol> loadable{
        Symbol::BYTE,  Symbol::U8,    Symbol::I8,    Symbol::U16LE,
        Symbol::U16BE, Symbol::I16LE, Symbol::I16BE, Symbol::U32LE,
        Symbol::U32BE, Symbol::I32LE, Symbol::I32BE, Symbol::U64LE,
        Symbol::U64BE, Symbol::I64LE, Symbol::I64BE,
    };
    return loadable.count(sym) > 0;
}

/**
 * Returns the type of the resulting variable after assuming a field of the
 * given field_type.
 */
Type assumedType(const Type& field_type)
{
    if (field_type.kind == TypeKind::RECORD) {
        return Type{ TypeKind::CONSUMED_RECORD, field_type.type_def };
    }
    if (field_type.kind == TypeKind::BUILTIN_FIELD) {
        const auto* builtin = field_type.type_def->as_builtin_field();
        if (builtin && hasLoadInstruction(builtin->kw())) {
            return Type{ TypeKind::NUMERIC };
        }
    }
    // Assume not defined for other types. Trying to use the result in an
    // operation will fail.
    return Type{ TypeKind::NONE };
}

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
    // Data members
    const detail::Logger& log_;
    std::unordered_map<std::string, Type> var_types_;

    // Analysis methods
    Type analyzeNode(const ASTPtr& node)
    {
        switch (node->converted_node_type()) {
            case ConvertedNodeType::NUM:
                return Type{ TypeKind::NUMERIC };
            case ConvertedNodeType::BUILTIN_FIELD:
                return Type{ TypeKind::BUILTIN_FIELD,
                             node->as_builtin_field() };
            case ConvertedNodeType::VAR:
                return analyze(*node->as_var());
            case ConvertedNodeType::BYTES:
                return analyze(*node->as_bytes());
            case ConvertedNodeType::ARRAY:
                return analyze(*node->as_array());
            case ConvertedNodeType::RECORD:
                return analyze(*node->as_record());
            case ConvertedNodeType::RECORD_FIELD:
                return analyze(*node->as_record_field());
            case ConvertedNodeType::CALL:
                return analyze(*node->as_call());
            case ConvertedNodeType::WHEN:
                return analyze(*node->as_when());
            case ConvertedNodeType::OP:
                return analyze(*node->as_op());
            default:
                throw InvariantViolation("Unsupported AST node type.");
        }
    }

    Type analyze(const ASTVar& var)
    {
        auto it = var_types_.find(var.name());
        if (it == var_types_.end()) {
            throw SemanticError(
                    var.loc(), "Undefined variable: '" + var.name() + "'");
        }
        return it->second;
    }

    Type analyze(const ASTBytes& bytes)
    {
        expectNumeric(analyzeNode(bytes.len()));
        return Type{ TypeKind::BYTES, &bytes };
    }

    Type analyze(const ASTArray& array)
    {
        expectFieldType(analyzeNode(array.field()));
        if (array.len()) {
            expectNumeric(analyzeNode(array.len()));
        }
        return Type{ TypeKind::ARRAY, &array };
    }

    Type analyze(const ASTRecordField& field)
    {
        const auto* var = field.name()->as_var();
        if (!var) {
            throw SemanticError(field.loc(), "Field name must be a variable.");
        }
        expectFieldType(analyzeNode(field.type()));
        return Type{ TypeKind::NONE };
    }

    Type analyze(const ASTRecord& record)
    {
        // Validate params are variable names and introduce them as NUMERIC
        auto saved_vars = var_types_;
        for (const auto& param : record.params()) {
            const auto* var = param->as_var();
            if (!var) {
                throw SemanticError(
                        param->loc(),
                        "Record parameter must be a variable name.");
            }
            var_types_[var->name()] = Type{ TypeKind::NUMERIC };
        }

        // Validate all fields
        for (const auto& field : record.fields()) {
            analyzeNode(field);
        }

        var_types_ = std::move(saved_vars);

        return Type{ TypeKind::RECORD, &record };
    }

    Type analyze(const ASTCall& call)
    {
        // Target must resolve to a record type
        auto target_type = analyzeNode(call.target());
        if (target_type.kind != TypeKind::RECORD) {
            throw SemanticError(
                    call.target()->loc(), "Call target must be a record type.");
        }

        const auto* record = target_type.type_def->as_record();
        if (!record) {
            throw SemanticError(
                    call.target()->loc(), "Call target must be a record type.");
        }

        // Validate argument count matches parameter count
        if (call.args().size() != record->params().size()) {
            throw SemanticError(
                    call.loc(),
                    "Expected " + std::to_string(record->params().size())
                            + " arguments but got "
                            + std::to_string(call.args().size()) + ".");
        }

        // Validate each argument is a numeric expression
        for (const auto& arg : call.args()) {
            expectNumeric(analyzeNode(arg));
        }

        return Type{ TypeKind::RECORD, target_type.type_def };
    }

    Type analyze(const ASTOp& op)
    {
        switch (op.op()) {
            case Op::ASSIGN:
                return analyzeAssign(op);
            case Op::ASSUME:
                return analyzeAssume(op);
            case Op::MEMBER:
                return analyzeMember(op);
            case Op::CONSUME:
                expectFieldType(analyzeNode(op.args()[0]));
                return Type{ TypeKind::NONE };
            case Op::SIZEOF:
                expectFieldType(analyzeNode(op.args()[0]));
                return Type{ TypeKind::NUMERIC };
            case Op::EXPECT:
                expectNumeric(analyzeNode(op.args()[0]));
                return Type{ TypeKind::NONE };
            case Op::NEG:
            case Op::LOG_NOT:
            case Op::BIT_NOT:
                expectNumeric(analyzeNode(op.args()[0]));
                return Type{ TypeKind::NUMERIC };
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
            case Op::LOG_OR:
                expectNumeric(analyzeNode(op.args()[0]));
                expectNumeric(analyzeNode(op.args()[1]));
                return Type{ TypeKind::NUMERIC };
            case Op::SEND:
            default:
                throw InvariantViolation(op.loc(), "Unsupported operation.");
        }
    }

    Type analyzeAssign(const ASTOp& op)
    {
        auto var = someVar(op.args()[0]);
        if (var_types_.count(var->name()) > 0) {
            throw SemanticError(
                    var->loc(),
                    "Variable '" + var->name() + "' already defined.");
        }
        var_types_[var->name()] = analyzeNode(op.args()[1]);
        return Type{ TypeKind::NONE };
    }

    Type analyzeAssume(const ASTOp& op)
    {
        // Check that the RHS is a valid field type
        auto field_type = analyzeNode(op.args()[1]);
        expectFieldType(field_type);

        // Assign the var to the assumed type
        auto var = someVar(op.args()[0]);
        if (var_types_.count(var->name()) > 0) {
            throw SemanticError(
                    var->loc(),
                    "Variable '" + var->name() + "' already defined.");
        }
        var_types_[var->name()] = assumedType(field_type);

        return Type{ TypeKind::NONE };
    }

    std::optional<Type> findField(const ASTVec& fields, const std::string& name)
    {
        for (const auto& field : fields) {
            if (auto record_field = field->as_record_field()) {
                auto& field_name = record_field->name()->as_var()->name();
                if (field_name == name) {
                    return assumedType(analyzeNode(record_field->type()));
                }
            }
            if (auto when = field->as_when()) {
                auto found_type = findField(when->body(), name);
                if (found_type) {
                    throw SemanticError(
                            field->loc(),
                            "Member access not supported on optional fields.");
                }
            }
        }
        return std::nullopt;
    }

    Type analyzeMember(const ASTOp& op)
    {
        auto saved_vars = var_types_;
        // Check that the LHS is a consumed record
        auto lhs_type = analyzeNode(op.args()[0]);
        if (lhs_type.kind != TypeKind::CONSUMED_RECORD) {
            throw SemanticError(
                    op.args()[0]->loc(),
                    "LHS of member access is not a record.");
        }

        // Check that the RHS is a valid field name return the consumed type
        auto* record           = lhs_type.type_def->as_record();
        const auto& field_name = someVar(op.args()[1])->name();

        // Add the record params to scope
        for (const auto& param : record->params()) {
            var_types_[param->as_var()->name()] = Type{ TypeKind::NUMERIC };
        }

        // Find the field
        auto found_type = findField(record->fields(), field_name);
        if (!found_type) {
            throw SemanticError(
                    op.args()[1]->loc(),
                    "Field '" + field_name + "' not a valid record field.");
        }
        var_types_ = std::move(saved_vars);
        return *found_type;
    }

    Type analyze(const ASTWhen& when)
    {
        expectNumeric(analyzeNode(when.condition()));
        for (const auto& stmt : when.body()) {
            analyzeNode(stmt);
        }
        return Type{ TypeKind::NONE };
    }
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
