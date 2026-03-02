// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <iomanip>
#include <sstream>

#include "tools/sddl2/compiler/Compiler.h"
#include "tools/sddl2/compiler/Exception.h"
#include "tools/sddl2/compiler/parser/AST.h"

using namespace testing;

namespace openzl::sddl2::tests {

namespace {
using ArgVec = std::vector<ASTPtr>;

// Helper class to check that the tokenization/grouper/parser steps are working
// as expected
class ASTCompiler : public Compiler {
   public:
    explicit ASTCompiler(Compiler::Options options)
            : Compiler{ std::move(options) }
    {
    }

    ASTVec compile(std::string_view source, std::string_view filename)
    {
        const Source src{ source, filename };
        const auto tokens = tokenizer_.tokenize(src);
        const auto groups = grouper_.group(tokens);
        const auto tree   = parser_.parse(groups);
        semantic_analyzer_.analyze(tree);
        return tree;
    }
};

void expect_node_eq(const ASTNode& lhs, const ASTNode& rhs)
{
    // Compare by printing to string - this works for all AST node types
    std::ostringstream lhs_ss, rhs_ss;
    lhs.print(lhs_ss, 0);
    rhs.print(rhs_ss, 0);
    EXPECT_EQ(lhs_ss.str(), rhs_ss.str());
}
} // namespace

class CompilerTest : public Test {
   protected:
    void SetUp() override
    {
        compiler_ = std::make_unique<Compiler>(
                Compiler::Options{}.with_log(logs_).with_verbosity(verbosity_));
        ast_compiler_ = std::make_unique<ASTCompiler>(
                Compiler::Options{}.with_log(logs_).with_verbosity(verbosity_));
    }

    void expect_error(std::string_view source, std::string_view msg)
    {
        try {
            compiler_->compile(source, "[local_input]");
        } catch (const CompilerException& ex) {
            EXPECT_NE(std::string{ ex.what() }.find(msg), std::string::npos)
                    << std::quoted(ex.what()) << "\nShould contain:\n  "
                    << std::quoted(msg) << "\n"
                    << "Compiler debug logs:\n"
                    << logs_.str();
            return;
        }
        EXPECT_TRUE(false) << "Should have thrown a CompilerException!\n"
                           << "Compiler debug logs:\n"
                           << logs_.str();
    }

    void expect_success(std::string_view source)
    {
        EXPECT_NO_THROW(compiler_->compile(source, "[local_input]"))
                << "Compiler debug logs:\n"
                << logs_.str();
    }

    void expect_ast(std::string_view source, ASTVec expected)
    {
        const auto actual = ast_compiler_->compile(source, "[local_input]");

        EXPECT_EQ(actual.size(), expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            auto& actual_node   = *actual.at(i);
            auto& expected_node = *expected.at(i);
            expect_node_eq(actual_node, expected_node);
        }
    }

    int verbosity_{ 3 };
    std::stringstream logs_;
    std::unique_ptr<Compiler> compiler_;
    std::unique_ptr<ASTCompiler> ast_compiler_;
};

TEST_F(CompilerTest, ErrorMsgOpsMissingArgs)
{
    expect_error("foo = \n", "Rule requires 1 rhs args but 0 found.");
    expect_error("= foo\n", "Rule requires a lhs arg but none found.");
}

TEST_F(CompilerTest, ErrorMsgEmptyExpr)
{
    expect_error("()", "Empty expression");
}

TEST_F(CompilerTest, ErrorMsgNoOperatorBetweenSubExpressions)
{
    const auto prog = R"(
        tmp = 9 + 10 11 + 12
    )";
    expect_error(prog, "Expected operator between expressions");
}

TEST_F(CompilerTest, ErrorMsgTwoOperatorsBetweenSubExpressions)
{
    const auto prog = R"(
        tmp = 9 + 10 + + 11 + 12
    )";
    expect_error(prog, "Failed to match args for rule");
}

TEST_F(CompilerTest, ParseSimpleOps)
{
    const auto prog = R"(
        # arithmetic operators
        tmp = 1 + 2
        tmp = 1 - 2
        tmp = 1 * 2
        tmp = 1 / 2
        tmp = 1 % 2

        # conditional operators
        expect 1 < 2
        expect 1 <= 2
        expect 1 > 2 == 0
        expect 1 >= 2 == 0
        expect 2 == 2
        expect 1 != 2

        # logical operators
        expect 1 && 2
        expect 1 || 2
        expect !0

        # bitwise operators
        tmp = 1 & 2
        tmp = 1 | 2
        tmp = 1 ^ 2
        tmp = ~1
    )";
    expect_success(prog);
}

