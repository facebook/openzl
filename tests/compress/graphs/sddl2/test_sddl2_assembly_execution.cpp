// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "tests/compress/graphs/sddl2/utils.h"

#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"
#include "tools/sddl2/assembler/Assembler.h"

namespace openzl {
namespace sddl2 {
namespace testing {

class SDDL2AssemblyExecutionTest : public SDDL2TestBase {
   protected:
    void SetUp() override
    {
        SDDL2_Segment_list_init(&segments_, alloc_fn, alloc_ctx_);
    }

    void TearDown() override
    {
        SDDL2_Segment_list_destroy(&segments_);
    }

    SDDL2_Error run(const std::string& assembly, const std::string& input)
    {
        auto bytecode = assembler_.assemble(assembly);
        return SDDL2_execute_bytecode(
                bytecode.data(),
                bytecode.size(),
                input.data(),
                input.size(),
                &segments_);
    }
    Assembler assembler_;
    SDDL2_Segment_list segments_;
};

// ============================================================================
// Segment Creation Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, SimpleSegmentCreation)
{
    const std::string input = "Hello";
    ASSERT_EQ(
            run(R"(
                push.i32 5
                segment.create_unspecified
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 1u);
    EXPECT_EQ(segments_.items[0].tag, 0u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 5u);
}

TEST_F(SDDL2AssemblyExecutionTest, ZeroSizeSegment)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 0
                segment.create_unspecified
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 1u);
    EXPECT_EQ(segments_.items[0].size_bytes, 0u);
}

// ============================================================================
// Push Type Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, PushTypeExecution)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.type.u8
                push.type.i32le
                push.type.f64be
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, PushTypeWithSegmentCreateTagged)
{
    const std::string input(
            { 0x01,
              0x00,
              0x00,
              0x00,
              0x02,
              0x00,
              0x00,
              0x00,
              0x03,
              0x00,
              0x00,
              0x00 });
    ASSERT_EQ(
            run(R"(
                push.tag 100
                push.type.i32le
                push.u32 3
                segment.create_tagged
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 1u);
    EXPECT_EQ(segments_.items[0].tag, 100u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 12u);
    EXPECT_EQ(segments_.items[0].type.kind, SDDL2_TYPE_I32LE);
    EXPECT_EQ(segments_.items[0].type.width, 1u);
}

TEST_F(SDDL2AssemblyExecutionTest, MultipleSegments)
{
    // Input data: 12 bytes [01 02 03 04][05 06 07 08][09 0A 0B 0C]
    const std::string input(
            { 0x01,
              0x02,
              0x03,
              0x04,
              0x05,
              0x06,
              0x07,
              0x08,
              0x09,
              0x0A,
              0x0B,
              0x0C });
    ASSERT_EQ(
            run(R"(
                push.tag 100
                push.type.u8
                push.u32 4
                segment.create_tagged
                push.tag 200
                push.type.i32le
                push.u32 1
                segment.create_tagged
                push.i32 4
                segment.create_unspecified
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 3u);

    // Segment 1: U8 array, tag=100, 4 bytes starting at offset 0
    EXPECT_EQ(segments_.items[0].tag, 100u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 4u);
    EXPECT_EQ(segments_.items[0].type.kind, SDDL2_TYPE_U8);
    EXPECT_EQ(segments_.items[0].type.width, 1u);

    // Segment 2: I32LE scalar, tag=200, 4 bytes starting at offset 4
    EXPECT_EQ(segments_.items[1].tag, 200u);
    EXPECT_EQ(segments_.items[1].start_pos, 4u);
    EXPECT_EQ(segments_.items[1].size_bytes, 4u);
    EXPECT_EQ(segments_.items[1].type.kind, SDDL2_TYPE_I32LE);
    EXPECT_EQ(segments_.items[1].type.width, 1u);

    // Segment 3: Unspecified, tag=0, 4 bytes starting at offset 8
    EXPECT_EQ(segments_.items[2].tag, 0u);
    EXPECT_EQ(segments_.items[2].start_pos, 8u);
    EXPECT_EQ(segments_.items[2].size_bytes, 4u);
    EXPECT_EQ(segments_.items[2].type.kind, SDDL2_TYPE_BYTES);
}

TEST_F(SDDL2AssemblyExecutionTest, MultipleTypedSegments)
{
    const std::string input({ 0x42, 0x00, 0x00, (char)0x80, 0x3F });
    ASSERT_EQ(
            run(R"(
                push.tag 100
                push.type.u8
                push.u32 1
                segment.create_tagged
                push.tag 200
                push.type.f32le
                push.u32 1
                segment.create_tagged
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 2u);

    EXPECT_EQ(segments_.items[0].tag, 100u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 1u);
    EXPECT_EQ(segments_.items[0].type.kind, SDDL2_TYPE_U8);
    EXPECT_EQ(segments_.items[0].type.width, 1u);

    EXPECT_EQ(segments_.items[1].tag, 200u);
    EXPECT_EQ(segments_.items[1].start_pos, 1u);
    EXPECT_EQ(segments_.items[1].size_bytes, 4u);
    EXPECT_EQ(segments_.items[1].type.kind, SDDL2_TYPE_F32LE);
    EXPECT_EQ(segments_.items[1].type.width, 1u);
}

TEST_F(SDDL2AssemblyExecutionTest, PushTagExecution)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.tag 100
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

// ============================================================================
// MATH Operation Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, MathAddExecution)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 10
                push.i32 5
                math.add
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, MathCombinedExecution)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 2
                push.i32 3
                math.add
                push.i32 4
                math.mul
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, MathAllOperations)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 10
                push.i32 5
                math.add
                push.i32 20
                push.i32 8
                math.sub
                push.i32 3
                push.i32 4
                math.mul
                push.i32 20
                push.i32 4
                math.div
                push.i32 17
                push.i32 5
                math.mod
                push.i32 -42
                math.abs
                push.i32 10
                math.neg
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, MathDivByZero)
{
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                push.i32 10
                push.i32 0
                math.div
                halt
            )",
                input),
            SDDL2_DIV_ZERO);
}

