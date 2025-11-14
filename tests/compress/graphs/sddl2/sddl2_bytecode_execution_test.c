// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Bytecode Execution Test
 *
 * This test verifies loading and executing pre-assembled bytecode:
 * 1. Load bytecode from .bin file (assembled externally)
 * 2. Execute bytecode through the interpreter
 * 3. Verify segment output
 *
 * Note: This is NOT a true end-to-end test - the assembly step is manual.
 * The bytecode must be pre-generated using the assembler:
 *   python3 tools/sddl/assembler/sddl2_assembler.py test_hello.sddl
 * test_hello.bin
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"
#include "generated_test_bytecode.h"

/**
 * Load bytecode from file
 */
static uint8_t* load_bytecode(const char* filename, size_t* size_out)
{
    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return NULL;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    // Allocate and read
    uint8_t* bytecode = (uint8_t*)malloc((size_t)size);
    if (!bytecode) {
        fclose(f);
        return NULL;
    }

    size_t bytes_read = fread(bytecode, 1, (size_t)size, f);
    fclose(f);

    if (bytes_read != (size_t)size) {
        free(bytecode);
        return NULL;
    }

    *size_out = (size_t)size;
    return bytecode;
}

/**
 * Test: Load and execute assembled bytecode from test_hello.bin
 *
 * Assembly source (test_hello.sddl):
 *   push.i32 5
 *   segment.create_unspecified
 *   halt
 *
 * Expected behavior:
 *   - Creates 1 unspecified segment of size 5
 *   - Segment starts at offset 0
 */
static void test_assembled_hello(void)
{
    // Input data
    uint8_t input[] = "Hello";

    // Load assembled bytecode
    size_t bytecode_size;
    uint8_t* bytecode = load_bytecode("test_hello.bin", &bytecode_size);

    if (!bytecode) {
        fprintf(stderr, "Failed to load test_hello.bin\n");
        fprintf(stderr,
                "Make sure to run: python3 "
                "../../../tools/sddl/assembler/sddl2_assembler.py test_hello.sddl "
                "test_hello.bin\n");
        exit(1);
    }

    printf("Loaded %zu bytes of bytecode from test_hello.bin\n", bytecode_size);

    // Execute bytecode
    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);

    SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    // Verify execution
    assert(err == SDDL2_OK);
    assert(segments.count == 1);
    assert(segments.items[0].tag == 0); // Unspecified
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 5);

    // Cleanup
    free(bytecode);
    SDDL2_Segment_list_destroy(&segments);

    printf("✓ test_assembled_hello passed\n");
}

/**
 * Test: Load and execute complex multi-segment bytecode
 *
 * Assembly source (test_multi_segments.sddl):
 *   # Segment 1: U8 array (tag=100), 4 bytes at offset 0
 *   push.i32 4
 *   push.tag 100
 *   push.type.u8
 *   segment.create_tagged
 *
 *   # Segment 2: I32LE scalar (tag=200), 4 bytes at offset 4
 *   push.i32 4
 *   push.tag 200
 *   push.type.i32le
 *   segment.create_tagged
 *
 *   # Segment 3: Unspecified (tag=0), 4 bytes at offset 8
 *   push.i32 4
 *   segment.create_unspecified
 *   halt
 *
 * Expected behavior:
 *   - Creates 3 segments with different types and tags
 *   - Tests both tagged and unspecified segments
 *   - Tests different type kinds (U8, I32LE)
 */