TEST_F(CompilerTest, ConsumeBuiltinFieldsAST)
{
    const auto prog     = R"(
        # integer numeric types
        : Byte
        : UInt8
        : Int8
        : UInt16LE
        : UInt16BE
        : Int16LE
        : Int16BE
        : UInt32LE
        : UInt32BE
        : Int32LE
        : Int32BE
        : UInt64LE
        : UInt64BE
        : Int64LE
        : Int64BE

        # float numeric types
        : Float16LE
        : Float16BE
        : Float32LE
        : Float32BE
        : Float64LE
        : Float64BE
        : BFloat16LE
        : BFloat16BE
    )";
    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.consume(cg.builtin_field(Symbol::BYTE)),
            cg.consume(cg.builtin_field(Symbol::U8)),
            cg.consume(cg.builtin_field(Symbol::I8)),
            cg.consume(cg.builtin_field(Symbol::U16LE)),
            cg.consume(cg.builtin_field(Symbol::U16BE)),
            cg.consume(cg.builtin_field(Symbol::I16LE)),
            cg.consume(cg.builtin_field(Symbol::I16BE)),
            cg.consume(cg.builtin_field(Symbol::U32LE)),
            cg.consume(cg.builtin_field(Symbol::U32BE)),
            cg.consume(cg.builtin_field(Symbol::I32LE)),
            cg.consume(cg.builtin_field(Symbol::I32BE)),
            cg.consume(cg.builtin_field(Symbol::U64LE)),
            cg.consume(cg.builtin_field(Symbol::U64BE)),
            cg.consume(cg.builtin_field(Symbol::I64LE)),
            cg.consume(cg.builtin_field(Symbol::I64BE)),
            cg.consume(cg.builtin_field(Symbol::F16LE)),
            cg.consume(cg.builtin_field(Symbol::F16BE)),
            cg.consume(cg.builtin_field(Symbol::F32LE)),
            cg.consume(cg.builtin_field(Symbol::F32BE)),
            cg.consume(cg.builtin_field(Symbol::F64LE)),
            cg.consume(cg.builtin_field(Symbol::F64BE)),
            cg.consume(cg.builtin_field(Symbol::BF16LE)),
            cg.consume(cg.builtin_field(Symbol::BF16BE)),
    });
    expect_ast(prog, expected);
}

TEST_F(CompilerTest, UnaryNegationAST)
{
    const auto prog = R"(
        tmp = 10 - - 11
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.assign(cg.var("tmp"), cg.sub(cg.num(10), cg.num(-11))),
    });
    expect_ast(prog, expected);
}

TEST_F(CompilerTest, SimpleArithmeticAST)
{
    const auto prog = R"(
       expect 1 + 2 == 3
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.expect(cg.eq(cg.add(cg.num(1), cg.num(2)), cg.num(3))),
    });
    expect_ast(prog, expected);
}

TEST_F(CompilerTest, ArrayAST)
{
    const auto prog = R"(
        len =  1 + 2
        : Byte[len]
        : Byte[]
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>(
            { cg.assign(cg.var("len"), cg.add(cg.num(1), cg.num(2))),
              cg.consume(
                      cg.array(cg.builtin_field(Symbol::BYTE), cg.var("len"))),
              cg.consume(cg.array(cg.builtin_field(Symbol::BYTE))) });

    expect_ast(prog, expected);
}

TEST_F(CompilerTest, RecordAST)
{
    const auto prog = R"(
        Record Entry() = {
            id: Int32LE,
        }
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({ cg.assign(
            cg.var("Entry"),
            cg.record(
                    ArgVec{},
                    ArgVec{ cg.assume(
                            cg.var("id"),
                            cg.builtin_field(Symbol::I32LE)) })) });
    expect_ast(prog, expected);
}

TEST_F(CompilerTest, AnonymousRecordAST)
{
    const auto prog = R"(
        : Record() {id: Int32LE, val: Int32LE}
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({ cg.consume(cg.record(
            ArgVec{},
            ArgVec{ cg.assume(cg.var("id"), cg.builtin_field(Symbol::I32LE)),
                    cg.assume(
                            cg.var("val"),
                            cg.builtin_field(Symbol::I32LE)) })) });
    expect_ast(prog, expected);
}

TEST_F(CompilerTest, ParenthesesOverridePrecedenceAST)
{
    const auto prog = R"(
        tmp = (1 - 2) * 3
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.assign(
                    cg.var("tmp"),
                    cg.mul(cg.sub(cg.num(1), cg.num(2)), cg.num(3))),
    });
    expect_ast(prog, expected);
}

