// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * SDDL2 Error Handling
 *
 * Provides:
 * - SDDL2_Error enum - Unified error codes for all VM operations
 * - SDDL2_RESULT_OF(type) - Generic Result type macro
 * - SDDL2_TRY() macro - Propagate errors up the call stack
 */

#ifndef SDDL2_ERROR_H
#define SDDL2_ERROR_H

#include <stddef.h> // size_t

#include "openzl/zl_macro_helpers.h" // ZS_MACRO_CONCAT
#include "openzl/zl_portability.h"   // ZL_NODISCARD

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
    SDDL2_OK = 0,            // Success
    SDDL2_STACK_OVERFLOW,    // Stack capacity exceeded
    SDDL2_STACK_UNDERFLOW,   // Pop from empty stack
    SDDL2_MATH_OVERFLOW,     // Arithmetic overflow
    SDDL2_TYPE_MISMATCH,     // Operation received wrong value type
    SDDL2_LOAD_BOUNDS,       // Load address out of bounds
    SDDL2_SEGMENT_BOUNDS,    // Segment extends beyond input buffer
    SDDL2_LIMIT_EXCEEDED,    // Maximum capacity limit exceeded
    SDDL2_DIV_ZERO,          // Division by zero
    SDDL2_ALLOCATION_FAILED, // Memory allocation failed
    SDDL2_INVALID_BYTECODE,  // Malformed or invalid bytecode
    SDDL2_VALIDATION_FAILED  // Runtime validation/assertion failed
} SDDL2_Error;

/* ============================================================================
 * SDDL2_RESULT_OF - Generic Result Type
 * ============================================================================
 *
 * Creates a Result type that bundles a value with an error code.
 * Eliminates out parameters and triggers warnings if return value is ignored.
 *
 * Usage:
 *   SDDL2_RESULT_DECLARE_TYPE(size_t);  // declare once
 *
 *   SDDL2_RESULT_OF(size_t) result = some_function();
 *   if (SDDL2_isError(result)) { handle_error(SDDL2_error(result)); }
 *   size_t value = SDDL2_value(result);
 */

#define SDDL2_RESULT_OF(type) ZS_MACRO_CONCAT(SDDL2_Result_, type)

#define SDDL2_RESULT_DECLARE_TYPE(type) \
    typedef struct ZL_NODISCARD {       \
        SDDL2_Error _code;              \
        type _value;                    \
    } SDDL2_RESULT_OF(type)

/* Generic accessors */
#define SDDL2_isError(result) ((result)._code != SDDL2_OK)
#define SDDL2_error(result) ((result)._code)
#define SDDL2_value(result) ((result)._value)

/* Generic constructors */
#if defined(__cplusplus)
#    define SDDL2_success(type, val) (SDDL2_RESULT_OF(type){ SDDL2_OK, (val) })
#    define SDDL2_failure(type, err) (SDDL2_RESULT_OF(type){ (err), {} })
#else
#    define SDDL2_success(type, val) \
        ((SDDL2_RESULT_OF(type)){ ._code = SDDL2_OK, ._value = (val) })
#    define SDDL2_failure(type, err) ((SDDL2_RESULT_OF(type)){ ._code = (err) })
#endif

/* Pre-declare size_t Result type (used in public API) */
SDDL2_RESULT_DECLARE_TYPE(size_t);

/* ============================================================================
 * Error Handling Macros
 * ========================================================================= */

/**
 * Try an operation that returns SDDL2_Error, return on failure.
 */
#define SDDL2_TRY(operation)            \
    do {                                \
        SDDL2_Error _err = (operation); \
        if (_err != SDDL2_OK)           \
            return _err;                \
    } while (0)

/**
 * Declare a variable and assign the result value, or return on error.
 * Use in functions that return SDDL2_Error.
 */
#define SDDL2_TRY_LET(type, var, operation)          \
    type var;                                        \
    do {                                             \
        SDDL2_RESULT_OF(type) _result = (operation); \
        if (SDDL2_isError(_result))                  \
            return SDDL2_error(_result);             \
        (var) = SDDL2_value(_result);                \
    } while (0)

#if defined(__cplusplus)
}
#endif

#endif // SDDL2_ERROR_H
