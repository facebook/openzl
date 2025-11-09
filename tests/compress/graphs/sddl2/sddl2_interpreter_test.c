// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "generated_test_bytecode.h"
#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"
#include "sddl2_test_framework.h"

/* ============================================================================
 * Interpreter-Specific Test Macros
 * ========================================================================= */

/**
 * Helper macro for tests that expect success (SDDL2_OK)
 *
 * Sets up segments, executes bytecode, asserts OK, provides cleanup.
 *
 * Example:
 *   EXPECT_SUCCESS(bytecode, size, input, input_size) {
 *       assert(segments.count == 1);
 *       assert(segments.items[0].tag == 100);
 *   }
 */
#define EXPECT_SUCCESS(bytecode_ptr, bytecode_len, input_ptr, input_len)  \
    SDDL2_segment_list segments;                                          \
    SDDL2_segment_list_init(&segments, NULL, NULL);                       \
    SDDL2_error err = SDDL2_execute_bytecode(                             \
            bytecode_ptr, bytecode_len, input_ptr, input_len, &segments); \
    assert(err == SDDL2_OK);                                              \
    do

/**
 * Helper macro for cleanup after EXPECT_SUCCESS
 *
 * Must be called after EXPECT_SUCCESS block to clean up resources.
 */
#define END_EXPECT_SUCCESS() \
    while (0)                \
        ;                    \
    SDDL2_segment_list_destroy(&segments)

/**
 * Helper macro for tests that expect a specific error
 *
 * Sets up segments, executes bytecode, asserts expected error, provides
 * cleanup.
 *
 * Example:
 *   EXPECT_ERROR(SDDL2_DIV_ZERO, bytecode, size, input, input_size);
 */
#define EXPECT_ERROR(                                                         \
        expected_err, bytecode_ptr, bytecode_len, input_ptr, input_len)       \
    do {                                                                      \
        SDDL2_segment_list segments;                                          \
        SDDL2_segment_list_init(&segments, NULL, NULL);                       \
        SDDL2_error err = SDDL2_execute_bytecode(                             \
                bytecode_ptr, bytecode_len, input_ptr, input_len, &segments); \
        assert(err == expected_err);                                          \
        SDDL2_segment_list_destroy(&segments);                                \
    } while (0)

/* ============================================================================
 * Test Definitions
 * ========================================================================= */

/**
 * Test: Execute simple program that creates one unspecified segment
 *
 * Assembly:
 *   push.i32 5
 *   segment.create_unspecified
 *   halt
 */
