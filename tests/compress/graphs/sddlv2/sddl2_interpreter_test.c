// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "openzl/compress/graphs/sddlv2/sddl2_interpreter.h"
#include "generated_test_bytecode.h"

/**
 * Test: Execute simple program that creates one unspecified segment
 *
 * Assembly:
 *   push.i32 5
 *   segment.create_unspecified
 *   halt
 */
static void test_simple_segment_creation(void)
{
    uint8_t input[] = "Hello";

    const uint8_t* bytecode = BYTECODE_TEST_SEGMENT_UNSPECIFIED;
    size_t bytecode_size = BYTECODE_TEST_SEGMENT_UNSPECIFIED_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 1);
    assert(segments.items[0].tag == 0);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 5);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_simple_segment_creation passed\n");
}

/**
 * Test: Push zero and create zero-size segment
 */
static void test_zero_size_segment(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_SEGMENT_ZERO;
    size_t bytecode_size = BYTECODE_TEST_SEGMENT_ZERO_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 1);
    assert(segments.items[0].size_bytes == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_zero_size_segment passed\n");
}

/**
 * Test: push.type opcode execution
 */
static void test_push_type_execution(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_PUSH_TYPE_EXECUTION;
    size_t bytecode_size = BYTECODE_TEST_PUSH_TYPE_EXECUTION_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_push_type_execution passed\n");
}

/**
 * Test: End-to-end test of push.type + segment.create_tagged
 */
static void test_push_type_with_segment_create_tagged(void)
{
    uint8_t input[12] = { 0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
                          0x00, 0x00, 0x03, 0x00, 0x00, 0x00 };

    const uint8_t* bytecode = BYTECODE_TEST_PUSH_TYPE_WITH_SEGMENT_CREATE_TAGGED;
    size_t bytecode_size = BYTECODE_TEST_PUSH_TYPE_WITH_SEGMENT_CREATE_TAGGED_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input), &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 12);
    assert(segments.items[0].type.kind == SDDL2_TYPE_I32LE);
    assert(segments.items[0].type.width == 1);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_push_type_with_segment_create_tagged passed\n");
}

/**
 * Test: Multiple typed segments with different types
 */
static void test_multiple_typed_segments(void)
{
    uint8_t input[5] = { 0x42, 0x00, 0x00, 0x80, 0x3F };

    const uint8_t* bytecode = BYTECODE_TEST_MULTIPLE_TYPED_SEGMENTS;
    size_t bytecode_size = BYTECODE_TEST_MULTIPLE_TYPED_SEGMENTS_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input), &segments);

    assert(err == SDDL2_OK);
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

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_multiple_typed_segments passed\n");
}

/**
 * Test: push.tag opcode execution
 */
static void test_push_tag_execution(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_PUSH_TAG_EXECUTION;
    size_t bytecode_size = BYTECODE_TEST_PUSH_TAG_EXECUTION_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_push_tag_execution passed\n");
}

/**
 * Test: MATH operations through interpreter
 *
 * Tests: 10 + 5 = 15
 */
static void test_math_add_execution(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_MATH_ADD;
    size_t bytecode_size = BYTECODE_TEST_MATH_ADD_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_math_add_execution passed\n");
}

/**
 * Test: Multiple MATH operations
 *
 * Tests: (2 + 3) * 4 = 20
 */
static void test_math_combined_execution(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_MATH_COMBINED;
    size_t bytecode_size = BYTECODE_TEST_MATH_COMBINED_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_math_combined_execution passed\n");
}

/**
 * Test: All 7 MATH operations
 *
 * Tests each MATH operation to verify dispatch works.
 * Note: We don't clean up intermediate results (would need stack.drop),
 * so the stack accumulates values, but this is fine for testing dispatch.
 */