TEST_F(SDDL2AssemblyExecutionTest, MathOverflow)
{
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                push.i64 0x7FFFFFFFFFFFFFFF
                push.i64 1
                math.add
                halt
            )",
                input),
            SDDL2_MATH_OVERFLOW);
}

TEST_F(SDDL2AssemblyExecutionTest, MathStackUnderflow)
{
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                push.i32 10
                math.add
                halt
            )",
                input),
            SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2AssemblyExecutionTest, MathTypeMismatch)
{
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                push.type.u8
                push.type.i32le
                math.add
                halt
            )",
                input),
            SDDL2_TYPE_MISMATCH);
}

// ============================================================================
// CMP Operation Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, CmpAllOperations)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 10
                push.i32 10
                cmp.eq
                push.i32 10
                push.i32 5
                cmp.ne
                push.i32 5
                push.i32 10
                cmp.lt
                push.i32 10
                push.i32 10
                cmp.le
                push.i32 10
                push.i32 5
                cmp.gt
                push.i32 10
                push.i32 10
                cmp.ge
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, CmpFalseResults)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 10
                push.i32 5
                cmp.eq
                push.i32 10
                push.i32 5
                cmp.lt
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, CmpNegativeNumbers)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 -10
                push.i32 5
                cmp.lt
                push.i32 5
                push.i32 -10
                cmp.gt
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, CmpStackUnderflow)
{
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                push.i32 10
                cmp.eq
                halt
            )",
                input),
            SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2AssemblyExecutionTest, CmpTypeMismatch)
{
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                push.tag 100
                push.tag 200
                cmp.eq
                halt
            )",
                input),
            SDDL2_TYPE_MISMATCH);
}

// ============================================================================
// STACK Operation Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, StackDrop)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 10
                push.i32 20
                stack.drop
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, StackDup)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 10
                stack.dup
                stack.drop
                stack.drop
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, StackSwap)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 10
                push.i32 20
                stack.swap
                stack.drop
                stack.drop
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, StackOperationsMixedTypes)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 10
                push.tag 100
                push.type.u8
                stack.dup
                stack.swap
                stack.drop
                stack.drop
                stack.drop
                stack.drop
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, StackDropUnderflow)
{
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                stack.drop
                halt
            )",
                input),
            SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2AssemblyExecutionTest, StackDupUnderflow)
{
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                stack.dup
                halt
            )",
                input),
            SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2AssemblyExecutionTest, StackSwapUnderflow)
{
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                push.i32 10
                stack.swap
                halt
            )",
                input),
            SDDL2_STACK_UNDERFLOW);
}

