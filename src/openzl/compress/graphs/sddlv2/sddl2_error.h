// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * SDDL2 Error Handling
 *
 * Provides:
 * - SDDL2_error enum - Unified error codes for all VM operations
 * - SDDL2_TRY() macro - Propagate errors up the call stack
 */

#ifndef SDDL2_ERROR_H
#define SDDL2_ERROR_H

#if defined(__cplusplus)
extern "C" {
#endif

/* ============================================================================
 * Error Codes
 * ========================================================================= */

/**
 * VM error codes.
 * Used as return values for all VM operations.
 */
typedef enum {
    SDDL2_OK = 0,              // Success
    SDDL2_STACK_OVERFLOW,      // Stack capacity exceeded
    SDDL2_STACK_UNDERFLOW,     // Pop from empty stack
    SDDL2_TYPE_MISMATCH,       // Operation received wrong value type
    SDDL2_LOAD_BOUNDS,         // Load address out of bounds
    SDDL2_SEGMENT_BOUNDS,      // Segment extends beyond input buffer
    SDDL2_LIMIT_EXCEEDED,      // Maximum capacity limit exceeded
    SDDL2_DIV_ZERO,            // Division by zero
    SDDL2_INVALID_BYTECODE     // Malformed or invalid bytecode
} SDDL2_error;

/* ============================================================================
 * Error Handling Macros
 * ========================================================================= */

/**
 * Try an operation, return on failure.
 *
 * Usage:
 *   SDDL2_TRY(pop_i64(stack, &value));
 *   SDDL2_TRY(SDDL2_stack_push(&stack, result));
 *
 * Expands to:
 *   do {
 *       SDDL2_error _err = (operation);
 *       if (_err != SDDL2_OK)
 *           return _err;
 *   } while (0)
 */
#define SDDL2_TRY(operation)            \
    do {                                \
        SDDL2_error _err = (operation); \
        if (_err != SDDL2_OK)           \
            return _err;                \
    } while (0)

#if defined(__cplusplus)
}
#endif

#endif // SDDL2_ERROR_H
