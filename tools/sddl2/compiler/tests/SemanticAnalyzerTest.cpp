// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl2/compiler/tests/CompilerTest.h"

namespace openzl::sddl2::tests {
namespace {
class SemanticAnalyzerTest : public CompilerTest {};
} // namespace

TEST_F(SemanticAnalyzerTest, UndefinedVar)
{
    const auto prog = R"(
        expect x
    )";
    expect_error(prog, "Undefined variable");
}

TEST_F(SemanticAnalyzerTest, UndefinedVarInExpression)
{
    const auto prog = R"(
        x = x + 1
    )";
    expect_error(prog, "Undefined variable");
}

TEST_F(SemanticAnalyzerTest, UndefinedRecordMemberVar)
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

TEST_F(SemanticAnalyzerTest, AssumeDefinesVar)
{
    const auto prog = R"(
        count: UInt8
        : Byte[count]
    )";

    // TODO: Update this once once variable references are supported in codegen.
    // This still shows us that the semantic analysis passes.
    expect_error(prog, "not yet supported");
}

TEST_F(SemanticAnalyzerTest, AssignLHSNotVar)
{
    const auto prog = R"(
        1 = 2
    )";
    expect_error(prog, "must be a variable");
}

TEST_F(SemanticAnalyzerTest, ConsumeNonFieldType)
{
    const auto prog = R"(
        : 42
    )";
    expect_error(prog, "field type");
}

TEST_F(SemanticAnalyzerTest, AssumeNonFieldType)
{
    const auto prog = R"(
        x: 42
    )";
    expect_error(prog, "field type");
}

TEST_F(SemanticAnalyzerTest, ArithmeticOnFieldType)
{
    const auto prog = R"(
        tmp = Int32LE + 1
    )";
    expect_error(prog, "numeric");
}

TEST_F(SemanticAnalyzerTest, ExpectFieldType)
{
    const auto prog = R"(
        expect Int32LE
    )";
    expect_error(prog, "numeric");
}

} // namespace openzl::sddl2::tests
