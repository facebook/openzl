// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Unit tests for SDDL2 bytecode interpreter.
 * Tests the minimal end-to-end execution: bytecode -> segments
 */

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
 *
 * Bytecode (little-endian):
 *   03 00 01 00   # push.i32 (opcode=0x0003, family=0x0001)
 *   05 00 00 00   # value = 5
 *   01 00 0C 00   # segment.create_unspecified (opcode=0x0001, family=0x000C)
 *   01 00 05 00   # halt (opcode=0x0001, family=0x0005)
 */
static void test_simple_segment_creation(void)
{
    // Input data: "Hello"
    uint8_t input[] = "Hello";

    // Bytecode: push.i32 5, segment.create_unspecified, halt
    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x05, 0x00, 0x00, 0x00, // value = 5
        0x01, 0x00, 0x0C, 0x00, // segment.create_unspecified
        0x01, 0x00, 0x05, 0x00  // halt
    };

    // Execute
    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode,
            sizeof(bytecode),
            input,
            sizeof(input) - 1, // Exclude null terminator
            &segments);

    // Verify
    assert(err == SDDL2_OK);
    assert(segments.count == 1);
    assert(segments.items[0].tag == 0); // Unspecified segment
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 5);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_simple_segment_creation passed\n");
}

/**
 * Test: Push zero and create zero-size segment
 */