static void test_multi_segments(void)
{
    // Input data: 12 bytes [01 02 03 04][05 06 07 08][09 0A 0B 0C]
    uint8_t input[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                        0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C };

    // Load assembled bytecode
    size_t bytecode_size;
    uint8_t* bytecode =
            load_bytecode("test_multi_segments.bin", &bytecode_size);

    if (!bytecode) {
        fprintf(stderr, "Failed to load test_multi_segments.bin\n");
        fprintf(stderr,
                "Make sure to run: python3 "
                "../../../tools/sddl/assembler/sddl2_assembler.py "
                "test_multi_segments.sddl test_multi_segments.bin\n");
        exit(1);
    }

    printf("Loaded %zu bytes of bytecode from test_multi_segments.bin\n",
           bytecode_size);

    // Execute bytecode
    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);

    SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input), &segments);

    // Verify execution
    if (err != SDDL2_OK) {
        fprintf(stderr, "Execution failed with error code: %d\n", err);
        free(bytecode);
        SDDL2_Segment_list_destroy(&segments);
        exit(1);
    }
    assert(err == SDDL2_OK);
    assert(segments.count == 3);

    // Segment 1: U8 array, tag=100, 4 bytes starting at offset 0
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 4);
    assert(segments.items[0].type.kind == SDDL2_TYPE_U8);
    assert(segments.items[0].type.width == 1);

    // Segment 2: I32LE scalar, tag=200, 4 bytes starting at offset 4
    assert(segments.items[1].tag == 200);
    assert(segments.items[1].start_pos == 4);
    assert(segments.items[1].size_bytes == 4);
    assert(segments.items[1].type.kind == SDDL2_TYPE_I32LE);
    assert(segments.items[1].type.width == 1);

    // Segment 3: Unspecified, tag=0, 4 bytes starting at offset 8
    assert(segments.items[2].tag == 0);
    assert(segments.items[2].start_pos == 8);
    assert(segments.items[2].size_bytes == 4);
    // Unspecified segments have type = BYTES
    assert(segments.items[2].type.kind == SDDL2_TYPE_BYTES);

    // Cleanup
    free(bytecode);
    SDDL2_Segment_list_destroy(&segments);

    printf("✓ test_multi_segments passed\n");
}

/**
 * Test: expect_true with non-zero values (success case)
 *
 * Assembly source (test_expect_true_success.asm):
 *   push.i32 1
 *   expect_true
 *   push.i32 42
 *   expect_true
 *   halt
 *
 * Expected behavior:
 *   - Both expect_true operations succeed
 *   - VM returns SDDL2_OK
 */
static void test_expect_true_success(void)
{
    // Use generated bytecode from header
    const uint8_t* bytecode = BYTECODE_TEST_EXPECT_TRUE_SUCCESS;
    size_t bytecode_size = BYTECODE_TEST_EXPECT_TRUE_SUCCESS_SIZE;

    // No input needed for this test
    uint8_t input[] = "";

    // Execute bytecode
    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);

    SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, 0, &segments);

    // Verify: should succeed
    assert(err == SDDL2_OK);
    assert(segments.count == 0); // No segments created

    SDDL2_Segment_list_destroy(&segments);
    printf("✓ test_expect_true_success passed\n");
}

/**
 * Test: expect_true with zero value (failure case)
 *
 * Assembly source (test_expect_true_failure.asm):
 *   push.i32 0
 *   expect_true
 *   halt
 *
 * Expected behavior:
 *   - expect_true fails with SDDL2_VALIDATION_FAILED
 *   - halt is never reached
 */
static void test_expect_true_failure(void)
{
    // Use generated bytecode from header
    const uint8_t* bytecode = BYTECODE_TEST_EXPECT_TRUE_FAILURE;
    size_t bytecode_size = BYTECODE_TEST_EXPECT_TRUE_FAILURE_SIZE;

    uint8_t input[] = "";

    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);

    SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, 0, &segments);

    // Verify: should fail with VALIDATION_FAILED
    assert(err == SDDL2_VALIDATION_FAILED);

    SDDL2_Segment_list_destroy(&segments);
    printf("✓ test_expect_true_failure passed\n");
}

/**
 * Test: expect_true with comparison (composability)
 *
 * Assembly source (test_expect_true_with_cmp.asm):
 *   push.i32 42
 *   push.i32 42
 *   cmp.eq
 *   expect_true
 *   halt
 *
 * Expected behavior:
 *   - cmp.eq returns 1 (true)
 *   - expect_true succeeds
 */
static void test_expect_true_with_comparison(void)
{
    // Use generated bytecode from header
    const uint8_t* bytecode = BYTECODE_TEST_EXPECT_TRUE_WITH_CMP;
    size_t bytecode_size = BYTECODE_TEST_EXPECT_TRUE_WITH_CMP_SIZE;

    uint8_t input[] = "";

    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);

    SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, 0, &segments);

    // Verify: should succeed (42 == 42 is true)
    assert(err == SDDL2_OK);

    SDDL2_Segment_list_destroy(&segments);
    printf("✓ test_expect_true_with_comparison passed\n");
}