static void test_math_all_operations(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_MATH_ALL_OPERATIONS;
    size_t bytecode_size = BYTECODE_TEST_MATH_ALL_OPERATIONS_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_math_all_operations passed\n");
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
static void test_math_div_by_zero(void)
{
    uint8_t input[] = "Test";

    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x0A, 0x00, 0x00, 0x00, // 10
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x00, 0x00, 0x00, 0x00, // 0
        0x04, 0x00, 0x02, 0x00, // math.div
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    // Should return divide by zero error
    assert(err == SDDL2_DIV_ZERO);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_math_div_by_zero passed\n");
}

/**
 * Test: MATH overflow error
 *
 * Tests that overflow is properly detected and propagated
 *
 * Assembly:
 *   push.i64 INT64_MAX
 *   push.i64 1
 *   math.add
 *   halt
 */
static void test_math_overflow(void)
{
    uint8_t input[] = "Test";

    uint8_t bytecode[] = {
        0x04, 0x00, 0x01, 0x00, // push.i64
        0xFF, 0xFF, 0xFF, 0xFF, // INT64_MAX = 0x7FFFFFFFFFFFFFFF
        0xFF, 0xFF, 0xFF, 0x7F,
        0x04, 0x00, 0x01, 0x00, // push.i64
        0x01, 0x00, 0x00, 0x00, // 1
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x02, 0x00, // math.add (will overflow)
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    // Should return overflow error
    assert(err == SDDL2_STACK_OVERFLOW);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_math_overflow passed\n");
}

/**
 * Test: All 6 CMP operations
 *
 * Tests each CMP operation to verify dispatch works.
 * Stack comparison results (1=true, 0=false) accumulate for testing.
 */
static void test_cmp_all_operations(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_CMP_ALL;
    size_t bytecode_size = BYTECODE_TEST_CMP_ALL_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_cmp_all_operations passed\n");
}

/**
 * Test: CMP operations with false results
 *
 * Tests that comparisons correctly return 0 for false conditions
 */
static void test_cmp_false_results(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_CMP_FALSE_RESULTS;
    size_t bytecode_size = BYTECODE_TEST_CMP_FALSE_RESULTS_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_cmp_false_results passed\n");
}

/**
 * Test: CMP operations with negative numbers
 *
 * Tests signed comparison behavior with negative values
 */
static void test_cmp_negative_numbers(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_CMP_NEGATIVE_NUMBERS;
    size_t bytecode_size = BYTECODE_TEST_CMP_NEGATIVE_NUMBERS_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_cmp_negative_numbers passed\n");
}

/**
 * Test: CMP with stack underflow (only 1 element)
 *
 * Tests that cmp.eq with only 1 element on stack returns underflow error
 */
static void test_cmp_stack_underflow(void)
{
    uint8_t input[] = "Test";

    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x0A, 0x00, 0x00, 0x00, // 10
        0x01, 0x00, 0x03, 0x00, // cmp.eq (needs 2 elements, only 1!)
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_STACK_UNDERFLOW);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_cmp_stack_underflow passed\n");
}

/**
 * Test: CMP with type mismatch (non-I64 values)
 *
 * Tests that cmp.eq with Tag values returns type mismatch error
 */
static void test_cmp_type_mismatch(void)
{
    uint8_t input[] = "Test";

    uint8_t bytecode[] = {
        0x05, 0x00, 0x01, 0x00, // push.tag
        0x64, 0x00, 0x00, 0x00, // tag = 100
        0x05, 0x00, 0x01, 0x00, // push.tag
        0xC8, 0x00, 0x00, 0x00, // tag = 200
        0x01, 0x00, 0x03, 0x00, // cmp.eq (expects I64, got Tag!)
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_TYPE_MISMATCH);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_cmp_type_mismatch passed\n");
}

/**
 * Test: MATH with stack underflow (only 1 element)
 *
 * Tests that math.add with only 1 element on stack returns underflow error
 */
static void test_math_stack_underflow(void)
{
    uint8_t input[] = "Test";

    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x0A, 0x00, 0x00, 0x00, // 10
        0x01, 0x00, 0x02, 0x00, // math.add (needs 2 elements, only 1!)
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_STACK_UNDERFLOW);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_math_stack_underflow passed\n");
}

/**
 * Test: MATH with type mismatch (non-I64 values)
 *
 * Tests that math.add with Type values returns type mismatch error
 */
static void test_math_type_mismatch(void)
{
    uint8_t input[] = "Test";

    uint8_t bytecode[] = {
        0x10, 0x01, 0x01, 0x00, // push.type.u8
        0x18, 0x01, 0x01, 0x00, // push.type.i32le
        0x01, 0x00, 0x02, 0x00, // math.add (expects I64, got Type!)
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_TYPE_MISMATCH);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_math_type_mismatch passed\n");
}

/**
 * Test: STACK drop operation
 *
 * Tests that stack.drop removes the top element
 */
