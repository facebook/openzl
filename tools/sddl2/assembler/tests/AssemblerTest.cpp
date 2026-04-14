// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <iomanip>
#include <string>

#include "tools/sddl2/assembler/Assembler.h"

using namespace testing;

namespace openzl::sddl2::tests {

class AssemblerTest : public Test {
   protected:
    void expect_success(std::string_view source)
    {
        EXPECT_NO_THROW(assembler_.assemble(source)) << "Source:\n" << source;
    }

    void expect_error(std::string_view source, std::string_view msg)
    {
        try {
            assembler_.assemble(source);
        } catch (const AssemblerError& ex) {
            EXPECT_NE(std::string{ ex.what() }.find(msg), std::string::npos)
                    << std::quoted(ex.what()) << "\nShould contain:\n  "
                    << std::quoted(msg) << "\nSource:\n"
                    << source;
            return;
        }
        EXPECT_TRUE(false) << "Should have thrown an AssemblerError!\nSource:\n"
                           << source;
    }

    Assembler assembler_;
};

// ── Empty / Minimal programs ────────────────────────────────────────────────

TEST_F(AssemblerTest, EmptyProgram)
{
    expect_success("# Empty program\n");
}

TEST_F(AssemblerTest, HaltOnly)
{
    expect_success("halt\n");
}

// ── Comments ────────────────────────────────────────────────────────────────

TEST_F(AssemblerTest, CommentsMixed)
{
    expect_success(R"(
        push.i32 10  # hash-style comment
        push.i32 20  ; semicolon-style comment
        halt
    )");
}

TEST_F(AssemblerTest, CommentsSemicolon)
{
    expect_success(R"(
        ; Test semicolon-style comments
        push.i32 42  ; inline semicolon comment
        push.i32 100 ; another inline comment
        halt         ; program termination
    )");
}

TEST_F(AssemblerTest, CommentsHash)
{
    expect_success(R"(
        # Test hash-style comments
        push.i32 42  # inline hash comment
        push.i32 100 # another inline comment
        halt          # program termination
    )");
}

// ── Push instructions ───────────────────────────────────────────────────────

TEST_F(AssemblerTest, PushI32Values)
{
    expect_success("push.i32 0\n      halt");
    expect_success("push.i32 42\n     halt");
    expect_success("push.i32 -1\n     halt");
    expect_success("push.i32 -100\n   halt");
    expect_success("push.i32 -0xFF\n  halt");
    expect_success("push.i32 2147483647\n  halt");
    expect_success("push.i32 -2147483648\n halt");
}

TEST_F(AssemblerTest, PushU32Values)
{
    expect_success("push.u32 0\n           halt");
    expect_success("push.u32 42\n          halt");
    expect_success("push.u32 255\n         halt");
    expect_success("push.u32 4294967295\n  halt");
    expect_success("push.u32 0x2A\n        halt");
}

TEST_F(AssemblerTest, PushI64Values)
{
    expect_success("push.i64 0\n                       halt");
    expect_success("push.i64 -1\n                      halt");
    expect_success("push.i64 1000000\n                 halt");
    expect_success("push.i64 9223372036854775807\n     halt");
    expect_success("push.i64 -9223372036854775808\n    halt");
}

TEST_F(AssemblerTest, PushZero)
{
    expect_success(R"(
        push.zero
        halt
    )");
}

TEST_F(AssemblerTest, PushTag)
{
    expect_success(R"(
        push.tag 100
        halt
    )");
}

// ── Push type instructions ──────────────────────────────────────────────────

TEST_F(AssemblerTest, PushTypeInteger)
{
    expect_success("push.type.u8\n     halt");
    expect_success("push.type.i8\n     halt");
    expect_success("push.type.u16le\n  halt");
    expect_success("push.type.u16be\n  halt");
    expect_success("push.type.i16le\n  halt");
    expect_success("push.type.i16be\n  halt");
    expect_success("push.type.u32le\n  halt");
    expect_success("push.type.u32be\n  halt");
    expect_success("push.type.i32le\n  halt");
    expect_success("push.type.i32be\n  halt");
    expect_success("push.type.u64le\n  halt");
    expect_success("push.type.u64be\n  halt");
    expect_success("push.type.i64le\n  halt");
    expect_success("push.type.i64be\n  halt");
}

TEST_F(AssemblerTest, PushTypeFloat)
{
    expect_success("push.type.f8\n      halt");
    expect_success("push.type.f16le\n   halt");
    expect_success("push.type.f16be\n   halt");
    expect_success("push.type.f32le\n   halt");
    expect_success("push.type.f32be\n   halt");
    expect_success("push.type.f64le\n   halt");
    expect_success("push.type.f64be\n   halt");
    expect_success("push.type.bf16le\n  halt");
    expect_success("push.type.bf16be\n  halt");
}

TEST_F(AssemblerTest, PushTypeBytes)
{
    expect_success(R"(
        push.type.bytes
        halt
    )");
}

// ── Stack instructions ──────────────────────────────────────────────────────

TEST_F(AssemblerTest, StackDup)
{
    expect_success(R"(
        push.u32 42
        stack.dup
        halt
    )");
}

TEST_F(AssemblerTest, StackDrop)
{
    expect_success(R"(
        push.u32 10
        push.u32 20
        stack.drop
        halt
    )");
}

TEST_F(AssemblerTest, StackSwap)
{
    expect_success(R"(
        push.u32 10
        push.u32 20
        stack.swap
        halt
    )");
}

TEST_F(AssemblerTest, StackOver)
{
    expect_success(R"(
        push.u32 10
        push.u32 20
        stack.over
        halt
    )");
}

TEST_F(AssemblerTest, StackRot)
{
    expect_success(R"(
        push.u32 10
        push.u32 20
        push.u32 30
        stack.rot
        halt
    )");
}

TEST_F(AssemblerTest, StackInstructionsOnly)
{
    expect_success(R"(
        stack.dup
        stack.drop
        stack.swap
        stack.over
        stack.rot
        halt
    )");
}

TEST_F(AssemblerTest, StackCombined)
{
    expect_success(R"(
        push.u32 1
        push.u32 2
        push.u32 3
        stack.swap
        stack.over
        stack.rot
        stack.dup
        stack.drop
        halt
    )");
}

// ── Basic operations ────────────────────────────────────────────────────────

TEST_F(AssemblerTest, MathAdd)
{
    expect_success(R"(
        push.i32 10
        push.i32 20
        math.add
        halt
    )");
}

TEST_F(AssemblerTest, MathSub)
{
    expect_success(R"(
        push.i32 30
        push.i32 10
        math.sub
        halt
    )");
}

TEST_F(AssemblerTest, MathMul)
{
    expect_success(R"(
        push.i32 5
        push.i32 7
        math.mul
        halt
    )");
}

TEST_F(AssemblerTest, MathDiv)
{
    expect_success(R"(
        push.i32 20
        push.i32 5
        math.div
        halt
    )");
}

TEST_F(AssemblerTest, MathMod)
{
    expect_success(R"(
        push.i32 17
        push.i32 5
        math.mod
        halt
    )");
}

TEST_F(AssemblerTest, MathAbs)
{
    expect_success(R"(
        push.i32 -100
        math.abs
        halt
    )");
    expect_success(R"(
        push.i32 42
        math.abs
        halt
    )");
}

TEST_F(AssemblerTest, MathNeg)
{
    expect_success(R"(
        push.i32 42
        math.neg
        halt
    )");
    expect_success(R"(
        push.i32 -50
        math.neg
        halt
    )");
}

TEST_F(AssemblerTest, CmpOps)
{
    expect_success(R"(
        push.i32 10
        push.i32 20
        cmp.eq
        stack.drop
        push.i32 10
        push.i32 20
        cmp.ne
        stack.drop
        push.i32 10
        push.i32 20
        cmp.lt
        stack.drop
        push.i32 10
        push.i32 20
        cmp.le
        stack.drop
        push.i32 10
        push.i32 20
        cmp.gt
        stack.drop
        push.i32 10
        push.i32 5
        cmp.ge
        stack.drop
        halt
    )");
}

TEST_F(AssemblerTest, LogicOps)
{
    expect_success(R"(
        push.i32 0xFF00
        push.i32 0x0FF0
        logic.and
        stack.drop
        push.i32 0x00F0
        push.i32 0x0F00
        logic.or
        stack.drop
        push.i32 0xAAAA
        push.i32 0x5555
        logic.xor
        stack.drop
        push.i32 0x0F0F
        logic.not
        stack.drop
        halt
    )");
}

TEST_F(AssemblerTest, BitOps)
{
    expect_success(R"(
        push.i32 0xFF00
        push.i32 0x0FF0
        math.bit_and
        stack.drop
        push.i32 0x00F0
        push.i32 0x0F00
        math.bit_or
        stack.drop
        push.i32 0xAAAA
        push.i32 0x5555
        math.bit_xor
        stack.drop
        push.i32 0x0F0F
        math.bit_not
        stack.drop
        halt
    )");
}

TEST_F(AssemblerTest, LoadOps)
{
    expect_success("load.u8\n     halt");
    expect_success("load.i8\n     halt");
    expect_success("load.u16le\n  halt");
    expect_success("load.i16le\n  halt");
    expect_success("load.u32le\n  halt");
    expect_success("load.i32le\n  halt");
    expect_success("load.i64le\n  halt");
    expect_success("load.u16be\n  halt");
    expect_success("load.i16be\n  halt");
    expect_success("load.u32be\n  halt");
    expect_success("load.i32be\n  halt");
    expect_success("load.i64be\n  halt");
}

// ── Segment instructions ────────────────────────────────────────────────────

TEST_F(AssemblerTest, SegmentCreateUnspecified)
{
    expect_success(R"(
        push.i32 5
        segment.create_unspecified
        halt
    )");
}

TEST_F(AssemblerTest, SegmentCreateTagged)
{
    expect_success(R"(
        push.tag 100
        push.type.u8
        push.i32 5
        segment.create_tagged
        halt
    )");
}

// ── Type instructions ───────────────────────────────────────────────────────

TEST_F(AssemblerTest, TypeFixedArray)
{
    expect_success(R"(
        push.type.u32le
        push.i32 10
        type.fixed_array
        halt
    )");
}

TEST_F(AssemblerTest, TypeFixedArrayNested)
{
    expect_success(R"(
        push.type.i16le
        push.i32 5
        type.fixed_array
        push.i32 3
        type.fixed_array
        halt
    )");
}

// ── Expect true ─────────────────────────────────────────────────────────────

TEST_F(AssemblerTest, ExpectTrue)
{
    expect_success(R"(
        push.i32 1
        expect_true
        push.i32 123456
        expect_true
        push.i32 -1
        expect_true
        push.i32 -999999
        expect_true
        halt
    )");
}

TEST_F(AssemblerTest, ExpectTrueWithCmp)
{
    expect_success(R"(
        push.i32 42
        cmp.eq
        expect_true
        push.i32 10
        push.i32 20
        cmp.ne
        expect_true
        push.i32 5
        push.i32 10
        cmp.lt
        expect_true
        halt
    )");
}

// ── Error cases ─────────────────────────────────────────────────────────────

TEST_F(AssemblerTest, ErrorPushI32MissingParam)
{
    expect_error("push.i32\n halt", "Couldn't parse integer literal");
}

TEST_F(AssemblerTest, ErrorPushI32Overflow)
{
    expect_error("push.i32 4294967296\n halt", "out of range");
}

TEST_F(AssemblerTest, ErrorPushI32OverflowLarge)
{
    expect_error("push.i32 99999999999\n halt", "out of range");
}

TEST_F(AssemblerTest, ErrorPushI32Underflow)
{
    expect_error("push.i32 -2147483649\n halt", "out of range");
}

TEST_F(AssemblerTest, ErrorPushU32MissingParam)
{
    expect_error("push.u32\n halt", "Couldn't parse integer literal");
}

TEST_F(AssemblerTest, ErrorPushU32Negative)
{
    expect_error("push.u32 -1\n halt", "out of range");
}

TEST_F(AssemblerTest, ErrorPushU32Overflow)
{
    expect_error("push.u32 4294967296\n halt", "out of range");
}

TEST_F(AssemblerTest, ErrorPushI64MissingParam)
{
    expect_error("push.i64\n halt", "Couldn't parse integer literal");
}

TEST_F(AssemblerTest, ErrorPushI64Overflow)
{
    expect_error("push.i64 9223372036854775808\n halt", "out of range");
}

TEST_F(AssemblerTest, ErrorPushTagMissingParam)
{
    expect_error("push.tag\n halt", "Couldn't parse integer literal");
}

TEST_F(AssemblerTest, ErrorPushTypeInvalidName)
{
    expect_error("push.type.invalid_type\n halt", "Unknown instruction");
}

TEST_F(AssemblerTest, ErrorPushTypeTypo)
{
    expect_error("push.type.u32el\n halt", "Unknown instruction");
}

TEST_F(AssemblerTest, ErrorInvalidLiteral)
{
    expect_error("push.u32 xyz\n halt", "Couldn't parse integer literal");
}

TEST_F(AssemblerTest, ErrorUnsupportedLoadFloat)
{
    expect_error("push.i32 0\n load.f32le\n halt", "Unknown instruction");
}

TEST_F(AssemblerTest, ErrorUnsupportedLoadU64)
{
    expect_error("push.i32 0\n load.u64le\n halt", "Unknown instruction");
}

TEST_F(AssemblerTest, ErrorNoParamInstructionsWithParam)
{
    expect_error(
            "push.i32 10\n push.i32 20\n cmp.eq 5\n halt",
            "Unknown instruction");
    expect_error(
            "push.i32 10\n push.i32 20\n math.add 5\n halt",
            "Unknown instruction");
    expect_error("push.i32 10\n logic.not 5\n halt", "Unknown instruction");
    expect_error("stack.dup 42\n halt", "Unknown instruction");
    expect_error("push.u32 10\n stack.drop 5\n halt", "Unknown instruction");
    expect_error(
            "push.u32 1\n push.u32 2\n stack.swap 1\n halt",
            "Unknown instruction");
    expect_error(
            "push.u32 1\n push.u32 2\n stack.over 2\n halt",
            "Unknown instruction");
    expect_error(
            "push.u32 1\n push.u32 2\n push.u32 3\n stack.rot 3\n halt",
            "Unknown instruction");
}

} // namespace openzl::sddl2::tests
