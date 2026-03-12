// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "tools/sddl2/compiler/Exception.h"
#include "tools/sddl2/compiler/codegen/AssemblyOutput.h"
#include "tools/sddl2/compiler/codegen/CodeGenerator.h"
#include "tools/sddl2/compiler/codegen/RegisterAllocator.h"
#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2 {
namespace {

/**
 * Maps operations to their corresponding assembly instructions.
 */

const std::map<Op, std::string> op_to_asm = {
    { Op::EXPECT, "expect_true" },

    { Op::SIZEOF, "type.sizeof" },

    { Op::ADD, "math.add" },       { Op::SUB, "math.sub" },
    { Op::MUL, "math.mul" },       { Op::DIV, "math.div" },
    { Op::MOD, "math.mod" },       { Op::NEG, "math.neg" },

    { Op::EQ, "cmp.eq" },          { Op::NE, "cmp.ne" },
    { Op::GT, "cmp.gt" },          { Op::GE, "cmp.ge" },
    { Op::LT, "cmp.lt" },          { Op::LE, "cmp.le" },

    { Op::LOG_AND, "logic.and" },  { Op::LOG_OR, "logic.or" },
    { Op::LOG_NOT, "logic.not" },

    { Op::BIT_AND, "logic.and" },  { Op::BIT_OR, "logic.or" },
    { Op::BIT_XOR, "logic.xor" },  { Op::BIT_NOT, "logic.not" },
};

/**
 * Maps builtin field types to their corresponding assembly instructions.
 */
const std::map<Symbol, std::string> builtin_field_to_asm = {
    { Symbol::BYTE, "bytes" },    { Symbol::U8, "u8" },
    { Symbol::I8, "i8" },         { Symbol::U16LE, "u16le" },
    { Symbol::U16BE, "u16be" },   { Symbol::I16LE, "i16le" },
    { Symbol::I16BE, "i16be" },   { Symbol::U32LE, "u32le" },
    { Symbol::U32BE, "u32be" },   { Symbol::I32LE, "i32le" },
    { Symbol::I32BE, "i32be" },   { Symbol::U64LE, "u64le" },
    { Symbol::U64BE, "u64be" },   { Symbol::I64LE, "i64le" },
    { Symbol::I64BE, "i64be" },   { Symbol::F16LE, "f16le" },
    { Symbol::F16BE, "f16be" },   { Symbol::F32LE, "f32le" },
    { Symbol::F32BE, "f32be" },   { Symbol::F64LE, "f64le" },
    { Symbol::F64BE, "f64be" },   { Symbol::BF16LE, "bf16le" },
    { Symbol::BF16BE, "bf16be" },
};

/**
 * Maps builtin field types to their corresponding load instructions.
 */
const std::map<Symbol, std::string> builtin_field_to_load = {
    { Symbol::BYTE, "load.u8" },     { Symbol::U8, "load.u8" },
    { Symbol::I8, "load.i8" },       { Symbol::U16LE, "load.u16le" },
    { Symbol::U16BE, "load.u16be" }, { Symbol::I16LE, "load.i16le" },
    { Symbol::I16BE, "load.i16be" }, { Symbol::U32LE, "load.u32le" },
    { Symbol::U32BE, "load.u32be" }, { Symbol::I32LE, "load.i32le" },
    { Symbol::I32BE, "load.i32be" }, { Symbol::U64LE, "load.u64le" },
    { Symbol::U64BE, "load.u64be" }, { Symbol::I64LE, "load.i64le" },
    { Symbol::I64BE, "load.i64be" },
};

using TypeResult = std::pair<AssemblyOutput, ASTPtr>;

/**
 * Takes an AST and generates assembly code.
 */
class CodeGeneratorImpl {
   public:
    explicit CodeGeneratorImpl(const detail::Logger& logger) : log_(logger) {}
    /**
     * Generates assembly from the given AST.
     *
     * @param ast The abstract syntax tree produced by the parser.
     * @returns The generated assembly instructions.
     */
    std::string generate(const ASTVec& ast)
    {
        (void)log_;
        AssemblyOutput output;
        for (const auto& node : ast) {
            output += generateOp(*node->as_op());
        }
        return output.str();
    }

