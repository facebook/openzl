// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl2/compiler/tests/CompilerTest.h"

namespace openzl::sddl2::tests {
namespace {
class OptimizerTest : public CompilerTest {};
} // namespace

TEST_F(OptimizerTest, ArithmeticConstFold)
{
    const auto prog     = R"(
        tmp = 1 + 2
        tmp = -(tmp + 2)
        tmp = 2 * 3 + tmp
        expect tmp == 1
    )";
    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.expect(cg.num(1)),
    });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, ComparisonConstFold)
{
    const auto prog     = R"(
        expect 5 > 3
        expect 5 >= 3
        expect 5 < 3
    )";
    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({ cg.expect(cg.num(1)),
                                                cg.expect(cg.num(1)),
                                                cg.expect(cg.num(0)) });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, LogicalConstFold)
{
    const auto prog     = R"(
        expect !0
        expect 1 && 0
        expect 1 || 0
    )";
    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({ cg.expect(cg.num(1)),
                                                cg.expect(cg.num(0)),
                                                cg.expect(cg.num(1)) });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, ConstPropagation)
{
    const auto prog     = R"(
        x = 5
        expect x + 1 == 6
    )";
    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.expect(cg.num(1)),
    });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, ChainedConstPropagation)
{
    const auto prog     = R"(
        a = 1
        b = 1 + a
        b = 2 * b
        expect b == 4
    )";
    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.expect(cg.num(1)),
    });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, SimpleArithmeticAST)
{
    const auto prog = R"(
       expect 1 + 2 == 3
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.expect(cg.num(1)),
    });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, ComplexArithmeticExpressionAST)
{
    const auto prog = R"(
        tmp = 1 + 2 * 3 - 4 / 2
        expect tmp == 5
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.expect(cg.num(1)),
    });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, ArrayAST)
{
    const auto prog = R"(
        len =  1 + 2
        : Byte[len]
        : Byte[]
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>(
            { cg.consume(cg.array(cg.builtin_field(Symbol::BYTE), cg.num(3))),
              cg.consume(cg.array(cg.builtin_field(Symbol::BYTE))) });

    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, RecordAST)
{
    const auto prog = R"(
        Record Entry() = {
            id: Int32LE,
        }
        : Entry
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({ cg.consume(cg.record(
            ArgVec{},
            ArgVec{ cg.assume(
                    cg.var("id"), cg.builtin_field(Symbol::I32LE)) })) });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, DivideByZeroError)
{
    const auto prog = R"(
        tmp = 2 - 2
        tmp = 1 / tmp
    )";
    expect_error(prog, "Division by zero");
}

TEST_F(OptimizerTest, ModuloByZeroError)
{
    const auto prog = R"(
        tmp = 2 - 2
        tmp = 1 % tmp
    )";
    expect_error(prog, "Modulo by zero");
}

TEST_F(OptimizerTest, DeadRecordVarElimination)
{
    const auto prog = R"(
        entry: Record() { id: Int32LE }
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.consume(cg.record(
                    ArgVec{},
                    ArgVec{ cg.assume(
                            cg.var("id"), cg.builtin_field(Symbol::I32LE)) })),
    });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, RecordMemberLastReference)
{
    const auto prog = R"(
        entry: Record() { id: Int32LE, val: Int32LE }
        expect entry.id == 0
        expect entry.val == 0
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.assume(
                    cg.var("entry"),
                    cg.record(
                            ArgVec{},
                            ArgVec{ cg.assume(
                                            cg.var("id"),
                                            cg.builtin_field(Symbol::I32LE)),
                                    cg.assume(
                                            cg.var("val"),
                                            cg.builtin_field(
                                                    Symbol::I32LE)) })),
            cg.expect(
                    cg.eq(cg.member(cg.var("entry"), cg.var("id")), cg.num(0))),
            cg.expect(
                    cg.eq(cg.member(cg.var("entry", true), cg.var("val")),
                          cg.num(0))),
    });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, TypeAliasResolution)
{
    const auto prog     = R"(
            MyType = Int32LE
            MyOtherType = MyType
            : MyType
        )";
    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>(
            { cg.consume(cg.builtin_field(Symbol::I32LE)) });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, TypeAliasInsideRecord)
{
    const auto prog     = R"(
            MyType = Int32LE
            entry: Record() { x: MyType }
        )";
    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({ cg.consume(cg.record(
            ArgVec{},
            ArgVec{ cg.assume(
                    cg.var("x"), cg.builtin_field(Symbol::I32LE)) })) });
    expect_ast(prog, expected);
}

} // namespace openzl::sddl2::tests
