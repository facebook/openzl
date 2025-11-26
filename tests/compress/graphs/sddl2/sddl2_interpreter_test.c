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
    SDDL2_Segment_list segments;                                          \
    SDDL2_Segment_list_init(&segments, NULL, NULL);                       \
    SDDL2_Error err = SDDL2_execute_bytecode(                             \
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
    SDDL2_Segment_list_destroy(&segments)

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
        SDDL2_Segment_list segments;                                          \
        SDDL2_Segment_list_init(&segments, NULL, NULL);                       \
        SDDL2_Error err = SDDL2_execute_bytecode(                             \
                bytecode_ptr, bytecode_len, input_ptr, input_len, &segments); \
        assert(err == expected_err);                                          \
        SDDL2_Segment_list_destroy(&segments);                                \
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

        assert(segments.items[1].tag == 200); // Different tag!
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
            SDDL2_MATH_OVERFLOW,
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

    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);

    SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err != SDDL2_OK);

    SDDL2_Segment_list_destroy(&segments);
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

    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);

    SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    // Should succeed with implicit halt
    assert(err == SDDL2_OK);

    // Verify the segment was created correctly
    assert(segments.count == 1);
    assert(segments.items[0].size_bytes == 5);

    SDDL2_Segment_list_destroy(&segments);
}

/**
 * Test: type.fixed_array execution
 *
 * Tests that type.fixed_array opcode works through the interpreter
 *
 * Assembly:
 *   push.type.u32le
 *   type.fixed_array 10
 *   halt
 */
TEST(test_type_fixed_array_execution)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_TYPE_FIXED_ARRAY_EXECUTION,
            BYTECODE_TEST_TYPE_FIXED_ARRAY_EXECUTION_SIZE,
            input,
            sizeof(input) - 1)
    {
        assert(segments.count == 0); // No segments created, just type on stack
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: End-to-end test of type.fixed_array + segment.create_tagged
 *
 * Tests that array types work correctly with segment creation.
 * Creates a segment with U32LE[10] type, which should be 40 bytes total.
 *
 * Assembly:
 *   push.tag 100
 *   push.type.u32le
 *   type.fixed_array 10
 *   push.i32 10
 *   segment.create_tagged
 *   halt
 */
TEST(test_type_fixed_array_with_segment)
{
    // 40 bytes of input data (10 * 4 bytes per U32LE)
    uint8_t input[40] = { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                          0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
                          0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
                          0x06, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
                          0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00 };

    // Execute bytecode and print error if it fails
    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Error err = SDDL2_execute_bytecode(
            BYTECODE_TEST_TYPE_FIXED_ARRAY_WITH_SEGMENT,
            BYTECODE_TEST_TYPE_FIXED_ARRAY_WITH_SEGMENT_SIZE,
            input,
            sizeof(input),
            &segments);

    if (err != SDDL2_OK) {
        printf("ERROR: SDDL2_execute_bytecode returned error code: %d\n", err);
        printf("Input size: %zu, expected segment size: 40\n", sizeof(input));
    }
    assert(err == SDDL2_OK);

    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 40); // 1 element × 10 × 4 bytes
    assert(segments.items[0].type.kind == SDDL2_TYPE_U32LE);
    assert(segments.items[0].type.width == 10); // Array of 10 elements

    SDDL2_Segment_list_destroy(&segments);
}

/* ============================================================================
 * Test: type.structure execution
 * ========================================================================= */

TEST(test_type_structure_execution)
{
    // This test verifies that type.structure works through the complete
    // pipeline: Assembly -> Bytecode -> Interpreter -> Type on stack
    //
    // The test program (test_type_structure.asm):
    //   push.type.u8
    //   push.type.i16le
    //   push.type.i32le
    //   push.i64 3
    //   type.structure
    //   halt
    //
    // Expected: Creates structure type {U8, I16LE, I32LE} with total size 7
    // bytes

    uint8_t input[1] = { 0 }; // Dummy input (not used in this test)

    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Error err = SDDL2_execute_bytecode(
            BYTECODE_TEST_TYPE_STRUCTURE,
            BYTECODE_TEST_TYPE_STRUCTURE_SIZE,
            input,
            sizeof(input),
            &segments);

    if (err != SDDL2_OK) {
        printf("ERROR: type.structure bytecode execution failed with error: %d\n",
               err);
    }
    assert(err == SDDL2_OK);

    // No segments should be created (test just creates a type on stack)
    assert(segments.count == 0);

    SDDL2_Segment_list_destroy(&segments);
}

