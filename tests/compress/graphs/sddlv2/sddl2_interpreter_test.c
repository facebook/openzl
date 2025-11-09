// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "openzl/compress/graphs/sddlv2/sddl2_interpreter.h"

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

    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x05, 0x00, 0x00, 0x00, // value = 5
        0x01, 0x00, 0x0C, 0x00, // segment.create_unspecified
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

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

    uint8_t bytecode[] = {
        0x01, 0x00, 0x01, 0x00, // push.zero
        0x01, 0x00, 0x0C, 0x00, // segment.create_unspecified
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

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

    uint8_t bytecode[] = {
        0x10, 0x01, 0x01, 0x00, // push.type.u8
        0x18, 0x01, 0x01, 0x00, // push.type.i32le
        0x38, 0x01, 0x01, 0x00, // push.type.f64be
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

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

    uint8_t bytecode[] = {
        0x05, 0x00, 0x01, 0x00, // push.tag
        0x64, 0x00, 0x00, 0x00, // tag = 100
        0x18, 0x01, 0x01, 0x00, // push.type.i32le
        0x02, 0x00, 0x01, 0x00, // push.u32
        0x03, 0x00, 0x00, 0x00, // value = 3
        0x02, 0x00, 0x0C, 0x00, // segment.create_tagged
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input), &segments);

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

    uint8_t bytecode[] = {
        // Segment 1: tag=100, type=U8, size=1
        0x05,
        0x00,
        0x01,
        0x00, // push.tag
        0x64,
        0x00,
        0x00,
        0x00, // tag = 100
        0x10,
        0x01,
        0x01,
        0x00, // push.type.u8
        0x02,
        0x00,
        0x01,
        0x00, // push.u32
        0x01,
        0x00,
        0x00,
        0x00, // value = 1
        0x02,
        0x00,
        0x0C,
        0x00, // segment.create_tagged

        // Segment 2: tag=100, type=F32LE, size=1
        0x05,
        0x00,
        0x01,
        0x00, // push.tag
        0x64,
        0x00,
        0x00,
        0x00, // tag = 100
        0x35,
        0x01,
        0x01,
        0x00, // push.type.f32le
        0x02,
        0x00,
        0x01,
        0x00, // push.u32
        0x01,
        0x00,
        0x00,
        0x00, // value = 1
        0x02,
        0x00,
        0x0C,
        0x00, // segment.create_tagged

        0x01,
        0x00,
        0x05,
        0x00 // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input), &segments);

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

    uint8_t bytecode[] = {
        0x05, 0x00, 0x01, 0x00, // push.tag
        0x64, 0x00, 0x00, 0x00, // value = 100
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

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

    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x0A, 0x00, 0x00, 0x00, // value = 10
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x05, 0x00, 0x00, 0x00, // value = 5
        0x01, 0x00, 0x02, 0x00, // math.add
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

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

    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x02, 0x00, 0x00, 0x00, // 2
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x03, 0x00, 0x00, 0x00, // 3
        0x01, 0x00, 0x02, 0x00, // math.add
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x04, 0x00, 0x00, 0x00, // 4
        0x03, 0x00, 0x02, 0x00, // math.mul
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

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

    uint8_t bytecode[] = {
        // add: 10 + 5 = 15
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x05,
        0x00,
        0x00,
        0x00, // 5
        0x01,
        0x00,
        0x02,
        0x00, // math.add (result: 15)

        // sub: 20 - 8 = 12
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x14,
        0x00,
        0x00,
        0x00, // 20
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x08,
        0x00,
        0x00,
        0x00, // 8
        0x02,
        0x00,
        0x02,
        0x00, // math.sub (result: 12)

        // mul: 3 * 4 = 12
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x03,
        0x00,
        0x00,
        0x00, // 3
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x04,
        0x00,
        0x00,
        0x00, // 4
        0x03,
        0x00,
        0x02,
        0x00, // math.mul (result: 12)

        // div: 20 / 4 = 5
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x14,
        0x00,
        0x00,
        0x00, // 20
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x04,
        0x00,
        0x00,
        0x00, // 4
        0x04,
        0x00,
        0x02,
        0x00, // math.div (result: 5)

        // mod: 17 % 5 = 2
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x11,
        0x00,
        0x00,
        0x00, // 17
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x05,
        0x00,
        0x00,
        0x00, // 5
        0x05,
        0x00,
        0x02,
        0x00, // math.mod (result: 2)

        // abs: abs(-42) = 42
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0xD6,
        0xFF,
        0xFF,
        0xFF, // -42 (two's complement)
        0x06,
        0x00,
        0x02,
        0x00, // math.abs (result: 42)

        // neg: neg(10) = -10
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x07,
        0x00,
        0x02,
        0x00, // math.neg (result: -10)

        0x01,
        0x00,
        0x05,
        0x00 // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

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

    uint8_t bytecode[] = {
        // eq: 10 == 10 -> 1
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x01,
        0x00,
        0x03,
        0x00, // cmp.eq (result: 1)

        // ne: 10 != 5 -> 1
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x05,
        0x00,
        0x00,
        0x00, // 5
        0x02,
        0x00,
        0x03,
        0x00, // cmp.ne (result: 1)

        // lt: 5 < 10 -> 1
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x05,
        0x00,
        0x00,
        0x00, // 5
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x03,
        0x00,
        0x03,
        0x00, // cmp.lt (result: 1)

        // le: 10 <= 10 -> 1
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x04,
        0x00,
        0x03,
        0x00, // cmp.le (result: 1)

        // gt: 10 > 5 -> 1
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x05,
        0x00,
        0x00,
        0x00, // 5
        0x05,
        0x00,
        0x03,
        0x00, // cmp.gt (result: 1)

        // ge: 10 >= 10 -> 1
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x06,
        0x00,
        0x03,
        0x00, // cmp.ge (result: 1)

        0x01,
        0x00,
        0x05,
        0x00 // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

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

    uint8_t bytecode[] = {
        // eq: 10 == 5 -> 0
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x05,
        0x00,
        0x00,
        0x00, // 5
        0x01,
        0x00,
        0x03,
        0x00, // cmp.eq (result: 0)

        // lt: 10 < 5 -> 0
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x0A,
        0x00,
        0x00,
        0x00, // 10
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x05,
        0x00,
        0x00,
        0x00, // 5
        0x03,
        0x00,
        0x03,
        0x00, // cmp.lt (result: 0)

        0x01,
        0x00,
        0x05,
        0x00 // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

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

    uint8_t bytecode[] = {
        // lt: -10 < 5 -> 1
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0xF6,
        0xFF,
        0xFF,
        0xFF, // -10 (two's complement)
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x05,
        0x00,
        0x00,
        0x00, // 5
        0x03,
        0x00,
        0x03,
        0x00, // cmp.lt (result: 1)

        // gt: 5 > -10 -> 1
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0x05,
        0x00,
        0x00,
        0x00, // 5
        0x03,
        0x00,
        0x01,
        0x00, // push.i32
        0xF6,
        0xFF,
        0xFF,
        0xFF, // -10
        0x05,
        0x00,
        0x03,
        0x00, // cmp.gt (result: 1)

        0x01,
        0x00,
        0x05,
        0x00 // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

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
    test_invalid_bytecode_size();
    test_missing_halt();

    printf("\n✅ All interpreter tests passed! (20 tests)\n");
    return 0;
}
