// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY
//
// Generated from interpreter test assembly files (.asm)
// Source: /Users/cyan/dev/openzl/cyan_openzl/tests/compress/graphs/sddl2/asm
// Generator: tests/compress/graphs/sddl2/generate_test_bytecode.py
//
// To regenerate:
//   python3 tests/compress/graphs/sddl2/generate_test_bytecode.py \
//       -i tests/compress/graphs/sddl2/asm \
//       -o tests/compress/graphs/sddl2/generated_test_bytecode.h

#ifndef GENERATED_TEST_BYTECODE_H
#define GENERATED_TEST_BYTECODE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"

/* Bytecode constants from interpreter test assembly files */

/* Source: test_cmp_all.asm */
static const uint8_t BYTECODE_TEST_CMP_ALL[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x05, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x06, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_CMP_ALL_SIZE = 124;

/* Source: test_cmp_false_results.asm */
/* Comparison operations with false results */
static const uint8_t BYTECODE_TEST_CMP_FALSE_RESULTS[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_CMP_FALSE_RESULTS_SIZE = 44;

/* Source: test_cmp_negative_numbers.asm */
/* Comparison operations with negative numbers */
static const uint8_t BYTECODE_TEST_CMP_NEGATIVE_NUMBERS[] = {
    0x03, 0x00, 0x01, 0x00, 0xF6, 0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0xF6, 0xFF, 0xFF, 0xFF, 0x05, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_CMP_NEGATIVE_NUMBERS_SIZE = 44;

/* Source: test_cmp_stack_underflow.asm */
/* CMP with stack underflow */
/* Expected error: SDDL2_STACK_UNDERFLOW */
static const uint8_t BYTECODE_TEST_CMP_STACK_UNDERFLOW[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_CMP_STACK_UNDERFLOW_SIZE = 16;

/* Source: test_cmp_type_mismatch.asm */
/* CMP with type mismatch */
/* Expected error: SDDL2_TYPE_MISMATCH */
static const uint8_t BYTECODE_TEST_CMP_TYPE_MISMATCH[] = {
    0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_CMP_TYPE_MISMATCH_SIZE = 24;

/* Source: test_expect_true_failure.asm */
/* expect_true with zero value (failure) */
/* Expected error: SDDL2_VALIDATION_FAILED */
static const uint8_t BYTECODE_TEST_EXPECT_TRUE_FAILURE[] = {
    0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x05, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_EXPECT_TRUE_FAILURE_SIZE = 16;

/* Source: test_expect_true_success.asm */
/* expect_true with non-zero values (success) */
static const uint8_t BYTECODE_TEST_EXPECT_TRUE_SUCCESS[] = {
    0x03, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x05, 0x00, 0x03, 0x00, 0x01, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x02, 0x00, 0x05, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_EXPECT_TRUE_SUCCESS_SIZE = 28;

/* Source: test_expect_true_with_cmp.asm */
/* expect_true with comparison (composability) */
static const uint8_t BYTECODE_TEST_EXPECT_TRUE_WITH_CMP[] = {
    0x03, 0x00, 0x01, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x02, 0x00, 0x05, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_EXPECT_TRUE_WITH_CMP_SIZE = 28;

/* Source: test_expect_true_with_trace.asm */
/* expect_true with rich trace output */
/* Expected error: SDDL2_VALIDATION_FAILED */
static const uint8_t BYTECODE_TEST_EXPECT_TRUE_WITH_TRACE[] = {
    0x04, 0x00, 0x05, 0x00, 0x03, 0x00, 0x01, 0x00, 0x96, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x02, 0x00, 0x05, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_EXPECT_TRUE_WITH_TRACE_SIZE = 60;

/* Source: test_expect_with_stack.asm */
/* expect_true failure with non-empty stack */
/* Expected error: SDDL2_VALIDATION_FAILED */
static const uint8_t BYTECODE_TEST_EXPECT_WITH_STACK[] = {
    0x04, 0x00, 0x05, 0x00, 0x03, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x02, 0x00, 0x05, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_EXPECT_WITH_STACK_SIZE = 60;

/* Source: test_math_add.asm */
static const uint8_t BYTECODE_TEST_MATH_ADD[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_MATH_ADD_SIZE = 24;

/* Source: test_math_all_operations.asm */
/* All 7 MATH operations */
static const uint8_t BYTECODE_TEST_MATH_ALL_OPERATIONS[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0x11, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x05, 0x00, 0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0xD6, 0xFF, 0xFF, 0xFF, 0x06, 0x00, 0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x07, 0x00, 0x02, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_MATH_ALL_OPERATIONS_SIZE = 128;

/* Source: test_math_combined.asm */
static const uint8_t BYTECODE_TEST_MATH_COMBINED[] = {
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_MATH_COMBINED_SIZE = 36;

/* Source: test_math_div_by_zero.asm */
/* Division by zero error */
/* Expected error: SDDL2_DIV_ZERO */
static const uint8_t BYTECODE_TEST_MATH_DIV_BY_ZERO[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_MATH_DIV_BY_ZERO_SIZE = 24;

/* Source: test_math_overflow.asm */
/* MATH overflow error */
/* Expected error: SDDL2_MATH_OVERFLOW */
static const uint8_t BYTECODE_TEST_MATH_OVERFLOW[] = {
    0x04, 0x00, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x04, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_MATH_OVERFLOW_SIZE = 32;

/* Source: test_math_stack_underflow.asm */
/* MATH with stack underflow */
/* Expected error: SDDL2_STACK_UNDERFLOW */
static const uint8_t BYTECODE_TEST_MATH_STACK_UNDERFLOW[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_MATH_STACK_UNDERFLOW_SIZE = 16;

/* Source: test_math_type_mismatch.asm */
/* MATH with type mismatch */
/* Expected error: SDDL2_TYPE_MISMATCH */
static const uint8_t BYTECODE_TEST_MATH_TYPE_MISMATCH[] = {
    0x10, 0x01, 0x01, 0x00, 0x18, 0x01, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_MATH_TYPE_MISMATCH_SIZE = 16;

/* Source: test_multiple_typed_segments.asm */
/* Create multiple typed segments with different types */
static const uint8_t BYTECODE_TEST_MULTIPLE_TYPED_SEGMENTS[] = {
    0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x10, 0x01, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x35, 0x01, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_MULTIPLE_TYPED_SEGMENTS_SIZE = 52;

/* Source: test_push_current_pos_after_segment.asm */
static const uint8_t BYTECODE_TEST_PUSH_CURRENT_POS_AFTER_SEGMENT[] = {
    0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x05, 0x00, 0x01, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_CURRENT_POS_AFTER_SEGMENT_SIZE = 48;

/* Source: test_push_current_pos_arithmetic.asm */
static const uint8_t BYTECODE_TEST_PUSH_CURRENT_POS_ARITHMETIC[] = {
    0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x05, 0x00, 0x01, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x80, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_CURRENT_POS_ARITHMETIC_SIZE = 60;

/* Source: test_push_current_pos_initial.asm */
static const uint8_t BYTECODE_TEST_PUSH_CURRENT_POS_INITIAL[] = {
    0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_CURRENT_POS_INITIAL_SIZE = 12;

/* Source: test_push_current_pos_multiple.asm */
static const uint8_t BYTECODE_TEST_PUSH_CURRENT_POS_MULTIPLE[] = {
    0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x05, 0x00, 0x01, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x80, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x05, 0x00, 0x01, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_CURRENT_POS_MULTIPLE_SIZE = 80;

/* Source: test_push_current_pos_no_side_effects.asm */
static const uint8_t BYTECODE_TEST_PUSH_CURRENT_POS_NO_SIDE_EFFECTS[] = {
    0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x80, 0x00, 0x01, 0x00, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_CURRENT_POS_NO_SIDE_EFFECTS_SIZE = 32;

/* Source: test_push_remaining_after_segment.asm */
static const uint8_t BYTECODE_TEST_PUSH_REMAINING_AFTER_SEGMENT[] = {
    0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x81, 0x00, 0x01, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_REMAINING_AFTER_SEGMENT_SIZE = 24;

/* Source: test_push_remaining_initial.asm */
static const uint8_t BYTECODE_TEST_PUSH_REMAINING_INITIAL[] = {
    0x81, 0x00, 0x01, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_REMAINING_INITIAL_SIZE = 12;

/* Source: test_push_remaining_multiple.asm */
static const uint8_t BYTECODE_TEST_PUSH_REMAINING_MULTIPLE[] = {
    0x03, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x81, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x81, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_REMAINING_MULTIPLE_SIZE = 44;

/* Source: test_push_remaining_no_side_effects.asm */
static const uint8_t BYTECODE_TEST_PUSH_REMAINING_NO_SIDE_EFFECTS[] = {
    0x03, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x81, 0x00, 0x01, 0x00, 0x81, 0x00, 0x01, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_REMAINING_NO_SIDE_EFFECTS_SIZE = 32;

/* Source: test_push_stack_depth_arithmetic.asm */
/* push.stack_depth combined with arithmetic */
static const uint8_t BYTECODE_TEST_PUSH_STACK_DEPTH_ARITHMETIC[] = {
    0x03, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x82, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x02, 0x00, 0x05, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_STACK_DEPTH_ARITHMETIC_SIZE = 52;

/* Source: test_push_stack_depth_empty.asm */
/* push.stack_depth on empty stack */
static const uint8_t BYTECODE_TEST_PUSH_STACK_DEPTH_EMPTY[] = {
    0x82, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x02, 0x00, 0x05, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_STACK_DEPTH_EMPTY_SIZE = 24;

/* Source: test_push_stack_depth_tracking.asm */
/* push.stack_depth tracks stack elements */
static const uint8_t BYTECODE_TEST_PUSH_STACK_DEPTH_TRACKING[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x82, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x02, 0x00, 0x05, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_STACK_DEPTH_TRACKING_SIZE = 48;

/* Source: test_push_tag_execution.asm */
/* Push tag value onto the stack */
static const uint8_t BYTECODE_TEST_PUSH_TAG_EXECUTION[] = {
    0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_TAG_EXECUTION_SIZE = 12;

/* Source: test_push_type_execution.asm */
/* Push various type values onto the stack */
static const uint8_t BYTECODE_TEST_PUSH_TYPE_EXECUTION[] = {
    0x10, 0x01, 0x01, 0x00, 0x18, 0x01, 0x01, 0x00, 0x38, 0x01, 0x01, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_TYPE_EXECUTION_SIZE = 16;

/* Source: test_push_type_with_segment_create_tagged.asm */
/* End-to-end typed segment creation pipeline */
static const uint8_t BYTECODE_TEST_PUSH_TYPE_WITH_SEGMENT_CREATE_TAGGED[] = {
    0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x18, 0x01, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_PUSH_TYPE_WITH_SEGMENT_CREATE_TAGGED_SIZE = 28;

/* Source: test_segment_unspecified.asm */
/* Simple segment creation (unspecified type) */
static const uint8_t BYTECODE_TEST_SEGMENT_UNSPECIFIED[] = {
    0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_SEGMENT_UNSPECIFIED_SIZE = 16;

/* Source: test_segment_zero.asm */
/* Zero-size segment creation */
static const uint8_t BYTECODE_TEST_SEGMENT_ZERO[] = {
    0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_SEGMENT_ZERO_SIZE = 12;

/* Source: test_stack_drop.asm */
static const uint8_t BYTECODE_TEST_STACK_DROP[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x03, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_DROP_SIZE = 24;

/* Source: test_stack_drop_if_false.asm */
/* stack.drop_if with false condition (zero) */
static const uint8_t BYTECODE_TEST_STACK_DROP_IF_FALSE[] = {
    0x03, 0x00, 0x01, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x07, 0x00, 0x03, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_DROP_IF_FALSE_SIZE = 28;

/* Source: test_stack_drop_if_negative.asm */
/* stack.drop_if with negative value as true */
static const uint8_t BYTECODE_TEST_STACK_DROP_IF_NEGATIVE[] = {
    0x03, 0x00, 0x01, 0x00, 0x32, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_DROP_IF_NEGATIVE_SIZE = 24;

/* Source: test_stack_drop_if_nonzero.asm */
/* stack.drop_if with non-zero value as true */
static const uint8_t BYTECODE_TEST_STACK_DROP_IF_NONZERO[] = {
    0x03, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x10, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_DROP_IF_NONZERO_SIZE = 24;

/* Source: test_stack_drop_if_true.asm */
/* stack.drop_if with true condition (non-zero) */
static const uint8_t BYTECODE_TEST_STACK_DROP_IF_TRUE[] = {
    0x03, 0x00, 0x01, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_DROP_IF_TRUE_SIZE = 24;

/* Source: test_stack_drop_if_underflow.asm */
/* stack.drop_if with stack underflow (missing condition) */
/* Expected error: SDDL2_STACK_UNDERFLOW */
static const uint8_t BYTECODE_TEST_STACK_DROP_IF_UNDERFLOW[] = {
    0x03, 0x00, 0x01, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x10, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_DROP_IF_UNDERFLOW_SIZE = 16;

/* Source: test_stack_drop_if_with_cmp.asm */
/* stack.drop_if combined with comparison */
static const uint8_t BYTECODE_TEST_STACK_DROP_IF_WITH_CMP[] = {
    0x03, 0x00, 0x01, 0x00, 0x63, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x05, 0x00, 0x03, 0x00, 0x10, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_DROP_IF_WITH_CMP_SIZE = 36;

/* Source: test_stack_drop_underflow.asm */
/* STACK drop with underflow */
/* Expected error: SDDL2_STACK_UNDERFLOW */
static const uint8_t BYTECODE_TEST_STACK_DROP_UNDERFLOW[] = {
    0x03, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_DROP_UNDERFLOW_SIZE = 8;

/* Source: test_stack_dup.asm */
/* STACK dup operation */
static const uint8_t BYTECODE_TEST_STACK_DUP[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x07, 0x00, 0x03, 0x00, 0x07, 0x00, 0x03, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_DUP_SIZE = 24;

/* Source: test_stack_dup_underflow.asm */
/* STACK dup with underflow */
/* Expected error: SDDL2_STACK_UNDERFLOW */
static const uint8_t BYTECODE_TEST_STACK_DUP_UNDERFLOW[] = {
    0x01, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_DUP_UNDERFLOW_SIZE = 8;

/* Source: test_stack_operations_mixed_types.asm */
/* STACK operations with mixed value types */
static const uint8_t BYTECODE_TEST_STACK_OPERATIONS_MIXED_TYPES[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x10, 0x01, 0x01, 0x00, 0x01, 0x00, 0x07, 0x00, 0x04, 0x00, 0x07, 0x00, 0x03, 0x00, 0x07, 0x00, 0x03, 0x00, 0x07, 0x00, 0x03, 0x00, 0x07, 0x00, 0x03, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_OPERATIONS_MIXED_TYPES_SIZE = 48;

/* Source: test_stack_swap.asm */
/* STACK swap operation */
static const uint8_t BYTECODE_TEST_STACK_SWAP[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x04, 0x00, 0x07, 0x00, 0x03, 0x00, 0x07, 0x00, 0x03, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_SWAP_SIZE = 32;

/* Source: test_stack_swap_underflow.asm */
/* STACK swap with underflow (only 1 element) */
/* Expected error: SDDL2_STACK_UNDERFLOW */
static const uint8_t BYTECODE_TEST_STACK_SWAP_UNDERFLOW[] = {
    0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x04, 0x00, 0x07, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_STACK_SWAP_UNDERFLOW_SIZE = 16;

/* Source: test_type_fixed_array_execution.asm */
static const uint8_t BYTECODE_TEST_TYPE_FIXED_ARRAY_EXECUTION[] = {
    0x16, 0x01, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_TYPE_FIXED_ARRAY_EXECUTION_SIZE = 20;

/* Source: test_type_fixed_array_with_segment.asm */
/* segment creation with array type */
static const uint8_t BYTECODE_TEST_TYPE_FIXED_ARRAY_WITH_SEGMENT[] = {
    0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x00, 0x00, 0x16, 0x01, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x03, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_TYPE_FIXED_ARRAY_WITH_SEGMENT_SIZE = 40;

/* Source: test_type_structure.asm */
static const uint8_t BYTECODE_TEST_TYPE_STRUCTURE[] = {
    0x10, 0x01, 0x01, 0x00, 0x14, 0x01, 0x01, 0x00, 0x18, 0x01, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_TYPE_STRUCTURE_SIZE = 32;

/* Source: test_zero_size_segment.asm */
static const uint8_t BYTECODE_TEST_ZERO_SIZE_SEGMENT[] = {
    0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0C, 0x00, 0x01, 0x00, 0x05, 0x00
};
static const size_t BYTECODE_TEST_ZERO_SIZE_SEGMENT_SIZE = 16;

/* Test metadata for auto-generated tests */
typedef struct {
    const char* name;
    const uint8_t* bytecode;
    size_t size;
    const char* description;
    SDDL2_Error expected_error;
    size_t input_size;                /* Minimum input size required (0 = none) */
    int skip;                         /* Skip in auto-test (1 = skip, 0 = run) */
    const char* custom_validator;     /* NULL = use default validator */
} SDDL2_TestCase;

static const SDDL2_TestCase SDDL2_BYTECODE_TESTS[] = {
    {
        .name = "test_cmp_all",
        .bytecode = BYTECODE_TEST_CMP_ALL,
        .size = BYTECODE_TEST_CMP_ALL_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_cmp_false_results",
        .bytecode = BYTECODE_TEST_CMP_FALSE_RESULTS,
        .size = BYTECODE_TEST_CMP_FALSE_RESULTS_SIZE,
        .description = "Comparison operations with false results",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_cmp_negative_numbers",
        .bytecode = BYTECODE_TEST_CMP_NEGATIVE_NUMBERS,
        .size = BYTECODE_TEST_CMP_NEGATIVE_NUMBERS_SIZE,
        .description = "Comparison operations with negative numbers",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_cmp_stack_underflow",
        .bytecode = BYTECODE_TEST_CMP_STACK_UNDERFLOW,
        .size = BYTECODE_TEST_CMP_STACK_UNDERFLOW_SIZE,
        .description = "CMP with stack underflow",
        .expected_error = SDDL2_STACK_UNDERFLOW,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_cmp_type_mismatch",
        .bytecode = BYTECODE_TEST_CMP_TYPE_MISMATCH,
        .size = BYTECODE_TEST_CMP_TYPE_MISMATCH_SIZE,
        .description = "CMP with type mismatch",
        .expected_error = SDDL2_TYPE_MISMATCH,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_expect_true_failure",
        .bytecode = BYTECODE_TEST_EXPECT_TRUE_FAILURE,
        .size = BYTECODE_TEST_EXPECT_TRUE_FAILURE_SIZE,
        .description = "expect_true with zero value (failure)",
        .expected_error = SDDL2_VALIDATION_FAILED,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_expect_true_success",
        .bytecode = BYTECODE_TEST_EXPECT_TRUE_SUCCESS,
        .size = BYTECODE_TEST_EXPECT_TRUE_SUCCESS_SIZE,
        .description = "expect_true with non-zero values (success)",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_expect_true_with_cmp",
        .bytecode = BYTECODE_TEST_EXPECT_TRUE_WITH_CMP,
        .size = BYTECODE_TEST_EXPECT_TRUE_WITH_CMP_SIZE,
        .description = "expect_true with comparison (composability)",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_expect_true_with_trace",
        .bytecode = BYTECODE_TEST_EXPECT_TRUE_WITH_TRACE,
        .size = BYTECODE_TEST_EXPECT_TRUE_WITH_TRACE_SIZE,
        .description = "expect_true with rich trace output",
        .expected_error = SDDL2_VALIDATION_FAILED,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_expect_with_stack",
        .bytecode = BYTECODE_TEST_EXPECT_WITH_STACK,
        .size = BYTECODE_TEST_EXPECT_WITH_STACK_SIZE,
        .description = "expect_true failure with non-empty stack",
        .expected_error = SDDL2_VALIDATION_FAILED,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_math_add",
        .bytecode = BYTECODE_TEST_MATH_ADD,
        .size = BYTECODE_TEST_MATH_ADD_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_math_all_operations",
        .bytecode = BYTECODE_TEST_MATH_ALL_OPERATIONS,
        .size = BYTECODE_TEST_MATH_ALL_OPERATIONS_SIZE,
        .description = "All 7 MATH operations",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_math_combined",
        .bytecode = BYTECODE_TEST_MATH_COMBINED,
        .size = BYTECODE_TEST_MATH_COMBINED_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_math_div_by_zero",
        .bytecode = BYTECODE_TEST_MATH_DIV_BY_ZERO,
        .size = BYTECODE_TEST_MATH_DIV_BY_ZERO_SIZE,
        .description = "Division by zero error",
        .expected_error = SDDL2_DIV_ZERO,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_math_overflow",
        .bytecode = BYTECODE_TEST_MATH_OVERFLOW,
        .size = BYTECODE_TEST_MATH_OVERFLOW_SIZE,
        .description = "MATH overflow error",
        .expected_error = SDDL2_MATH_OVERFLOW,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_math_stack_underflow",
        .bytecode = BYTECODE_TEST_MATH_STACK_UNDERFLOW,
        .size = BYTECODE_TEST_MATH_STACK_UNDERFLOW_SIZE,
        .description = "MATH with stack underflow",
        .expected_error = SDDL2_STACK_UNDERFLOW,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_math_type_mismatch",
        .bytecode = BYTECODE_TEST_MATH_TYPE_MISMATCH,
        .size = BYTECODE_TEST_MATH_TYPE_MISMATCH_SIZE,
        .description = "MATH with type mismatch",
        .expected_error = SDDL2_TYPE_MISMATCH,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_multiple_typed_segments",
        .bytecode = BYTECODE_TEST_MULTIPLE_TYPED_SEGMENTS,
        .size = BYTECODE_TEST_MULTIPLE_TYPED_SEGMENTS_SIZE,
        .description = "Create multiple typed segments with different types",
        .expected_error = SDDL2_OK,
        .input_size = 5,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_current_pos_after_segment",
        .bytecode = BYTECODE_TEST_PUSH_CURRENT_POS_AFTER_SEGMENT,
        .size = BYTECODE_TEST_PUSH_CURRENT_POS_AFTER_SEGMENT_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 10,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_current_pos_arithmetic",
        .bytecode = BYTECODE_TEST_PUSH_CURRENT_POS_ARITHMETIC,
        .size = BYTECODE_TEST_PUSH_CURRENT_POS_ARITHMETIC_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 20,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_current_pos_initial",
        .bytecode = BYTECODE_TEST_PUSH_CURRENT_POS_INITIAL,
        .size = BYTECODE_TEST_PUSH_CURRENT_POS_INITIAL_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_current_pos_multiple",
        .bytecode = BYTECODE_TEST_PUSH_CURRENT_POS_MULTIPLE,
        .size = BYTECODE_TEST_PUSH_CURRENT_POS_MULTIPLE_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 16,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_current_pos_no_side_effects",
        .bytecode = BYTECODE_TEST_PUSH_CURRENT_POS_NO_SIDE_EFFECTS,
        .size = BYTECODE_TEST_PUSH_CURRENT_POS_NO_SIDE_EFFECTS_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 5,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_remaining_after_segment",
        .bytecode = BYTECODE_TEST_PUSH_REMAINING_AFTER_SEGMENT,
        .size = BYTECODE_TEST_PUSH_REMAINING_AFTER_SEGMENT_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 10,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_remaining_initial",
        .bytecode = BYTECODE_TEST_PUSH_REMAINING_INITIAL,
        .size = BYTECODE_TEST_PUSH_REMAINING_INITIAL_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_remaining_multiple",
        .bytecode = BYTECODE_TEST_PUSH_REMAINING_MULTIPLE,
        .size = BYTECODE_TEST_PUSH_REMAINING_MULTIPLE_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 15,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_remaining_no_side_effects",
        .bytecode = BYTECODE_TEST_PUSH_REMAINING_NO_SIDE_EFFECTS,
        .size = BYTECODE_TEST_PUSH_REMAINING_NO_SIDE_EFFECTS_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 10,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_stack_depth_arithmetic",
        .bytecode = BYTECODE_TEST_PUSH_STACK_DEPTH_ARITHMETIC,
        .size = BYTECODE_TEST_PUSH_STACK_DEPTH_ARITHMETIC_SIZE,
        .description = "push.stack_depth combined with arithmetic",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_stack_depth_empty",
        .bytecode = BYTECODE_TEST_PUSH_STACK_DEPTH_EMPTY,
        .size = BYTECODE_TEST_PUSH_STACK_DEPTH_EMPTY_SIZE,
        .description = "push.stack_depth on empty stack",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_stack_depth_tracking",
        .bytecode = BYTECODE_TEST_PUSH_STACK_DEPTH_TRACKING,
        .size = BYTECODE_TEST_PUSH_STACK_DEPTH_TRACKING_SIZE,
        .description = "push.stack_depth tracks stack elements",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_tag_execution",
        .bytecode = BYTECODE_TEST_PUSH_TAG_EXECUTION,
        .size = BYTECODE_TEST_PUSH_TAG_EXECUTION_SIZE,
        .description = "Push tag value onto the stack",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_type_execution",
        .bytecode = BYTECODE_TEST_PUSH_TYPE_EXECUTION,
        .size = BYTECODE_TEST_PUSH_TYPE_EXECUTION_SIZE,
        .description = "Push various type values onto the stack",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_push_type_with_segment_create_tagged",
        .bytecode = BYTECODE_TEST_PUSH_TYPE_WITH_SEGMENT_CREATE_TAGGED,
        .size = BYTECODE_TEST_PUSH_TYPE_WITH_SEGMENT_CREATE_TAGGED_SIZE,
        .description = "End-to-end typed segment creation pipeline",
        .expected_error = SDDL2_OK,
        .input_size = 12,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_segment_unspecified",
        .bytecode = BYTECODE_TEST_SEGMENT_UNSPECIFIED,
        .size = BYTECODE_TEST_SEGMENT_UNSPECIFIED_SIZE,
        .description = "Simple segment creation (unspecified type)",
        .expected_error = SDDL2_OK,
        .input_size = 5,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_segment_zero",
        .bytecode = BYTECODE_TEST_SEGMENT_ZERO,
        .size = BYTECODE_TEST_SEGMENT_ZERO_SIZE,
        .description = "Zero-size segment creation",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_drop",
        .bytecode = BYTECODE_TEST_STACK_DROP,
        .size = BYTECODE_TEST_STACK_DROP_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_drop_if_false",
        .bytecode = BYTECODE_TEST_STACK_DROP_IF_FALSE,
        .size = BYTECODE_TEST_STACK_DROP_IF_FALSE_SIZE,
        .description = "stack.drop_if with false condition (zero)",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_drop_if_negative",
        .bytecode = BYTECODE_TEST_STACK_DROP_IF_NEGATIVE,
        .size = BYTECODE_TEST_STACK_DROP_IF_NEGATIVE_SIZE,
        .description = "stack.drop_if with negative value as true",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_drop_if_nonzero",
        .bytecode = BYTECODE_TEST_STACK_DROP_IF_NONZERO,
        .size = BYTECODE_TEST_STACK_DROP_IF_NONZERO_SIZE,
        .description = "stack.drop_if with non-zero value as true",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_drop_if_true",
        .bytecode = BYTECODE_TEST_STACK_DROP_IF_TRUE,
        .size = BYTECODE_TEST_STACK_DROP_IF_TRUE_SIZE,
        .description = "stack.drop_if with true condition (non-zero)",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_drop_if_underflow",
        .bytecode = BYTECODE_TEST_STACK_DROP_IF_UNDERFLOW,
        .size = BYTECODE_TEST_STACK_DROP_IF_UNDERFLOW_SIZE,
        .description = "stack.drop_if with stack underflow (missing condition)",
        .expected_error = SDDL2_STACK_UNDERFLOW,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_drop_if_with_cmp",
        .bytecode = BYTECODE_TEST_STACK_DROP_IF_WITH_CMP,
        .size = BYTECODE_TEST_STACK_DROP_IF_WITH_CMP_SIZE,
        .description = "stack.drop_if combined with comparison",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_drop_underflow",
        .bytecode = BYTECODE_TEST_STACK_DROP_UNDERFLOW,
        .size = BYTECODE_TEST_STACK_DROP_UNDERFLOW_SIZE,
        .description = "STACK drop with underflow",
        .expected_error = SDDL2_STACK_UNDERFLOW,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_dup",
        .bytecode = BYTECODE_TEST_STACK_DUP,
        .size = BYTECODE_TEST_STACK_DUP_SIZE,
        .description = "STACK dup operation",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_dup_underflow",
        .bytecode = BYTECODE_TEST_STACK_DUP_UNDERFLOW,
        .size = BYTECODE_TEST_STACK_DUP_UNDERFLOW_SIZE,
        .description = "STACK dup with underflow",
        .expected_error = SDDL2_STACK_UNDERFLOW,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_operations_mixed_types",
        .bytecode = BYTECODE_TEST_STACK_OPERATIONS_MIXED_TYPES,
        .size = BYTECODE_TEST_STACK_OPERATIONS_MIXED_TYPES_SIZE,
        .description = "STACK operations with mixed value types",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_swap",
        .bytecode = BYTECODE_TEST_STACK_SWAP,
        .size = BYTECODE_TEST_STACK_SWAP_SIZE,
        .description = "STACK swap operation",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_stack_swap_underflow",
        .bytecode = BYTECODE_TEST_STACK_SWAP_UNDERFLOW,
        .size = BYTECODE_TEST_STACK_SWAP_UNDERFLOW_SIZE,
        .description = "STACK swap with underflow (only 1 element)",
        .expected_error = SDDL2_STACK_UNDERFLOW,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_type_fixed_array_execution",
        .bytecode = BYTECODE_TEST_TYPE_FIXED_ARRAY_EXECUTION,
        .size = BYTECODE_TEST_TYPE_FIXED_ARRAY_EXECUTION_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_type_fixed_array_with_segment",
        .bytecode = BYTECODE_TEST_TYPE_FIXED_ARRAY_WITH_SEGMENT,
        .size = BYTECODE_TEST_TYPE_FIXED_ARRAY_WITH_SEGMENT_SIZE,
        .description = "segment creation with array type",
        .expected_error = SDDL2_OK,
        .input_size = 40,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_type_structure",
        .bytecode = BYTECODE_TEST_TYPE_STRUCTURE,
        .size = BYTECODE_TEST_TYPE_STRUCTURE_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
    {
        .name = "test_zero_size_segment",
        .bytecode = BYTECODE_TEST_ZERO_SIZE_SEGMENT,
        .size = BYTECODE_TEST_ZERO_SIZE_SEGMENT_SIZE,
        .description = "",
        .expected_error = SDDL2_OK,
        .input_size = 0,
        .skip = 0,
        .custom_validator = NULL
    },
};

static const size_t SDDL2_BYTECODE_TEST_COUNT = 52;

#endif // GENERATED_TEST_BYTECODE_H