/* ============================================================================
 * push.current_pos Tests
 * ========================================================================= */

/**
 * Test: push.current_pos at buffer start
 *
 * Verifies that push.current_pos returns 0 when called at the beginning
 * of buffer before any segments are created.
 *
 * Assembly:
 *   push.current_pos
 *   segment.create_unspecified
 *   halt
 */
TEST(test_push_current_pos_initial)
{
    uint8_t input[] = "Hello";

    EXPECT_SUCCESS(
            BYTECODE_TEST_PUSH_CURRENT_POS_INITIAL,
            BYTECODE_TEST_PUSH_CURRENT_POS_INITIAL_SIZE,
            input,
            sizeof(input) - 1)
    {
        // Should create one segment from position 0
        assert(segments.count == 1);
        assert(segments.items[0].start_pos == 0);
        assert(segments.items[0].size_bytes
               == 0); // Current pos (0) was used as size
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: push.current_pos after creating segment
 *
 * Verifies that push.current_pos correctly returns the updated cursor
 * position after a segment advances it. Uses tagged segments to prevent
 * automatic merging.
 *
 * Assembly:
 *   push.tag 100
 *   push.type.bytes
 *   push.i32 5
 *   segment.create_tagged
 *   push.tag 200
 *   push.type.bytes
 *   push.current_pos
 *   segment.create_tagged
 *   halt
 */
TEST(test_push_current_pos_after_segment)
{
    uint8_t input[] = "HelloWorld";

    EXPECT_SUCCESS(
            BYTECODE_TEST_PUSH_CURRENT_POS_AFTER_SEGMENT,
            BYTECODE_TEST_PUSH_CURRENT_POS_AFTER_SEGMENT_SIZE,
            input,
            sizeof(input) - 1)
    {
        // Should create two segments with different tags
        assert(segments.count == 2);

        // First segment: tag=100, 5 bytes from position 0
        assert(segments.items[0].tag == 100);
        assert(segments.items[0].start_pos == 0);
        assert(segments.items[0].size_bytes == 5);

        // Second segment: tag=200, 5 bytes from position 5 (current_pos was 5)
        assert(segments.items[1].tag == 200);
        assert(segments.items[1].start_pos == 5);
        assert(segments.items[1].size_bytes == 5);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: push.current_pos with multiple segments
 *
 * Verifies cursor position tracking across multiple segment creations
 * and arithmetic operations. Uses tagged segments to prevent merging.
 *
 * Assembly:
 *   push.tag 100
 *   push.type.bytes
 *   push.i32 3
 *   segment.create_tagged
 *   push.tag 200
 *   push.type.bytes
 *   push.current_pos
 *   push.i32 2
 *   math.add
 *   segment.create_tagged
 *   push.tag 300
 *   push.type.bytes
 *   push.current_pos
 *   segment.create_tagged
 *   halt
 */
TEST(test_push_current_pos_multiple)
{
    uint8_t input[] = "0123456789ABCDEF";

    EXPECT_SUCCESS(
            BYTECODE_TEST_PUSH_CURRENT_POS_MULTIPLE,
            BYTECODE_TEST_PUSH_CURRENT_POS_MULTIPLE_SIZE,
            input,
            sizeof(input) - 1)
    {
        // Should create three segments with different tags
        assert(segments.count == 3);

        // First segment: tag=100, 3 bytes from position 0
        assert(segments.items[0].tag == 100);
        assert(segments.items[0].start_pos == 0);
        assert(segments.items[0].size_bytes == 3);

        // Second segment: tag=200, 5 bytes from position 3 (current_pos=3,
        // added 2)
        assert(segments.items[1].tag == 200);
        assert(segments.items[1].start_pos == 3);
        assert(segments.items[1].size_bytes == 5);

        // Third segment: tag=300, 8 bytes from position 8 (size = current_pos
        // value)
        assert(segments.items[2].tag == 300);
        assert(segments.items[2].start_pos == 8);
        assert(segments.items[2].size_bytes == 8);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: push.current_pos for arithmetic operations
 *
 * Demonstrates using cursor position for calculating bytes consumed.
 * Tests the pattern of: end_pos - start_pos = bytes_consumed
 * Uses tagged segments to prevent merging.
 *
 * Assembly:
 *   push.tag 100
 *   push.type.bytes
 *   push.i32 10
 *   segment.create_tagged
 *   push.current_pos
 *   push.i32 0
 *   math.sub
 *   push.tag 200
 *   push.type.bytes
 *   stack.swap
 *   segment.create_tagged
 *   halt
 */
TEST(test_push_current_pos_arithmetic)
{
    uint8_t input[] = "0123456789ABCDEFGHIJ";

    EXPECT_SUCCESS(
            BYTECODE_TEST_PUSH_CURRENT_POS_ARITHMETIC,
            BYTECODE_TEST_PUSH_CURRENT_POS_ARITHMETIC_SIZE,
            input,
            sizeof(input) - 1)
    {
        // Should create two segments with different tags
        assert(segments.count == 2);

        // First segment: tag=100, 10 bytes from position 0
        assert(segments.items[0].tag == 100);
        assert(segments.items[0].start_pos == 0);
        assert(segments.items[0].size_bytes == 10);

        // Second segment: tag=200, 10 bytes from position 10 (10 - 0 = 10)
        assert(segments.items[1].tag == 200);
        assert(segments.items[1].start_pos == 10);
        assert(segments.items[1].size_bytes == 10);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: push.current_pos doesn't advance cursor
 *
 * Verifies that push.current_pos is a read-only operation that doesn't
 * modify the cursor position. Calls push.current_pos twice and verifies
 * they return the same value (difference = 0).
 *
 * Assembly:
 *   push.i32 2
 *   segment.create_unspecified
 *   push.current_pos
 *   push.current_pos
 *   math.sub
 *   segment.create_unspecified
 *   halt
 */
TEST(test_push_current_pos_no_side_effects)
{
    uint8_t input[] = "Test";

    EXPECT_SUCCESS(
            BYTECODE_TEST_PUSH_CURRENT_POS_NO_SIDE_EFFECTS,
            BYTECODE_TEST_PUSH_CURRENT_POS_NO_SIDE_EFFECTS_SIZE,
            input,
            sizeof(input) - 1)
    {
        // Two consecutive unspecified segments get merged into one
        assert(segments.count == 1);

        // Merged segment: 2 bytes from position 0 (2 + 0 = 2)
        assert(segments.items[0].start_pos == 0);
        assert(segments.items[0].size_bytes == 2);
    }
    END_EXPECT_SUCCESS();
}

/* ============================================================================
 * push.remaining Tests
 * ========================================================================= */

/**
 * Test: push.remaining at buffer start
 *
 * Verifies that push.remaining returns the full buffer size when called
 * at the beginning of input traversal.
 *
 * Assembly:
 *   push.remaining
 *   segment.create_unspecified
 *   halt
 *
 * Expected:
 *   - 1 segment with size 10 (full buffer size)
 */
TEST(test_push_remaining_initial)
{
    uint8_t input[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    EXPECT_SUCCESS(
            BYTECODE_TEST_PUSH_REMAINING_INITIAL,
            BYTECODE_TEST_PUSH_REMAINING_INITIAL_SIZE,
            input,
            sizeof(input))
    {
        // push.remaining creates a segment with the remaining bytes
        assert(segments.count == 1);
        assert(segments.items[0].start_pos == 0);
        assert(segments.items[0].size_bytes == 10);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: push.remaining after creating segment
 *
 * Verifies that push.remaining correctly returns the updated remaining
 * bytes after a segment consumes part of the buffer.
 *
 * Assembly:
 *   push.i32 5
 *   segment.create_unspecified
 *   push.remaining
 *   segment.create_unspecified
 *   halt
 *
 * Expected:
 *   - Consecutive unspecified segments merge into 1 segment
 *   - Merged segment: 10 bytes (5 + 5) at position 0
 */
TEST(test_push_remaining_after_segment)
{
    uint8_t input[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    EXPECT_SUCCESS(
            BYTECODE_TEST_PUSH_REMAINING_AFTER_SEGMENT,
            BYTECODE_TEST_PUSH_REMAINING_AFTER_SEGMENT_SIZE,
            input,
            sizeof(input))
    {
        // Two consecutive unspecified segments merge into one
        assert(segments.count == 1);

        // Merged segment: 10 bytes (5 + 5)
        assert(segments.items[0].start_pos == 0);
        assert(segments.items[0].size_bytes == 10);
    }
    END_EXPECT_SUCCESS();
}

/**
 * Test: push.remaining with multiple segments
 *
 * Tests that attempting to create a segment larger than remaining bytes
 * fails with SEGMENT_BOUNDS error, even when the size comes from arithmetic.
 *
 * Assembly:
 *   push.i32 3
 *   segment.create_unspecified
 *   push.remaining              ; 7 remaining
 *   push.i32 5
 *   segment.create_unspecified
 *   push.remaining              ; 2 remaining
 *   math.sub                    ; 7 - 2 = 5
 *   segment.create_unspecified
 *   halt
 *
 * Expected:
 *   - After first two segments (3 + 5 = 8 bytes), 2 bytes remain
 *   - Attempting to create 5-byte segment fails: SEGMENT_BOUNDS
 */
TEST(test_push_remaining_multiple)
{
    uint8_t input[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    // This test will FAIL because we try to create 5 bytes when only 2 remain
    EXPECT_ERROR(
            SDDL2_SEGMENT_BOUNDS,
            BYTECODE_TEST_PUSH_REMAINING_MULTIPLE,
            BYTECODE_TEST_PUSH_REMAINING_MULTIPLE_SIZE,
            input,
            sizeof(input));
}

/**
 * Test: push.remaining doesn't advance cursor
 *
 * Verifies that push.remaining is a read-only operation that doesn't
 * modify the cursor position. Calls push.remaining twice and verifies
 * they return the same value.
 *
 * Assembly:
 *   push.i32 3
 *   segment.create_unspecified  ; Consume 3 bytes
 *   push.remaining              ; 7
 *   push.remaining              ; 7 (no side effects)
 *   cmp.eq                      ; 7 == 7 → 1
 *   segment.create_unspecified  ; Create 1-byte segment
 *   halt
 *
 * Expected:
 *   - Two unspecified segments merge into 1 segment
 *   - Merged segment: 4 bytes (3 + 1)
 */
TEST(test_push_remaining_no_side_effects)
{
    uint8_t input[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    EXPECT_SUCCESS(
            BYTECODE_TEST_PUSH_REMAINING_NO_SIDE_EFFECTS,
            BYTECODE_TEST_PUSH_REMAINING_NO_SIDE_EFFECTS_SIZE,
            input,
            sizeof(input))
    {
        // Two consecutive unspecified segments merge into one
        assert(segments.count == 1);

        // Merged segment: 4 bytes (3 + 1)
        assert(segments.items[0].start_pos == 0);
        assert(segments.items[0].size_bytes == 4);
    }
    END_EXPECT_SUCCESS();
}

/* ============================================================================
 * Logical Operations Error Tests (Interpreter Level)
 * ========================================================================= */

/**
 * Test: logic.and with insufficient stack values
 *
 * Assembly:
 *   push.i32 42
 *   logic.and
 *   halt
 *
 * Expected: SDDL2_STACK_UNDERFLOW (needs 2 values, only 1 provided)
 */
TEST(test_logic_and_interpreter_stack_underflow)
{
    // Bytecode: push.i32 42, logic.and, halt
    uint32_t bytecode[] = {
        0x00010003, // push.i32
        42,         // value
        0x00040001, // logic.and
        0x00050001, // halt
    };
    uint8_t input[] = { 0 };

    EXPECT_ERROR(
            SDDL2_STACK_UNDERFLOW,
            bytecode,
            sizeof(bytecode),
            input,
            sizeof(input));
}

/**
 * Test: logic.or with empty stack
 *
 * Assembly:
 *   logic.or
 *   halt
 *
 * Expected: SDDL2_STACK_UNDERFLOW
 */
TEST(test_logic_or_interpreter_empty_stack)
{
    // Bytecode: logic.or, halt
    uint32_t bytecode[] = {
        0x00040002, // logic.or
        0x00050001, // halt
    };
    uint8_t input[] = { 0 };

    EXPECT_ERROR(
            SDDL2_STACK_UNDERFLOW,
            bytecode,
            sizeof(bytecode),
            input,
            sizeof(input));
}

/**
 * Test: logic.xor with type mismatch
 *
 * Assembly:
 *   push.tag 100
 *   push.i32 42
 *   logic.xor
 *   halt
 *
 * Expected: SDDL2_TYPE_MISMATCH (tag is not I64)
 */
TEST(test_logic_xor_interpreter_type_mismatch)
{
    // Bytecode: push.tag 100, push.i32 42, logic.xor, halt
    uint32_t bytecode[] = {
        0x00010005, // push.tag
        100,        // tag value
        0x00010003, // push.i32
        42,         // i32 value
        0x00040003, // logic.xor
        0x00050001, // halt
    };
    uint8_t input[] = { 0 };

    EXPECT_ERROR(
            SDDL2_TYPE_MISMATCH,
            bytecode,
            sizeof(bytecode),
            input,
            sizeof(input));
}

/**
 * Test: logic.not with empty stack
 *
 * Assembly:
 *   logic.not
 *   halt
 *
 * Expected: SDDL2_STACK_UNDERFLOW
 */
TEST(test_logic_not_interpreter_empty_stack)
{
    // Bytecode: logic.not, halt
    uint32_t bytecode[] = {
        0x00040004, // logic.not
        0x00050001, // halt
    };
    uint8_t input[] = { 0 };

    EXPECT_ERROR(
            SDDL2_STACK_UNDERFLOW,
            bytecode,
            sizeof(bytecode),
            input,
            sizeof(input));
}

/**
 * Test: logic.not with type mismatch
 *
 * Assembly:
 *   push.type.u8
 *   logic.not
 *   halt
 *
 * Expected: SDDL2_TYPE_MISMATCH (Type is not I64)
 */
TEST(test_logic_not_interpreter_type_mismatch)
{
    // Bytecode: push.type.u8, logic.not, halt
    uint32_t bytecode[] = {
        0x00010110, // push.type.u8
        0x00040004, // logic.not
        0x00050001, // halt
    };
    uint8_t input[] = { 0 };

    EXPECT_ERROR(
            SDDL2_TYPE_MISMATCH,
            bytecode,
            sizeof(bytecode),
            input,
            sizeof(input));
}

/**
 * Test: Successful logic operations at interpreter level
 *
 * Assembly:
 *   push.i32 0xFF00
 *   push.i32 0x0FF0
 *   logic.and        ; Result: 0x0F00
 *   push.i32 0x000F
 *   logic.or         ; Result: 0x0F0F
 *   push.i32 0xFFFF
 *   logic.xor        ; Result: 0xF0F0
 *   logic.not        ; Result: ~0xF0F0
 *   stack.drop       ; Clean up
 *   halt
 *
 * Expected: SDDL2_OK
 */
TEST(test_logic_all_operations_interpreter)
{
    // Bytecode: full logical operations sequence
    uint32_t bytecode[] = {
        0x00010003, 0x0000FF00, // push.i32 0xFF00
        0x00010003, 0x00000FF0, // push.i32 0x0FF0
        0x00040001,             // logic.and
        0x00010003, 0x0000000F, // push.i32 0x000F
        0x00040002,             // logic.or
        0x00010003, 0x0000FFFF, // push.i32 0xFFFF
        0x00040003,             // logic.xor
        0x00040004,             // logic.not
        0x00070003,             // stack.drop
        0x00050001,             // halt
    };
    uint8_t input[] = { 0 };

    EXPECT_SUCCESS(bytecode, sizeof(bytecode), input, sizeof(input))
    {
        // Should execute without error, stack should be empty after drop
        assert(segments.count == 0);
    }
    END_EXPECT_SUCCESS();
}

int main(void)
{
    return sddl2_run_all_tests();
}