// ============================================================================
// Invalid Bytecode / Missing Halt Tests
// ============================================================================

/**
 * Uses hard-coded bytecode: intentionally malformed/truncated to test
 * interpreter error handling of invalid bytecode structure.
 */
TEST_F(SDDL2AssemblyExecutionTest, InvalidBytecodeSize)
{
    const std::string input = "Test";
    uint8_t bytecode[]      = { 0x01, 0x00, 0x03 };
    EXPECT_NE(
            SDDL2_execute_bytecode(
                    bytecode,
                    sizeof(bytecode),
                    input.data(),
                    input.size(),
                    &segments_),
            SDDL2_OK);
}

/**
 * Uses hand-crafted bytecode to ensure we test the interpreter's implicit
 * halt behavior, not the assembler's.
 *
 * Bytecode: push.i32 5, segment.create_unspecified (no halt)
 */
TEST_F(SDDL2AssemblyExecutionTest, MissingHalt)
{
    const std::string input = "Test5";
    uint8_t bytecode[]      = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x05, 0x00, 0x00, 0x00, // 5
        0x01, 0x00, 0x0C, 0x00  // segment.create_unspecified
    };
    ASSERT_EQ(
            SDDL2_execute_bytecode(
                    bytecode,
                    sizeof(bytecode),
                    input.data(),
                    input.size(),
                    &segments_),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 1u);
    EXPECT_EQ(segments_.items[0].size_bytes, 5u);
}

