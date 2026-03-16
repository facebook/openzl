// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl2/compiler/tests/CompilerTest.h"

namespace openzl::sddl2::tests {
namespace {
class OptimizerTest : public CompilerTest {
   protected:
    bool optimize() const override
    {
        return true;
    }
};
} // namespace

TEST_F(OptimizerTest, ArithmeticConstFold)
{
    const auto prog     = R"(
        a = 1 + 2
        b = -(a + 2)
        c = 2 * 3 + b
        expect c == 1
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
        c = 2 * b
        expect c == 4
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
    const auto expected = std::vector<ASTPtr>(
            { cg.assign(
                      cg.var("Entry"),
                      cg.record(
                              ArgVec{},
                              ArgVec{ cg.assume(
                                      cg.var("id"),
                                      cg.builtin_field(Symbol::I32LE)) })),
              cg.consume(cg.var("Entry", true)) });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, DivideByZeroError)
{
    const auto prog = R"(
        tmp = 2 - 2
        div = 1 / tmp
    )";
    expect_error(prog, "Division by zero");
}

TEST_F(OptimizerTest, ModuloByZeroError)
{
    const auto prog = R"(
        tmp = 2 - 2
        mod = 1 % tmp
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
        Record Foo() = {
            x: Int32LE,
            y: Int16LE
        }
        foo: Foo
        expect foo.x == 1
        expect foo.y == 2
    )";

    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>({
            cg.assign(
                    cg.var("Foo"),
                    cg.record(
                            ArgVec{},
                            ArgVec{ cg.assume(
                                            cg.var("x"),
                                            cg.builtin_field(Symbol::I32LE)),
                                    cg.assume(
                                            cg.var("y"),
                                            cg.builtin_field(
                                                    Symbol::I16LE)) })),
            cg.assume(cg.var("foo"), cg.var("Foo", true)),
            cg.expect(cg.eq(cg.member(cg.var("foo"), cg.var("x")), cg.num(1))),
            cg.expect(cg.eq(
                    cg.member(cg.var("foo", true), cg.var("y")), cg.num(2))),
    });
    expect_ast(prog, expected);
}

TEST_F(OptimizerTest, WhenConstant)
{
    const auto prog     = R"(
        when 1 {
            : Int32LE
        }
        when 0 {
            : Int16LE
        }
    )";
    const auto cg       = Codegen(SourceLocation::null());
    const auto expected = std::vector<ASTPtr>(
            { cg.consume(cg.builtin_field(Symbol::I32LE)) });
    expect_ast(prog, expected);
}

} // namespace openzl::sddl2::tests