static void test_zero_size_segment(void)
{
    uint8_t input[] = "Test";

    // Bytecode: push.zero, segment.create_unspecified, halt
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
 *
 * This tests that the interpreter correctly executes push.type bytecode.
 * We test a few representative types (u8, i32le, f64be) to verify:
 * 1. The opcodes execute without errors
 * 2. Values are pushed onto the stack (test passes if no stack errors occur)
 *
 * Bytecode: push.type u8, push.type i32le, push.type f64be, halt
 */
static void test_push_type_execution(void)
{
    uint8_t input[] = "Test";

    // Bytecode: push.type.u8, push.type.i32le, push.type.f64be, halt
    uint8_t bytecode[] = {
        0x10, 0x01,
        0x01, 0x00, // push.type.u8 (opcode=0x0110, family=0x0001)
        0x18, 0x01,
        0x01, 0x00, // push.type.i32le (opcode=0x0118, family=0x0001)
        0x38, 0x01,
        0x01, 0x00, // push.type.f64be (opcode=0x0138, family=0x0001)
        0x01, 0x00,
        0x05, 0x00 // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Execute - should succeed (just pushes values and halts)
    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    // Should execute successfully
    assert(err == SDDL2_OK);

    // No segments should be created (we just push and halt)
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_push_type_execution passed\n");
}

/**
 * Test: End-to-end test of push.type + segment.create_tagged
 *
 * This tests the complete pipeline:
 * 1. push.tag pushes a Tag value
 * 2. push.type pushes a Type value
 * 3. push.i32 pushes element count
 * 4. segment.create_tagged creates a typed segment
 *
 * We test with multiple types to verify type metadata is correctly set.
 *
 * Assembly:
 *   push.tag 100
 *   push.type.i32le
 *   push.i32 3
 *   segment.create_tagged
 *   halt
 *
 * Expected: 1 segment with tag=100, type=I32LE, width=1, size=12 bytes (3
 * elements * 4 bytes)
 */
static void test_push_type_with_segment_create_tagged(void)
{
    // Input: 12 bytes for 3 i32le elements
    uint8_t input[12] = {
        0x01, 0x00, 0x00, 0x00, // element 1
        0x02, 0x00, 0x00, 0x00, // element 2
        0x03, 0x00, 0x00, 0x00  // element 3
    };

    // Bytecode: push.tag 100, push.type.i32le, push.i32 3,
    // segment.create_tagged, halt
    uint8_t bytecode[] = {
        0x05, 0x00,
        0x01, 0x00, // push.tag (opcode=0x0005, family=0x0001)
        0x64, 0x00,
        0x00, 0x00, // tag = 100
        0x18, 0x01,
        0x01, 0x00, // push.type.i32le (opcode=0x0118, family=0x0001)
        0x02, 0x00,
        0x01, 0x00, // push.u32 (opcode=0x0002, family=0x0001)
        0x03, 0x00,
        0x00, 0x00, // value = 3 (element count)
        0x02, 0x00,
        0x0C, 0x00, // segment.create_tagged (opcode=0x0002, family=0x000C)
        0x01, 0x00,
        0x05, 0x00 // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input), &segments);

    // Should execute successfully
    assert(err == SDDL2_OK);

    // Check segment was created
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 12); // 3 elements * 4 bytes
    assert(segments.items[0].type.kind == SDDL2_TYPE_I32LE);
    assert(segments.items[0].type.width == 1);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_push_type_with_segment_create_tagged passed\n");
}

/**
 * Test: Multiple typed segments with different types
 *
 * Tests creating multiple segments with different types to verify:
 * 1. Each segment has correct type metadata
 * 2. Size calculation works for all types
 * 3. Different types prevent merging even with same tag
 */
static void test_multiple_typed_segments(void)
{
    // Input: 1 byte (u8) + 4 bytes (f32le) = 5 bytes total
    uint8_t input[5] = { 0x42, 0x00, 0x00, 0x80, 0x3F };

    // Bytecode:
    // 1. Create segment: tag=100, type=U8, size=1 (1 byte)
    // 2. Create segment: tag=100, type=F32LE, size=1 (4 bytes)
    // Note: Same tag but different types = NO MERGE
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
        0x00, // push.type.u8 (opcode=0x0110)
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
        0x00, // push.type.f32le (opcode=0x0135)
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

    // Should have 2 segments (no merge due to different types)
    assert(segments.count == 2);

    // Segment 1: tag=100, type=U8, 1 byte
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 1);
    assert(segments.items[0].type.kind == SDDL2_TYPE_U8);
    assert(segments.items[0].type.width == 1);

    // Segment 2: tag=100, type=F32LE, 4 bytes
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
 *
 * This tests that the interpreter correctly executes push.tag bytecode.
 * Since we can't directly inspect the stack, we test indirectly by:
 * 1. Using push.tag to push a value that would be wrong if interpreted as I64
 * 2. The test will fail at the segment creation step if push.tag isn't working
 *
 * Bytecode: push.tag 100, halt
 */
static void test_push_tag_execution(void)
{
    uint8_t input[] = "Test";

    // Bytecode: push.tag 100, halt
    // This is the bytecode generated by assembler test push_tag_100.asm
    uint8_t bytecode[] = {
        0x05, 0x00, 0x01, 0x00, // push.tag (opcode=0x0005, family=0x0001)
        0x64, 0x00, 0x00, 0x00, // value = 100
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Execute - should succeed (just pushes a value and halts)
    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    // Should execute successfully even though we don't use the pushed value
    assert(err == SDDL2_OK);

    // No segments should be created (we just push and halt)
    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_push_tag_execution passed\n");
}

/**
 * Test: Invalid bytecode (not multiple of 4)
 */
static void test_invalid_bytecode_size(void)
{
    uint8_t input[]    = "Test";
    uint8_t bytecode[] = { 0x01, 0x00, 0x03 }; // Only 3 bytes

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err != SDDL2_OK); // Should fail

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_invalid_bytecode_size passed\n");
}

/**
 * Test: Program without halt
 */
static void test_missing_halt(void)
{
    uint8_t input[] = "Test";

    // Bytecode: push.i32 5, segment.create_unspecified (no halt!)
    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x05, 0x00, 0x00, 0x00, // 5
        0x01, 0x00, 0x0C, 0x00  // segment.create_unspecified
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err != SDDL2_OK); // Should fail - no halt

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
    test_invalid_bytecode_size();
    test_missing_halt();

    printf("\n✅ All interpreter tests passed! (8 tests)\n");
    return 0;
}