   private:
    /**
     * Generates code for an operation node. This is the central dispatch
     * for the code generator. Each op knows whether its arguments are
     * values or types.
     */
    AssemblyOutput generateOp(const ASTOp& op)
    {
        AssemblyOutput output;
        switch (op.op()) {
            case Op::EXPECT:
            case Op::NEG:
            case Op::LOG_NOT:
            case Op::BIT_NOT: {
                output += generateValue(op.args()[0]);
                output += op_to_asm.at(op.op());
                return output;
            }
            case Op::SIZEOF: {
                auto [type_asm, _] = generateType(op.args()[0]);
                output += std::move(type_asm);
                output += op_to_asm.at(op.op());
                return output;
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
                output += generateValue(op.args()[0]);
                output += generateValue(op.args()[1]);
                output += op_to_asm.at(op.op());
                return output;
            }
            case Op::ASSIGN:
                return generateAssign(op);
            case Op::ASSUME:
                return generateAssume(op);
            case Op::CONSUME:
                return generateConsume(generateType(op.args()[0]));
            case Op::MEMBER:
                return generateMember(op);
            case Op::SEND:
            default:
                throw InvariantViolation(op.loc(), "Unsupported operation.");
        };
    }

    /**
     * Generates code that pushes a value onto the stack.
     * Handles numeric literals, variable references, and value-producing ops.
     */
    AssemblyOutput generateValue(const ASTPtr& node)
    {
        AssemblyOutput output;
        switch (node->converted_node_type()) {
            case ConvertedNodeType::NUM: {
                output += "push.i64 " + std::to_string(node->as_num()->val());
                return output;
            }
            case ConvertedNodeType::VAR: {
                auto var = node->as_var();
                output += "push.i64 "
                        + std::to_string(registers_.get(var->name()));
                output += "var.load";
                return output;
            }
            case ConvertedNodeType::OP:
                return generateOp(*node->as_op());
            case ConvertedNodeType::BUILTIN_FIELD:
            case ConvertedNodeType::BYTES:
            case ConvertedNodeType::ARRAY:
            case ConvertedNodeType::RECORD:
            case ConvertedNodeType::CALL:
            default:
                throw InvariantViolation(
                        node->loc(), "Expected a value, got a type.");
        }
    }

    AssemblyOutput bindParams(const ASTRecord* record, const ASTVec& args)
    {
        AssemblyOutput output;
        for (size_t i = 0; i < record->params().size(); ++i) {
            const auto& param_name = record->params()[i]->as_var()->name();
            auto reg               = registers_.assign(param_name);
            output += generateValue(args[i]);
            output += "push.i64 " + std::to_string(reg);
            output += "var.store";
        }
        return output;
    }

    TypeResult generateType(const ASTPtr& type)
    {
        AssemblyOutput output;

        switch (type->converted_node_type()) {
            case ConvertedNodeType::VAR: {
                auto var = type->as_var();
                auto it  = type_aliases_.find(var->name());
                if (it == type_aliases_.end()) {
                    throw InvariantViolation(
                            type->loc(), "Undefined variable!");
                }
                output += "push.i64 "
                        + std::to_string(registers_.get(var->name()));
                output += "var.load";
                return { std::move(output), it->second };
            }
            case ConvertedNodeType::BUILTIN_FIELD: {
                output += "push.type."
                        + builtin_field_to_asm.at(
                                type->as_builtin_field()->kw());
                return { std::move(output), type };
            }
            case ConvertedNodeType::RECORD: {
                auto record = type->as_record();
                for (const auto& field : record->fields()) {
                    auto assume            = field->as_op();
                    const auto& field_type = assume->args()[1];
                    auto [field_asm, _]    = generateType(field_type);
                    output += std::move(field_asm);
                }
                output += "push.i64 " + std::to_string(record->fields().size());
                output += "type.structure";
                return { std::move(output), type };
            }
            case ConvertedNodeType::BYTES: {
                auto bytes = type->as_bytes();
                output += "push.type.bytes";
                output += generateValue(bytes->len());
                output += "type.fixed_array";
                return { std::move(output), type };
            }
            case ConvertedNodeType::ARRAY: {
                auto array          = type->as_array();
                auto [field_asm, _] = generateType(array->field());
                output += std::move(field_asm);
                auto& len = array->len();
                if (len) {
                    output += generateValue(len);
                } else {
                    output += "stack.dup";
                    output += "type.sizeof";
                    output += "push.remaining";
                    output += "stack.swap";
                    output += "math.div";
                }
                output += "type.fixed_array";
                return { std::move(output), type };
            }
            case ConvertedNodeType::CALL: {
                auto call               = type->as_call();
                const auto& target_name = call->target()->as_var()->name();
                auto target             = type_aliases_.at(target_name);

                auto saved_regs = registers_;
                // Bind params to registers
                output += bindParams(target->as_record(), call->args());

                // Generate the record body
                auto [record_asm, _] = generateType(target);
                output += std::move(record_asm);

                // Restore the registers
                registers_ = std::move(saved_regs);

                return { std::move(output), type };
            }
            case ConvertedNodeType::NUM:
            case ConvertedNodeType::OP:
            default:
                throw InvariantViolation(
                        type->loc(), "Expected a type, got a value.");
        }
    }

