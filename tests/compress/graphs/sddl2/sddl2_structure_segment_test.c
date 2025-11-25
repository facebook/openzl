// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Tests for creating segments with structure types.
 *
 * This test file verifies that segment.create_tagged works correctly with:
 * - Single structure instances
 * - Arrays of structures
 * - Nested structures
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"
#include "sddl2_test_framework.h"

// Stack storage for tests
#define STACK_SIZE 256
static SDDL2_Value g_stack_storage[STACK_SIZE];

/* ============================================================================
 * Test 1: Create segment with single structure instance
 * ========================================================================= */

TEST(test_segment_single_structure)
{
    // Create a structure {U8, I16LE, I32LE} = 7 bytes
    // Then create a segment with 1 instance of this structure
    SDDL2_Stack stack;
    stack.items    = g_stack_storage;
    stack.capacity = STACK_SIZE;
    SDDL2_Stack_init(&stack);

    // Create test input buffer (7 bytes for the structure)
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Initialize segment list and tag registry
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Step 1: Build structure type {U8, I16LE, I32LE}
    SDDL2_Type u8_type  = { SDDL2_TYPE_U8, 1, .struct_data = NULL };
    SDDL2_Type i16_type = { SDDL2_TYPE_I16LE, 1, .struct_data = NULL };
    SDDL2_Type i32_type = { SDDL2_TYPE_I32LE, 1, .struct_data = NULL };

    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(u8_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(i16_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(i32_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(3)) == SDDL2_OK);

    assert(SDDL2_op_type_structure(&stack, NULL, NULL) == SDDL2_OK);

    // Get the structure type
    SDDL2_Value struct_val;
    assert(SDDL2_Stack_pop(&stack, &struct_val) == SDDL2_OK);

    // Push in correct order: tag, type, size
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_tag(100)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, struct_val) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(1))
           == SDDL2_OK); // 1 instance

    // Create the segment
    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            &stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Verify the segment
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 7); // 1 instance × 7 bytes
    assert(segments.items[0].type.kind == SDDL2_TYPE_STRUCTURE);
    assert(segments.items[0].type.width == 1);

    // Cleanup
    SDDL2_Struct_data* struct_data = struct_val.value.as_type.struct_data;
    free(struct_data);
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

/* ============================================================================
 * Test 2: Create segment with array of structures
 * ========================================================================= */

TEST(test_segment_array_of_structures)
{
    // Create structure {U8, I32LE} = 5 bytes
    // Create array of 10 such structures = 50 bytes
    // Create segment with this array

    SDDL2_Stack stack;
    stack.items    = g_stack_storage;
    stack.capacity = STACK_SIZE;
    SDDL2_Stack_init(&stack);

    // Create test input buffer (50 bytes for 10 structures)
    uint8_t data[50];
    memset(data, 0x42, sizeof(data));
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Initialize segment list and tag registry
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Build structure type {U8, I32LE}
    SDDL2_Type u8_type  = { SDDL2_TYPE_U8, 1, .struct_data = NULL };
    SDDL2_Type i32_type = { SDDL2_TYPE_I32LE, 1, .struct_data = NULL };

    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(u8_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(i32_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_type_structure(&stack, NULL, NULL) == SDDL2_OK);

    // Create array of 10 structures
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_op_type_fixed_array(&stack) == SDDL2_OK);

    // Get the array type
    SDDL2_Value array_val;
    assert(SDDL2_Stack_pop(&stack, &array_val) == SDDL2_OK);
    assert(array_val.value.as_type.width == 10);

    // Push in correct order: tag, type, size (size=1 because type already has
    // width=10)
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_tag(200)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, array_val) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(1)) == SDDL2_OK);

    // Create the segment
    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            &stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Verify the segment
    assert(segments.count == 1);
    assert(segments.items[0].tag == 200);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 50); // 10 instances × 5 bytes
    assert(segments.items[0].type.kind == SDDL2_TYPE_STRUCTURE);
    assert(segments.items[0].type.width == 10);

    // Cleanup
    SDDL2_Struct_data* struct_data = array_val.value.as_type.struct_data;
    free(struct_data);
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