// ============================================================================
// type.fixed_array Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, TypeFixedArrayExecution)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.type.u32le
                push.i32 10
                type.fixed_array
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, TypeFixedArrayWithSegment)
{
    std::string input(40, '\0'); // mutable: modified below
    input[4]  = 0x01;
    input[8]  = 0x02;
    input[12] = 0x03;
    input[16] = 0x04;
    input[20] = 0x05;
    input[24] = 0x06;
    input[28] = 0x07;
    input[32] = 0x08;
    input[36] = 0x09;
    ASSERT_EQ(
            run(R"(
                push.tag 100
                push.type.u32le
                push.i32 10
                type.fixed_array
                push.i32 1
                segment.create_tagged
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 1u);
    EXPECT_EQ(segments_.items[0].tag, 100u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 40u);
    EXPECT_EQ(segments_.items[0].type.kind, SDDL2_TYPE_U32LE);
    EXPECT_EQ(segments_.items[0].type.width, 10u);
}

// ============================================================================
// type.structure Test
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, TypeStructureExecution)
{
    const std::string input(1, '\0');
    ASSERT_EQ(
            run(R"(
                push.type.u8
                push.type.i16le
                push.type.i32le
                push.i64 3
                type.structure
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

// ============================================================================
// push.current_pos Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, PushCurrentPosInitial)
{
    const std::string input = "Hello";
    ASSERT_EQ(
            run(R"(
                push.current_pos
                segment.create_unspecified
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 1u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 0u);
}

TEST_F(SDDL2AssemblyExecutionTest, PushCurrentPosAfterSegment)
{
    const std::string input = "HelloWorld";
    ASSERT_EQ(
            run(R"(
                push.tag 100
                push.type.bytes
                push.i32 5
                segment.create_tagged
                push.tag 200
                push.type.bytes
                push.current_pos
                segment.create_tagged
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 2u);

    EXPECT_EQ(segments_.items[0].tag, 100u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 5u);

    EXPECT_EQ(segments_.items[1].tag, 200u);
    EXPECT_EQ(segments_.items[1].start_pos, 5u);
    EXPECT_EQ(segments_.items[1].size_bytes, 5u);
}

TEST_F(SDDL2AssemblyExecutionTest, PushCurrentPosMultiple)
{
    const std::string input = "0123456789ABCDEF";
    ASSERT_EQ(
            run(R"(
                push.tag 100
                push.type.bytes
                push.i32 3
                segment.create_tagged
                push.tag 200
                push.type.bytes
                push.current_pos
                push.i32 2
                math.add
                segment.create_tagged
                push.tag 300
                push.type.bytes
                push.current_pos
                segment.create_tagged
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 3u);

    EXPECT_EQ(segments_.items[0].tag, 100u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 3u);

    EXPECT_EQ(segments_.items[1].tag, 200u);
    EXPECT_EQ(segments_.items[1].start_pos, 3u);
    EXPECT_EQ(segments_.items[1].size_bytes, 5u);

    EXPECT_EQ(segments_.items[2].tag, 300u);
    EXPECT_EQ(segments_.items[2].start_pos, 8u);
    EXPECT_EQ(segments_.items[2].size_bytes, 8u);
}

TEST_F(SDDL2AssemblyExecutionTest, PushCurrentPosArithmetic)
{
    const std::string input = "0123456789ABCDEFGHIJ";
    ASSERT_EQ(
            run(R"(
                push.tag 100
                push.type.bytes
                push.i32 10
                segment.create_tagged
                push.tag 200
                push.type.bytes
                push.current_pos
                push.i32 0
                math.sub
                segment.create_tagged
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 2u);

    EXPECT_EQ(segments_.items[0].tag, 100u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 10u);

    EXPECT_EQ(segments_.items[1].tag, 200u);
    EXPECT_EQ(segments_.items[1].start_pos, 10u);
    EXPECT_EQ(segments_.items[1].size_bytes, 10u);
}

TEST_F(SDDL2AssemblyExecutionTest, PushCurrentPosNoSideEffects)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 2
                segment.create_unspecified
                push.current_pos
                push.current_pos
                math.sub
                segment.create_unspecified
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 1u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 2u);
}

// ============================================================================
// push.remaining Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, PushRemainingInitial)
{
    const std::string input({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 });
    ASSERT_EQ(
            run(R"(
                push.remaining
                segment.create_unspecified
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 1u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 10u);
}

TEST_F(SDDL2AssemblyExecutionTest, PushRemainingAfterSegment)
{
    const std::string input({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 });
    ASSERT_EQ(
            run(R"(
                push.i32 5
                segment.create_unspecified
                push.remaining
                segment.create_unspecified
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 1u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 10u);
}

TEST_F(SDDL2AssemblyExecutionTest, PushRemainingMultiple)
{
    const std::string input({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 });
    EXPECT_EQ(
            run(R"(
                push.i32 3
                segment.create_unspecified
                push.remaining
                push.i32 5
                segment.create_unspecified
                push.remaining
                math.sub
                segment.create_unspecified
                halt
            )",
                input),
            SDDL2_SEGMENT_BOUNDS);
}

TEST_F(SDDL2AssemblyExecutionTest, PushRemainingNoSideEffects)
{
    const std::string input({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 });
    ASSERT_EQ(
            run(R"(
                push.i32 3
                segment.create_unspecified
                push.remaining
                push.remaining
                cmp.eq
                segment.create_unspecified
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 1u);
    EXPECT_EQ(segments_.items[0].start_pos, 0u);
    EXPECT_EQ(segments_.items[0].size_bytes, 4u);
}

// ============================================================================
// Logical Operations Error Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, LogicAndStackUnderflow)
{
    const std::string input(1, '\0');
    EXPECT_EQ(
            run(R"(
                push.i32 42
                logic.and
                halt
            )",
                input),
            SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2AssemblyExecutionTest, LogicOrEmptyStack)
{
    const std::string input(1, '\0');
    EXPECT_EQ(
            run(R"(
                logic.or
                halt
            )",
                input),
            SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2AssemblyExecutionTest, LogicXorTypeMismatch)
{
    const std::string input(1, '\0');
    EXPECT_EQ(
            run(R"(
                push.tag 100
                push.i32 42
                logic.xor
                halt
            )",
                input),
            SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2AssemblyExecutionTest, LogicNotEmptyStack)
{
    const std::string input(1, '\0');
    EXPECT_EQ(
            run(R"(
                logic.not
                halt
            )",
                input),
            SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2AssemblyExecutionTest, LogicNotTypeMismatch)
{
    const std::string input(1, '\0');
    EXPECT_EQ(
            run(R"(
                push.type.u8
                logic.not
                halt
            )",
                input),
            SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2AssemblyExecutionTest, LogicAllOperations)
{
    const std::string input(1, '\0');
    ASSERT_EQ(
            run(R"(
                push.i32 65280
                push.i32 4080
                logic.and
                push.i32 15
                logic.or
                push.i32 65535
                logic.xor
                logic.not
                stack.drop
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 0u);
}

// ============================================================================
// expect_true Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, ExpectTrueSuccess)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 1
                expect_true
                push.i32 42
                expect_true
                halt
            )",
                input),
            SDDL2_OK);
}

TEST_F(SDDL2AssemblyExecutionTest, ExpectTrueFailure)
{
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                push.i32 0
                expect_true
                halt
            )",
                input),
            SDDL2_VALIDATION_FAILED);
}

TEST_F(SDDL2AssemblyExecutionTest, ExpectTrueWithCmp)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 42
                push.i32 42
                cmp.eq
                expect_true
                halt
            )",
                input),
            SDDL2_OK);
}