/**
 * Test: push.stack_depth on empty stack
 *
 * Assembly source (test_push_stack_depth_empty.asm):
 *   push.stack_depth
 *   push.i32 0
 *   cmp.eq
 *   expect_true
 *   halt
 *
 * Expected behavior:
 *   - push.stack_depth on empty stack pushes 0
 *   - cmp.eq verifies depth is 0
 *   - expect_true succeeds
 */
static void test_push_stack_depth_empty(void)
{
    // Use generated bytecode from header
    const uint8_t* bytecode = BYTECODE_TEST_PUSH_STACK_DEPTH_EMPTY;
    size_t bytecode_size = BYTECODE_TEST_PUSH_STACK_DEPTH_EMPTY_SIZE;

    uint8_t input[] = "";

    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);

    SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, 0, &segments);

    // Verify: should succeed
    assert(err == SDDL2_OK);

    SDDL2_Segment_list_destroy(&segments);
    printf("✓ test_push_stack_depth_empty passed\n");
}

/**
 * Test: push.stack_depth tracks stack elements
 *
 * Assembly source (test_push_stack_depth_tracking.asm):
 *   push.i32 10
 *   push.i32 20
 *   push.i32 30
 *   push.stack_depth
 *   push.i32 3
 *   cmp.eq
 *   expect_true
 *   halt
 *
 * Expected behavior:
 *   - After pushing 3 values, stack depth is 3
 *   - Verification succeeds
 */
static void test_push_stack_depth_tracking(void)
{
    // Use generated bytecode from header
    const uint8_t* bytecode = BYTECODE_TEST_PUSH_STACK_DEPTH_TRACKING;
    size_t bytecode_size = BYTECODE_TEST_PUSH_STACK_DEPTH_TRACKING_SIZE;

    uint8_t input[] = "";

    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);

    SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, 0, &segments);

    // Verify: should succeed
    assert(err == SDDL2_OK);

    SDDL2_Segment_list_destroy(&segments);
    printf("✓ test_push_stack_depth_tracking passed\n");
}

/**
 * Test: push.stack_depth combined with arithmetic
 *
 * Assembly source (test_push_stack_depth_arithmetic.asm):
 *   push.i32 1
 *   push.i32 2
 *   push.stack_depth
 *   push.i32 10
 *   math.mul
 *   push.i32 20
 *   cmp.eq
 *   expect_true
 *   halt
 *
 * Expected behavior:
 *   - Stack has 2 elements
 *   - push.stack_depth pushes 2
 *   - 2 * 10 = 20
 *   - Verification succeeds
 */
static void test_push_stack_depth_arithmetic(void)
{
    // Use generated bytecode from header
    const uint8_t* bytecode = BYTECODE_TEST_PUSH_STACK_DEPTH_ARITHMETIC;
    size_t bytecode_size = BYTECODE_TEST_PUSH_STACK_DEPTH_ARITHMETIC_SIZE;

    uint8_t input[] = "";

    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments, NULL, NULL);

    SDDL2_Error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, 0, &segments);

    // Verify: should succeed
    assert(err == SDDL2_OK);

    SDDL2_Segment_list_destroy(&segments);
    printf("✓ test_push_stack_depth_arithmetic passed\n");
}

int main(void)
{
    printf("Running SDDL2 Bytecode Execution Test...\n\n");

    test_assembled_hello();
    test_multi_segments();
    test_expect_true_success();
    test_expect_true_failure();
    test_expect_true_with_comparison();
    test_push_stack_depth_empty();
    test_push_stack_depth_tracking();
    test_push_stack_depth_arithmetic();

    printf("\n✅ All bytecode execution tests passed!\n");
    printf("Verified: Load bytecode → Execute → Verify segments\n");
    printf("Verified: expect_true runtime behavior\n");
    printf("Verified: push.stack_depth runtime behavior\n");
    return 0;
}
