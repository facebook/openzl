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
    expect_success(prog);
}

TEST_F(SemanticAnalyzerTest, AssignLHSNotVar)
{
    const auto prog = R"(
        1 = 2
    )";
    expect_error(prog, "variable name");
}

TEST_F(SemanticAnalyzerTest, RedefineVar)
{
    const auto prog = R"(
        x = 5
        x = 6
    )";
    expect_error(prog, "already defined");
}

TEST_F(SemanticAnalyzerTest, RedefineAssumedVar)
{
    const auto prog = R"(
        x: Int32LE
        x: Int32LE
    )";
    expect_error(prog, "already defined");
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

TEST_F(SemanticAnalyzerTest, AssumeRecordAndMemberAccess)
{
    const auto prog = R"(
        entry: Record() { id: Int32LE }
        expect entry.id == 0
    )";

    expect_success(prog);
}

TEST_F(SemanticAnalyzerTest, MemberAccessOnNonRecord)
{
    const auto prog = R"(
        x: Int32LE
        expect x.id == 0
    )";
    expect_error(prog, "not a record");
}

TEST_F(SemanticAnalyzerTest, MemberAccessUndefinedField)
{
    const auto prog = R"(
        entry: Record() { id: Int32LE }
        expect entry.nonexistent == 0
    )";
    expect_error(prog, "not a valid record field");
}

TEST_F(SemanticAnalyzerTest, MemberAccessUndefinedVar)
{
    const auto prog = R"(
        expect entry.id == 0
    )";
    expect_error(prog, "Undefined variable");
}

TEST_F(SemanticAnalyzerTest, AssumeRecordTypeAlias)
{
    const auto prog = R"(
        Record Entry() = {
            id: Int32LE,
            val: Int32LE,
            bytes: Byte[4]
        }
        entry: Entry
        expect entry.id == 0
        expect entry.val == 0
    )";
    expect_success(prog);
}

TEST_F(SemanticAnalyzerTest, AssumeBuiltinTypeAlias)
{
    const auto prog = R"(
        MyType = Int32LE
        x: MyType
        expect x == 0
    )";
    expect_success(prog);
}

TEST_F(SemanticAnalyzerTest, AssumeFloatType)
{
    const auto prog = R"(
        x: Float32LE
        expect x == 0
    )";
    expect_error(prog, "numeric expression");
}

TEST_F(SemanticAnalyzerTest, AssumeFloatTypeAlias)
{
    const auto prog = R"(
        MyFloat = Float32LE
        x: MyFloat
        expect x == 0
    )";
    expect_error(prog, "numeric expression");
}

TEST_F(SemanticAnalyzerTest, AssumeChainedTypeAlias)
{
    const auto prog = R"(
        MyInt = Int32LE
        MyType = MyInt
        x: MyType
        expect x == 0
    )";
    expect_success(prog);
}

TEST_F(SemanticAnalyzerTest, NestedMemberAccess)
{
    const auto prog = R"(
        Record Foo() = {
            x: Int32LE
        }
        Record Bar() = {
            foo: Foo,
            y: Int16LE
        }
        bar: Bar
        expect bar.foo.x == 1
    )";

    expect_success(prog);
}

TEST_F(SemanticAnalyzerTest, NestedMemberAccessOnNonRecordField)
{
    const auto prog = R"(
        Record Bar() = {
            y: Int16LE
        }
        bar: Bar
        expect bar.y.z == 0
    )";
    expect_error(prog, "not a record");
}

TEST_F(SemanticAnalyzerTest, ParameterizedRecordWrongArgCount)
{
    const auto prog = R"(
        Record Entry(N) = {
            items: Int32LE[N]
        }
        : Entry(1, 2)
    )";
    expect_error(prog, "Expected 1 arguments but got 2");
}

TEST_F(SemanticAnalyzerTest, ParameterizedRecordNonNumericArg)
{
    const auto prog = R"(
        Record Entry(N) = {
            items: Int32LE[N]
        }
        : Entry(Int32LE)
    )";
    expect_error(prog, "numeric");
}

TEST_F(SemanticAnalyzerTest, ParameterizedRecordCallNonRecord)
{
    const auto prog = R"(
        MyType = Int32LE
        : MyType(10)
    )";
    expect_error(prog, "record type");
}

TEST_F(SemanticAnalyzerTest, WhenBlockWithNonNumericCondition)
{
    const auto prog = R"(
        when Int32LE {
            : Int32LE
        }
    )";
    expect_error(prog, "numeric");
}

TEST_F(SemanticAnalyzerTest, WhenBlockWithUndefinedVarInCondition)
{
    const auto prog = R"(
        when undefined_var == 1 {
            : Int32LE
        }
    )";
    expect_error(prog, "Undefined variable");
}

TEST_F(SemanticAnalyzerTest, NestedWhenBlocks)
{
    const auto prog = R"(
        a: UInt8
        b: UInt8
        when a == 1 {
            when b == 2 {
                : Int32LE
            }
        }
    )";

    expect_success(prog);
}

TEST_F(SemanticAnalyzerTest, WhenBlockInRecord)
{
    const auto prog = R"(
        Record Data(flags) = {
            base: UInt32LE,
            when flags == 1 {
                optional: UInt16LE
            }
        }
        : Data(1)
    )";

    expect_success(prog);
}

TEST_F(SemanticAnalyzerTest, WhenBlockInRecordWithUndefinedParam)
{
    const auto prog = R"(
        Record Data(flags) = {
            base: UInt32LE,
            when undefined_param == 1 {
                optional: UInt16LE
            }
        }
        : Data(1)
    )";
    expect_error(prog, "Undefined variable");
}

TEST_F(SemanticAnalyzerTest, WhenBlockInRecordWithFieldReference)
{
    const auto prog = R"(
        Record Data() = {
            flags: UInt8,
            when flags == 1 {
                optional: UInt16LE
            }
        }
        : Data
    )";

    expect_error(prog, "Undefined variable");
}

TEST_F(SemanticAnalyzerTest, AbsFieldType)
{
    const auto prog = R"(
        tmp = abs(Int32LE)
    )";
    expect_error(prog, "numeric");
}

TEST_F(SemanticAnalyzerTest, MemberAccessOnConditionalField)
{
    const auto prog = R"(
        Record Data(flags) = {
            when flags {
                optional: Int32LE
            }
        }
        data: Data(1)
        expect data.optional == 0
    )";
    expect_error(prog, "access not supported");
}

TEST_F(SemanticAnalyzerTest, MemberAccessOnNonConditionalField)
{
    const auto prog = R"(
        Record Data(flags) = {
            when flags {
                optional: Int32LE
            },
            present: Int32LE
        }
        data: Data(1)
        expect data.present == 0
    )";

    expect_success(prog);
}

TEST_F(SemanticAnalyzerTest, IntrinsicRemIsNumeric)
{
    const auto prog = R"(
        expect _rem >= 0
    )";
    expect_success(prog);
}

TEST_F(SemanticAnalyzerTest, IntrinsicRemCannotBeAssigned)
{
    const auto prog = R"(
        _rem = 42
    )";
    expect_error(prog, "Cannot assign to built-in variable");
}

TEST_F(SemanticAnalyzerTest, IntrinsicRemCannotBeAssumed)
{
    const auto prog = R"(
        _rem: Int32LE
    )";
    expect_error(prog, "Cannot assign to built-in variable");
}

} // namespace openzl::sddl2::tests
