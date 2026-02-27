// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <sstream>

#include "tools/sddl2/compiler/Exception.h"
#include "tools/sddl2/compiler/codegen/CodeGenerator.h"
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
 * Output of the code generator: a sequence of assembly instructions.
 */
using Instruction    = std::string;
using AssemblyOutput = std::vector<Instruction>;

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
            generateNode(node, output);
        }

        std::ostringstream oss;
        for (const auto& inst : output) {
            oss << inst << std::endl;
        }

        return std::move(oss).str();
    }

   private:
    /**
     * Generates code for a single AST node.
     *
     * @param node The AST node to process.
     * @param output The output vector to append instructions to.
     */
    void generateNode(const ASTPtr& node, AssemblyOutput& output)
    {
        const auto type = node->converted_node_type();
        switch (type) {
            case ConvertedNodeType::NUM: {
                return generate(*node->as_num(), output);
            };
            case ConvertedNodeType::VAR: {
                return generate(*node->as_var(), output);
            };
            case ConvertedNodeType::BUILTIN_FIELD: {
                return generate(*node->as_builtin_field(), output);
            };
            case ConvertedNodeType::BYTES: {
                return generate(*node->as_bytes(), output);
            };
            case ConvertedNodeType::RECORD: {
                return generate(*node->as_record(), output);
            }
            case ConvertedNodeType::ARRAY: {
                return generate(*node->as_array(), output);
            }
            case ConvertedNodeType::OP: {
                return generate(*node->as_op(), output);
            }
            default:
                throw std::runtime_error("Unsupported AST node type");
        }
    }

    /**
     * Generates code for a numeric literal.
     *
     * @param value The numeric value.
     * @param output The output vector to append instructions to.
     */
    void generate(const ASTNum& num, AssemblyOutput& output) const
    {
        output.emplace_back("push.i64 " + std::to_string(num.val()));
    }

    /**
     * Generates code for a variable reference.
     *
     * @param var The variable node.
     * @param output The output vector to append instructions to.
     */
    void generate(const ASTVar& var, AssemblyOutput& output)
    {
        (void)output;
        throw CodegenError(
                "Variable references are not yet supported: " + var.name());
    }

    /**
     * Generates code for a builtin field type.
     */
    void generate(const ASTBuiltinField& field, AssemblyOutput& output) const
    {
        output.emplace_back("push.type." + builtin_field_to_asm.at(field.kw()));
    }

    /**
     * Generates code for Bytes field type.
     */
    void generate(const ASTBytes& bytes, AssemblyOutput& output)
    {
        output.emplace_back("push.type.bytes");
        generateNode(bytes.len(), output);
        output.emplace_back("type.fixed_array");
    }

    /**
     * Generates code for Record field type.
     */
    void generate(const ASTRecord& record, AssemblyOutput& output)
    {
        // TODO: eventually we want to be able to generate scan records. For
        // now, we define them as a structure type.

        // Field types
        for (const auto& field : record.fields()) {
            // We expect assign + consume operations
            auto assume = field->as_op();
            if (!assume || assume->op() != Op::ASSUME) {
                throw CodegenError(
                        assume->loc(),
                        "Record field must be an assume operation!");
            }
            generateNode(assume->args()[1], output);
        }
        // Number of fields
        output.emplace_back(
                "push.i64 " + std::to_string(record.fields().size()));
        output.emplace_back("type.structure");
    }

    /**
     * Generates code for Array field type.
     */
    void generate(const ASTArray& array, AssemblyOutput& output)
    {
        // Field type
        generateNode(array.field(), output);
        // Array length
        auto& len = array.len();
        if (len) {
            generateNode(len, output);
        } else {
            output.emplace_back("stack.dup");
            output.emplace_back("type.sizeof");
            output.emplace_back("push.remaining");
            output.emplace_back("stack.swap");
            output.emplace_back("math.div");
        }
        output.emplace_back("type.fixed_array");
    }

    /**
     * Generates code for an operation node.
     *
     * @param op The operation type.
     * @param args The operation arguments.
     * @param output The output vector to append instructions to.
     */

    void generate(const ASTOp& op, AssemblyOutput& output)
    {
        switch (op.op()) {
            case Op::EXPECT:
            case Op::NEG:
            case Op::LOG_NOT:
            case Op::BIT_NOT:
            case Op::SIZEOF: {
                generateNode(op.args()[0], output);
                output.emplace_back(op_to_asm.at(op.op()));
                break;
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
                generateNode(op.args()[0], output);
                generateNode(op.args()[1], output);
                output.emplace_back(op_to_asm.at(op.op()));
                break;
            }
            case Op::ASSIGN: {
                auto maybe_var = op.args()[0];
                if (!maybe_var->as_var()) {
                    throw InvariantViolation(
                            maybe_var->loc(),
                            "Left-hand side of assignment must be a variable name!");
                }
                // TODO: actually handle variable assignment
                break;
            }
            case Op::ASSUME: // TODO: treat assume differently from consume
            case Op::CONSUME: {
                const auto val =
                        (op.op() == Op::ASSUME) ? op.args()[1] : op.args()[0];
                const auto type = val->converted_node_type();

                if (type == ConvertedNodeType::OP
                    || type == ConvertedNodeType::NUM) {
                    throw InvariantViolation(
                            val->loc(),
                            "Consume operation expects a valid field type.");
                }

                // Tag
                output.emplace_back("push.tag " + std::to_string(tag_++));
                // Type
                generateNode(val, output);
                // Length
                output.emplace_back("push.i64 1");
                output.emplace_back("segment.create_tagged");
                break;
            }
            case Op::SEND:
            default:
                throw InvariantViolation(op.loc(), "Unsupported operation.");
        };
    }

    const detail::Logger& log_;
    size_t tag_ = 1;
};

} // namespace

CodeGenerator::CodeGenerator(const detail::Logger& logger) : log_(logger) {}

std::string CodeGenerator::generate(const ASTVec& ast) const
{
    return CodeGeneratorImpl{ log_ }.generate(ast);
}

} // namespace openzl::sddl2