TEST_F(SDDL2AssemblyExecutionTest, ExpectTrueWithTrace)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                trace.start # Start trace collection
                push.i32 150
                push.i32 200
                cmp.eq # Should record: "cmp.eq: 150 == 200 → 0"
                push.i32 5
                push.i32 10
                cmp.lt # Should record: "cmp.lt: 5 < 10 → 1"
                logic.or # Should record: "logic.or: 0 || 1 → 1"
                logic.not # Should record: "logic.not: !1 → 0"
                expect_true
                halt
            )",
                input),
            SDDL2_VALIDATION_FAILED);
}

TEST_F(SDDL2AssemblyExecutionTest, ExpectWithStack)
{
    // 5 < 10 → 1, 1 & 0 → 0 → expect_true fails
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                trace.start
                push.i32 100
                push.i32 200
                push.i32 5
                push.i32 10
                cmp.lt
                push.i32 0
                logic.and
                expect_true
                halt
            )",
                input),
            SDDL2_VALIDATION_FAILED);
}

// ============================================================================
// push.stack_depth Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, PushStackDepthEmpty)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.stack_depth
                push.i32 0
                cmp.eq
                expect_true
                halt
            )",
                input),
            SDDL2_OK);
}

TEST_F(SDDL2AssemblyExecutionTest, PushStackDepthTracking)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 10
                push.i32 20
                push.i32 30
                push.stack_depth
                push.i32 3
                cmp.eq
                expect_true
                halt
            )",
                input),
            SDDL2_OK);
}

TEST_F(SDDL2AssemblyExecutionTest, PushStackDepthArithmetic)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 1
                push.i32 2
                push.stack_depth
                push.i32 10
                math.mul
                push.i32 20
                cmp.eq
                expect_true
                halt
            )",
                input),
            SDDL2_OK);
}

// ============================================================================
// stack.drop_if Tests
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, StackDropIfTrue)
{
    // condition=1 (true) → drops 42
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 42
                push.i32 1
                stack.drop_if
                halt
            )",
                input),
            SDDL2_OK);
}

TEST_F(SDDL2AssemblyExecutionTest, StackDropIfFalse)
{
    // condition=0 (false) → keeps 42, then explicitly drop it
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 42
                push.i32 0
                stack.drop_if
                stack.drop
                halt
            )",
                input),
            SDDL2_OK);
}

TEST_F(SDDL2AssemblyExecutionTest, StackDropIfNonzero)
{
    // condition=42 (non-zero = true) → drops 100
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 100
                push.i32 42
                stack.drop_if
                halt
            )",
                input),
            SDDL2_OK);
}

TEST_F(SDDL2AssemblyExecutionTest, StackDropIfNegative)
{
    // condition=-1 (negative = true) → drops 50
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 50
                push.i32 -1
                stack.drop_if
                halt
            )",
                input),
            SDDL2_OK);
}

TEST_F(SDDL2AssemblyExecutionTest, StackDropIfUnderflow)
{
    // Only one value on stack, drop_if needs condition + value
    const std::string input = "Test";
    EXPECT_EQ(
            run(R"(
                push.i32 42
                stack.drop_if
                halt
            )",
                input),
            SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2AssemblyExecutionTest, StackDropIfWithCmp)
{
    // 10 > 5 → 1 (true) → drops 99
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.i32 99
                push.i32 10
                push.i32 5
                cmp.gt
                stack.drop_if
                halt
            )",
                input),
            SDDL2_OK);
}

// ============================================================================
// push.zero / Segment Zero Test
// ============================================================================

TEST_F(SDDL2AssemblyExecutionTest, SegmentZero)
{
    const std::string input = "Test";
    ASSERT_EQ(
            run(R"(
                push.zero
                segment.create_unspecified
                halt
            )",
                input),
            SDDL2_OK);
    EXPECT_EQ(segments_.count, 1u);
    EXPECT_EQ(segments_.items[0].size_bytes, 0u);
}

} // namespace testing
} // namespace sddl2
} // namespace openzl