TEST(test_simple_segment_creation)
{
    uint8_t input[] = "Hello";

    EXPECT_SUCCESS(
            BYTECODE_TEST_SEGMENT_UNSPECIFIED,
            BYTECODE_TEST_SEGMENT_UNSPECIFIED_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 1);
        assert(segments.items[0].tag == 0);
        assert(segments.items[0].start_pos == 0);
        assert(segments.items[0].size_bytes == 5);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: Push zero and create zero-size segment
 */
TEST(test_zero_size_segment)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_SEGMENT_ZERO,
            BYTECODE_TEST_SEGMENT_ZERO_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 1);
        assert(segments.items[0].size_bytes == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: push.type opcode execution
 */
TEST(test_push_type_execution)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_PUSH_TYPE_EXECUTION,
            BYTECODE_TEST_PUSH_TYPE_EXECUTION_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: End-to-end test of push.type + segment.create_tagged
 */
TEST(test_push_type_with_segment_create_tagged)
{
    uint8_t input[12] = { 0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
                          0x00, 0x00, 0x03, 0x00, 0x00, 0x00 };

    EXPECT_SUCCESS(
            BYTECODE_TEST_PUSH_TYPE_WITH_SEGMENT_CREATE_TAGGED,
            BYTECODE_TEST_PUSH_TYPE_WITH_SEGMENT_CREATE_TAGGED_SIZE,
            input,
            sizeof(input))
    {
        assert(segments.count == 1);
        assert(segments.items[0].tag == 100);
        assert(segments.items[0].start_pos == 0);
        assert(segments.items[0].size_bytes == 12);
        assert(segments.items[0].type.kind == SDDL2_TYPE_I32LE);
        assert(segments.items[0].type.width == 1);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: Multiple typed segments with different types
 */
TEST(test_multiple_typed_segments)
{
    uint8_t input[5] = { 0x42, 0x00, 0x00, 0x80, 0x3F };

    EXPECT_SUCCESS(
            BYTECODE_TEST_MULTIPLE_TYPED_SEGMENTS,
            BYTECODE_TEST_MULTIPLE_TYPED_SEGMENTS_SIZE,
            input,
            sizeof(input))
    {
        assert(segments.count == 2);

        assert(segments.items[0].tag == 100);
        assert(segments.items[0].start_pos == 0);
        assert(segments.items[0].size_bytes == 1);
        assert(segments.items[0].type.kind == SDDL2_TYPE_U8);
        assert(segments.items[0].type.width == 1);

        assert(segments.items[1].tag == 100);
        assert(segments.items[1].start_pos == 1);
        assert(segments.items[1].size_bytes == 4);
        assert(segments.items[1].type.kind == SDDL2_TYPE_F32LE);
        assert(segments.items[1].type.width == 1);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: push.tag opcode execution
 */
TEST(test_push_tag_execution)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_PUSH_TAG_EXECUTION,
            BYTECODE_TEST_PUSH_TAG_EXECUTION_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: MATH operations through interpreter
 *
 * Tests: 10 + 5 = 15
 */
TEST(test_math_add_execution)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_MATH_ADD,
            BYTECODE_TEST_MATH_ADD_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: Multiple MATH operations
 *
 * Tests: (2 + 3) * 4 = 20
 */
TEST(test_math_combined_execution)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_MATH_COMBINED,
            BYTECODE_TEST_MATH_COMBINED_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: All 7 MATH operations
 *
 * Tests each MATH operation to verify dispatch works.
 * Note: We don't clean up intermediate results (would need stack.drop),
 * so the stack accumulates values, but this is fine for testing dispatch.
 */
TEST(test_math_all_operations)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_MATH_ALL_OPERATIONS,
            BYTECODE_TEST_MATH_ALL_OPERATIONS_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: MATH divide by zero error
 *
 * Tests that math.div with zero divisor returns SDDL2_DIV_ZERO error
 *
 * Assembly:
 *   push.i32 10
 *   push.i32 0
 *   math.div
 *   halt
 */
TEST(test_math_div_by_zero)
{
    uint8_t input[] = "Test";

    EXPECT_ERROR(
            SDDL2_DIV_ZERO,
            BYTECODE_TEST_MATH_DIV_BY_ZERO,
            BYTECODE_TEST_MATH_DIV_BY_ZERO_SIZE,
            input,
            sizeof(input) - 1);
}

/**
 * Test: MATH overflow error
 *
 * Tests that overflow is properly detected and propagated
 *
 * Assembly:
 *   push.i64 0x7FFFFFFFFFFFFFFF
 *   push.i64 1
 *   math.add
 *   halt
 */
TEST(test_math_overflow)
{
    uint8_t input[] = "Test";

    EXPECT_ERROR(
            SDDL2_STACK_OVERFLOW,
            BYTECODE_TEST_MATH_OVERFLOW,
            BYTECODE_TEST_MATH_OVERFLOW_SIZE,
            input,
            sizeof(input) - 1);
}

/**
 * Test: All 6 CMP operations
 *
 * Tests each CMP operation to verify dispatch works.
 * Stack comparison results (1=true, 0=false) accumulate for testing.
 */
TEST(test_cmp_all_operations)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_CMP_ALL,
            BYTECODE_TEST_CMP_ALL_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: CMP operations with false results
 *
 * Tests that comparisons correctly return 0 for false conditions
 */
TEST(test_cmp_false_results)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_CMP_FALSE_RESULTS,
            BYTECODE_TEST_CMP_FALSE_RESULTS_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: CMP operations with negative numbers
 *
 * Tests signed comparison behavior with negative values
 */
TEST(test_cmp_negative_numbers)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_CMP_NEGATIVE_NUMBERS,
            BYTECODE_TEST_CMP_NEGATIVE_NUMBERS_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: CMP with stack underflow (only 1 element)
 *
 * Tests that cmp.eq with only 1 element on stack returns underflow error
 *
 * Assembly:
 *   push.i32 10
 *   cmp.eq
 *   halt
 */
TEST(test_cmp_stack_underflow)
{
    uint8_t input[] = "Test";

    EXPECT_ERROR(
            SDDL2_STACK_UNDERFLOW,
            BYTECODE_TEST_CMP_STACK_UNDERFLOW,
            BYTECODE_TEST_CMP_STACK_UNDERFLOW_SIZE,
            input,
            sizeof(input) - 1);
}

/**
 * Test: CMP with type mismatch (non-I64 values)
 *
 * Tests that cmp.eq with Tag values returns type mismatch error
 *
 * Assembly:
 *   push.tag 100
 *   push.tag 200
 *   cmp.eq
 *   halt
 */
TEST(test_cmp_type_mismatch)
{
    uint8_t input[] = "Test";

    EXPECT_ERROR(
            SDDL2_TYPE_MISMATCH,
            BYTECODE_TEST_CMP_TYPE_MISMATCH,
            BYTECODE_TEST_CMP_TYPE_MISMATCH_SIZE,
            input,
            sizeof(input) - 1);
}

/**
 * Test: MATH with stack underflow (only 1 element)
 *
 * Tests that math.add with only 1 element on stack returns underflow error
 *
 * Assembly:
 *   push.i32 10
 *   math.add
 *   halt
 */
TEST(test_math_stack_underflow)
{
    uint8_t input[] = "Test";

    EXPECT_ERROR(
            SDDL2_STACK_UNDERFLOW,
            BYTECODE_TEST_MATH_STACK_UNDERFLOW,
            BYTECODE_TEST_MATH_STACK_UNDERFLOW_SIZE,
            input,
            sizeof(input) - 1);
}

/**
 * Test: MATH with type mismatch (non-I64 values)
 *
 * Tests that math.add with Type values returns type mismatch error
 *
 * Assembly:
 *   push.type.u8
 *   push.type.i32le
 *   math.add
 *   halt
 */
TEST(test_math_type_mismatch)
{
    uint8_t input[] = "Test";

    EXPECT_ERROR(
            SDDL2_TYPE_MISMATCH,
            BYTECODE_TEST_MATH_TYPE_MISMATCH,
            BYTECODE_TEST_MATH_TYPE_MISMATCH_SIZE,
            input,
            sizeof(input) - 1);
}

/**
 * Test: STACK drop operation
 *
 * Tests that stack.drop removes the top element
 */
TEST(test_stack_drop)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_STACK_DROP,
            BYTECODE_TEST_STACK_DROP_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: STACK dup operation
 *
 * Tests that stack.dup duplicates the top element
 */
TEST(test_stack_dup)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_STACK_DUP,
            BYTECODE_TEST_STACK_DUP_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: STACK swap operation
 *
 * Tests that stack.swap swaps the top two elements
 * Stack: a b -> b a
 */
TEST(test_stack_swap)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_STACK_SWAP,
            BYTECODE_TEST_STACK_SWAP_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: STACK operations with mixed types
 *
 * Tests that stack operations work with different value types (I64, Tag, Type)
 */
TEST(test_stack_operations_mixed_types)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_STACK_OPERATIONS_MIXED_TYPES,
            BYTECODE_TEST_STACK_OPERATIONS_MIXED_TYPES_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: STACK drop with underflow
 *
 * Tests that stack.drop on empty stack returns underflow error
 *
 * Assembly:
 *   stack.drop
 *   halt
 */
TEST(test_stack_drop_underflow)
{
    uint8_t input[] = "Test";

    EXPECT_ERROR(
            SDDL2_STACK_UNDERFLOW,
            BYTECODE_TEST_STACK_DROP_UNDERFLOW,
            BYTECODE_TEST_STACK_DROP_UNDERFLOW_SIZE,
            input,
            sizeof(input) - 1);
}

/**
 * Test: STACK dup with underflow
 *
 * Tests that stack.dup on empty stack returns underflow error
 *
 * Assembly:
 *   stack.dup
 *   halt
 */
TEST(test_stack_dup_underflow)
{
    uint8_t input[] = "Test";

    EXPECT_ERROR(
            SDDL2_STACK_UNDERFLOW,
            BYTECODE_TEST_STACK_DUP_UNDERFLOW,
            BYTECODE_TEST_STACK_DUP_UNDERFLOW_SIZE,
            input,
            sizeof(input) - 1);
}

/**
 * Test: STACK swap with underflow (only 1 element)
 *
 * Tests that stack.swap with only 1 element returns underflow error
 *
 * Assembly:
 *   push.i32 10
 *   stack.swap
 *   halt
 */
TEST(test_stack_swap_underflow)
{
    uint8_t input[] = "Test";

    EXPECT_ERROR(
            SDDL2_STACK_UNDERFLOW,
            BYTECODE_TEST_STACK_SWAP_UNDERFLOW,
            BYTECODE_TEST_STACK_SWAP_UNDERFLOW_SIZE,
            input,
            sizeof(input) - 1);
}

/**
 * Test: Invalid bytecode size
 *
 * NOTE: Uses hard-coded bytecode instead of assembler-generated
 * Reason: This test uses INTENTIONALLY MALFORMED/TRUNCATED bytecode to test
 * the interpreter's error handling of invalid bytecode structure. The byte
 * sequence { 0x01, 0x00, 0x03 } is incomplete and cannot be represented in
 * valid assembly syntax. This tests parser robustness, not valid programs.
 */
TEST(test_invalid_bytecode_size)
{
    uint8_t input[]    = "Test";
    uint8_t bytecode[] = { 0x01, 0x00, 0x03 };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err != SDDL2_OK);

    SDDL2_segment_list_destroy(&segments);
}

/**
 * Test: Program without halt (implicit halt)
 *
 * Tests that programs ending without an explicit halt instruction
 * are treated as successful (implicit halt behavior).
 *
 * This program performs valid operations but doesn't include a halt.
 * The interpreter should treat reaching the end as an implicit halt.
 *
 * Assembly (no explicit halt):
 *   push.i32 5
 *   segment.create_unspecified
 *
 * NOTE: Uses hard-coded bytecode instead of assembler-generated
 * Reason: This tests the implicit halt feature. If we used the assembler,
 * we couldn't be certain it doesn't automatically add a halt instruction.
 * Hand-crafted bytecode ensures we're testing the interpreter's implicit
 * halt behavior, not the assembler's behavior.
 */
TEST(test_missing_halt)
{
    uint8_t input[] = "Test5"; // 5 bytes to match segment size

    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x05, 0x00, 0x00, 0x00, // 5
        0x01, 0x00, 0x0C, 0x00  // segment.create_unspecified
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    // Should succeed with implicit halt
    assert(err == SDDL2_OK);

    // Verify the segment was created correctly
    assert(segments.count == 1);
    assert(segments.items[0].size_bytes == 5);

    SDDL2_segment_list_destroy(&segments);
}

int main(void)
{
    return sddl2_run_all_tests();
}