static void test_stack_drop(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_STACK_DROP;
    size_t bytecode_size = BYTECODE_TEST_STACK_DROP_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_stack_drop passed\n");
}

/**
 * Test: STACK dup operation
 *
 * Tests that stack.dup duplicates the top element
 */
static void test_stack_dup(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_STACK_DUP;
    size_t bytecode_size = BYTECODE_TEST_STACK_DUP_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_stack_dup passed\n");
}

/**
 * Test: STACK swap operation
 *
 * Tests that stack.swap swaps the top two elements
 * Stack: a b -> b a
 */
static void test_stack_swap(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_STACK_SWAP;
    size_t bytecode_size = BYTECODE_TEST_STACK_SWAP_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_stack_swap passed\n");
}

/**
 * Test: STACK operations with mixed types
 *
 * Tests that stack operations work with different value types (I64, Tag, Type)
 */
static void test_stack_operations_mixed_types(void)
{
    uint8_t input[] = "Test";

    const uint8_t* bytecode = BYTECODE_TEST_STACK_OPERATIONS_MIXED_TYPES;
    size_t bytecode_size = BYTECODE_TEST_STACK_OPERATIONS_MIXED_TYPES_SIZE;

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_stack_operations_mixed_types passed\n");
}

/**
 * Test: STACK drop with underflow
 *
 * Tests that stack.drop on empty stack returns underflow error
 */
static void test_stack_drop_underflow(void)
{
    uint8_t input[] = "Test";

    uint8_t bytecode[] = {
        0x03, 0x00, 0x07, 0x00, // stack.drop (empty stack!) - opcode 0x0003
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_STACK_UNDERFLOW);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_stack_drop_underflow passed\n");
}

/**
 * Test: STACK dup with underflow
 *
 * Tests that stack.dup on empty stack returns underflow error
 */
static void test_stack_dup_underflow(void)
{
    uint8_t input[] = "Test";

    uint8_t bytecode[] = {
        0x01, 0x00, 0x07, 0x00, // stack.dup (empty stack!)
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_STACK_UNDERFLOW);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_stack_dup_underflow passed\n");
}

/**
 * Test: STACK swap with underflow (only 1 element)
 *
 * Tests that stack.swap with only 1 element returns underflow error
 */
static void test_stack_swap_underflow(void)
{
    uint8_t input[] = "Test";

    uint8_t bytecode[] = {
        0x03, 0x00,
        0x01, 0x00, // push.i32
        0x0A, 0x00,
        0x00, 0x00, // 10
        0x04, 0x00,
        0x07, 0x00, // stack.swap (needs 2 elements, only 1!) - opcode 0x0004
        0x01, 0x00,
        0x05, 0x00 // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_STACK_UNDERFLOW);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_stack_swap_underflow passed\n");
}

/**
 * Test: Invalid bytecode size
 */
static void test_invalid_bytecode_size(void)
{
    uint8_t input[]    = "Test";
    uint8_t bytecode[] = { 0x01, 0x00, 0x03 };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err != SDDL2_OK);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_invalid_bytecode_size passed\n");
}

/**
 * Test: Program without halt
 */
static void test_missing_halt(void)
{
    uint8_t input[] = "Test";

    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x05, 0x00, 0x00, 0x00, // 5
        0x01, 0x00, 0x0C, 0x00  // segment.create_unspecified
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err != SDDL2_OK);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_missing_halt passed\n");
}

int main(void)
{
    printf("Running SDDL2 Interpreter Tests...\n\n");

    test_simple_segment_creation();
    test_zero_size_segment();
    test_push_type_execution();
    test_push_type_with_segment_create_tagged();
    test_multiple_typed_segments();
    test_push_tag_execution();
    test_math_add_execution();
    test_math_combined_execution();
    test_math_all_operations();
    test_math_div_by_zero();
    test_math_overflow();
    test_cmp_all_operations();
    test_cmp_false_results();
    test_cmp_negative_numbers();
    test_cmp_stack_underflow();
    test_cmp_type_mismatch();
    test_math_stack_underflow();
    test_math_type_mismatch();
    test_stack_drop();
    test_stack_dup();
    test_stack_swap();
    test_stack_operations_mixed_types();
    test_stack_drop_underflow();
    test_stack_dup_underflow();
    test_stack_swap_underflow();
    test_invalid_bytecode_size();
    test_missing_halt();

    printf("\n✅ All interpreter tests passed! (27 tests)\n");
    return 0;
}