/* ============================================================================
 * Test 3: Multiple segments with structures
 * ========================================================================= */

TEST(test_multiple_structure_segments)
{
    // Create two different structure types and segments

    SDDL2_Stack stack;
    stack.items    = g_stack_storage;
    stack.capacity = STACK_SIZE;
    SDDL2_Stack_init(&stack);

    // Create test input buffer
    uint8_t data[100];
    memset(data, 0, sizeof(data));
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Initialize segment list and tag registry
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Segment 1: Structure {U8, U8} = 2 bytes, 10 instances = 20 bytes
    SDDL2_Type u8_type = { SDDL2_TYPE_U8, 1, .struct_data = NULL };

    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(u8_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(u8_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_type_structure(&stack, NULL, NULL) == SDDL2_OK);

    SDDL2_Value struct1_val;
    assert(SDDL2_Stack_pop(&stack, &struct1_val) == SDDL2_OK);

    assert(SDDL2_Stack_push(&stack, SDDL2_Value_tag(100)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, struct1_val) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(10)) == SDDL2_OK);

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            &stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Segment 2: Structure {I32LE, I32LE} = 8 bytes, 5 instances = 40 bytes
    SDDL2_Type i32_type = { SDDL2_TYPE_I32LE, 1, .struct_data = NULL };

    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(i32_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(i32_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_type_structure(&stack, NULL, NULL) == SDDL2_OK);

    SDDL2_Value struct2_val;
    assert(SDDL2_Stack_pop(&stack, &struct2_val) == SDDL2_OK);

    assert(SDDL2_Stack_push(&stack, SDDL2_Value_tag(200)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, struct2_val) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(5)) == SDDL2_OK);

    err = SDDL2_op_segment_create_tagged(&stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Verify segments
    assert(segments.count == 2);

    // Segment 1
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 20); // 10 × 2
    assert(segments.items[0].type.kind == SDDL2_TYPE_STRUCTURE);

    // Segment 2
    assert(segments.items[1].tag == 200);
    assert(segments.items[1].start_pos == 20);
    assert(segments.items[1].size_bytes == 40); // 5 × 8
    assert(segments.items[1].type.kind == SDDL2_TYPE_STRUCTURE);

    // Cleanup
    free(struct1_val.value.as_type.struct_data);
    free(struct2_val.value.as_type.struct_data);
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

/* ============================================================================
 * Test 4: Segment merging with structures (same structure type)
 * ========================================================================= */

TEST(test_structure_segment_merging)
{
    // Create two consecutive segments with the same structure type
    // They should merge automatically

    SDDL2_Stack stack;
    stack.items    = g_stack_storage;
    stack.capacity = STACK_SIZE;
    SDDL2_Stack_init(&stack);

    // Create test input buffer
    uint8_t data[100];
    memset(data, 0, sizeof(data));
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Initialize segment list and tag registry
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Create structure {U8, I16LE} = 3 bytes
    SDDL2_Type u8_type  = { SDDL2_TYPE_U8, 1, .struct_data = NULL };
    SDDL2_Type i16_type = { SDDL2_TYPE_I16LE, 1, .struct_data = NULL };

    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(u8_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(i16_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_type_structure(&stack, NULL, NULL) == SDDL2_OK);

    SDDL2_Value struct_val;
    assert(SDDL2_Stack_pop(&stack, &struct_val) == SDDL2_OK);

    // Create first segment: tag=100, 5 instances = 15 bytes
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_tag(100)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, struct_val) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(5)) == SDDL2_OK);

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            &stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);
    assert(segments.count == 1);

    // Create second segment with SAME tag and type: should merge!
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_tag(100)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, struct_val) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(3))
           == SDDL2_OK); // 3 more instances

    err = SDDL2_op_segment_create_tagged(&stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Should still be 1 segment (merged!)
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 24); // 8 instances × 3 bytes
    assert(segments.items[0].type.kind == SDDL2_TYPE_STRUCTURE);

    // Cleanup
    free(struct_val.value.as_type.struct_data);
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

/* ============================================================================
 * Main Test Runner
 * ========================================================================= */

int main(void)
{
    return sddl2_run_all_tests();
}