TEST_F(CompilerTest, NestedParenthesesAST)
{
    const auto prog = R"(
        tmp = ((1 + 2) * (3 + 4))
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.assign(
                    cg.var("tmp"),
                    cg.mul(cg.add(cg.num(1), cg.num(2)),
                           cg.add(cg.num(3), cg.num(4)))),
    });
    expect_ast(prog, expected);
}

TEST_F(CompilerTest, ComplexArithmeticExpressionAST)
{
    const auto prog = R"(
        tmp = 1 + 2 * 3 - 4 / 2
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.assign(
                    cg.var("tmp"),
                    cg.sub(cg.add(cg.num(1), cg.mul(cg.num(2), cg.num(3))),
                           cg.div(cg.num(4), cg.num(2)))),
    });
    expect_ast(prog, expected);
}

TEST_F(CompilerTest, SimpleSaoAST)
{
    const auto prog = R"(
        # Star catalog entry (28 bytes)
        Record StarEntry() = {
            SRA0:  Float64LE,    # Right Ascension (radians)
            SDEC0: Float64LE,    # Declination (radians)
            ISP:   Bytes(2),     # Spectral type
            MAG:   Int16LE,      # Magnitude
            XRPM:  Float32LE,    # R.A. proper motion
            XDPM:  Float32LE     # Dec. proper motion
        }

        # File structure
        header: Bytes(28)
        stars: StarEntry[10]
    )";

    // TODO: Update this test once auto-sized arrays are supported
    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.assign(
                    cg.var("StarEntry"),
                    cg.record(
                            ArgVec{},
                            ArgVec{
                                    cg.assume(
                                            cg.var("SRA0"),
                                            cg.builtin_field(Symbol::F64LE)),
                                    cg.assume(
                                            cg.var("SDEC0"),
                                            cg.builtin_field(Symbol::F64LE)),
                                    cg.assume(
                                            cg.var("ISP"), cg.bytes(cg.num(2))),
                                    cg.assume(
                                            cg.var("MAG"),
                                            cg.builtin_field(Symbol::I16LE)),
                                    cg.assume(
                                            cg.var("XRPM"),
                                            cg.builtin_field(Symbol::F32LE)),
                                    cg.assume(
                                            cg.var("XDPM"),
                                            cg.builtin_field(Symbol::F32LE)),
                            })),
            cg.assume(cg.var("header"), cg.bytes(cg.num(28))),
            cg.assume(
                    cg.var("stars"), cg.array(cg.var("StarEntry"), cg.num(10))),
    });
    expect_ast(prog, expected);
}

// ============================================================================
// Semantic Analysis Tests
// ============================================================================

TEST_F(CompilerTest, UndefinedVar)
{
    const auto prog = R"(
        expect x
    )";
    expect_error(prog, "Undefined variable");
}

TEST_F(CompilerTest, UndefinedVarInExpression)
{
    const auto prog = R"(
        x = x + 1
    )";
    expect_error(prog, "Undefined variable");
}

TEST_F(CompilerTest, UndefinedRecordMemberVar)
{
    const auto prog = R"(
        Record Foo() = {
            x: Int32LE
        }
        foo: Foo
        expect x
   )";

    expect_error(prog, "Undefined variable");
}

TEST_F(CompilerTest, DefinedVar)
{
    const auto prog = R"(
        tmp = 1
        expect tmp
    )";

    // TODO: Update this once once variable references are supported in codegen.
    // This still shows us that the semantic analysis passes.
    expect_error(prog, "not yet supported");
}

TEST_F(CompilerTest, AssumeDefinesVar)
{
    const auto prog = R"(
        count: UInt8
        : Byte[count]
    )";

    // TODO: Update this once once variable references are supported in codegen.
    // This still shows us that the semantic analysis passes.
    expect_error(prog, "not yet supported");
}

TEST_F(CompilerTest, AssignLHSNotVar)
{
    const auto prog = R"(
        1 = 2
    )";
    expect_error(prog, "must be a variable");
}

TEST_F(CompilerTest, ConsumeNonFieldType)
{
    const auto prog = R"(
        : 42
    )";
    expect_error(prog, "field type");
}

TEST_F(CompilerTest, AssumeNonFieldType)
{
    const auto prog = R"(
        x: 42
    )";
    expect_error(prog, "field type");
}

TEST_F(CompilerTest, ArithmeticOnFieldType)
{
    const auto prog = R"(
        tmp = Int32LE + 1
    )";
    expect_error(prog, "numeric");
}

TEST_F(CompilerTest, ExpectFieldType)
{
    const auto prog = R"(
        expect Int32LE
    )";
    expect_error(prog, "numeric");
}

} // namespace openzl::sddl2::tests