    std::pair<const ASTVar&, std::vector<std::string>> flattenMember(
            const ASTOp& op)
    {
        ASTPtr root                   = op.args()[0];
        std::vector<std::string> path = { op.args()[1]->as_var()->name() };

        while (auto member_op = root->as_op()) {
            if (member_op->op() != Op::MEMBER)
                break;
            path.push_back(member_op->args()[1]->as_var()->name());
            root = member_op->args()[0];
        }

        // Reverse path since we built it from leaf to root
        std::reverse(path.begin(), path.end());

        return { *root->as_var(), std::move(path) };
    }

    AssemblyOutput generateMember(const ASTOp& op)
    {
        AssemblyOutput output;
        // Flatten the member chain
        const auto& [root, path] = flattenMember(op);

        // Load the base offset
        output += "push.i64 " + std::to_string(registers_.get(root.name()));
        output += "var.load";

        auto type       = assumed_types_[root.name()];
        auto saved_regs = registers_;

        // Walk through the path, accumulating offsets
        for (const auto& name : path) {
            auto curr_record = type->as_record();
            if (const auto call = type->as_call()) {
                auto const target_name = call->target()->as_var()->name();
                curr_record = type_aliases_.at(target_name)->as_record();
                output += bindParams(curr_record, call->args());
            }
            for (const auto& field : curr_record->fields()) {
                auto assume      = field->as_op();
                auto& field_name = assume->args()[0]->as_var()->name();
                auto [field_asm, field_type] = generateType(assume->args()[1]);
                if (name == field_name) {
                    type = field_type;
                    break;
                }
                output += std::move(field_asm);
                output += "type.sizeof";
                output += "math.add";
            }
        }
        registers_ = std::move(saved_regs);

        // Load the final value if it's a builtin type
        if (auto builtin = type->as_builtin_field()) {
            output += builtin_field_to_load.at(builtin->kw());
        } else {
            throw CodegenError(
                    op.loc(), "Member access must be a builtin type.");
        }
        return output;
    }

    AssemblyOutput generateAssign(const ASTOp& op)
    {
        auto& var = *op.args()[0]->as_var();
        auto& rhs = op.args()[1];

        // If the RHS is a parameterized record, do not generate the code
        if (auto record = rhs->as_record()) {
            if (!record->params().empty()) {
                type_aliases_[var.name()] = rhs;
                return AssemblyOutput{};
            }
        }

        auto [type_asm, type]     = generateType(rhs);
        type_aliases_[var.name()] = type;

        AssemblyOutput output;
        output += std::move(type_asm);
        output += "push.i64 " + std::to_string(registers_.assign(var.name()));
        output += "var.store";
        return output;
    }

    AssemblyOutput generateAssume(const ASTOp& op)
    {
        auto& var        = *op.args()[0]->as_var();
        auto type_result = generateType(op.args()[1]);
        AssemblyOutput output;

        // Save the current position
        output += "push.current_pos";

        // Consume the type
        output += generateConsume(type_result);

        auto type = type_result.second;

        if (const auto builtin = type->as_builtin_field()) {
            // If the type is a builtin field save the loaded value
            output += builtin_field_to_load.at(builtin->kw());
            output +=
                    "push.i64 " + std::to_string(registers_.assign(var.name()));
        } else if (type->as_record() || type->as_call()) {
            // Otherwise, save the current position and type information
            output +=
                    "push.i64 " + std::to_string(registers_.assign(var.name()));
            assumed_types_[var.name()] = type;
        } else {
            throw CodegenError(
                    op.loc(), "Assume must be a builtin field or record.");
        }

        output += "var.store";
        return output;
    }

    AssemblyOutput generateConsume(TypeResult type_result)
    {
        AssemblyOutput output;
        auto& [type_asm, _] = type_result;
        output += "push.tag " + std::to_string(tag_++);
        output += std::move(type_asm);
        output += "push.i64 1";
        output += "segment.create_tagged";
        return output;
    }

    const detail::Logger& log_;
    size_t tag_ = 1;

    // Register allocation
    RegisterAllocator registers_;
    std::unordered_map<std::string, ASTPtr> type_aliases_;
    std::unordered_map<std::string, ASTPtr> assumed_types_;
};

} // namespace

CodeGenerator::CodeGenerator(const detail::Logger& logger) : log_(logger) {}

std::string CodeGenerator::generate(const ASTVec& ast) const
{
    return CodeGeneratorImpl{ log_ }.generate(ast);
}

} // namespace openzl::sddl2
