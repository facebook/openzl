// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * \file
 *
 * This header file defines the public interface for zstrong's error reporting
 * and processing mechanisms.
 */

#ifndef ZSTRONG_ZS2_ERRORS_H
#define ZSTRONG_ZS2_ERRORS_H

#include <assert.h>
#include <stddef.h> // size_t

#include "openzl/zl_errors_types.h"  // ZL_ErrorCode
#include "openzl/zl_macro_helpers.h" // ZS_MACRO_PAD1 and friends
#include "openzl/zl_portability.h"   // ZL_INLINE

#include "openzl/detail/zl_errors_detail.h" // implementation details

#if defined(__cplusplus)
extern "C" {
#endif

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// Note: the list of error codes is maintained within zs2_errors_types.h
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

/**********************************
 * ZS2_Result Handling in Zstrong *
 **********************************/

/**
 * Zstrong employs the `ZS2_Result` type as a primary method for error handling
 * and value returning. A `ZS2_Result` acts as a sum-type, encapsulating either
 * an error or a successful value, ensuring that error checking is systematic
 * and robust across the library.
 *
 * Usage:
 * - A `ZS2_Result` must always be checked using `ZL_RES_isError` before
 * accessing the value.
 * - On success, the value can be retrieved using `ZL_RES_value`.
 * - On error, the error code is accessible through `ZL_RES_code`, the
 * value part is undefined and should not be accessed.
 *
 * Example:
 * ```c
 * ZL_RESULT_OF(int) result = some_function_returning_result_int();
 * if (ZL_RES_isError(result)) {
 *     handle_error(ZL_RES_code(result));
 * } else {
 *     int value = ZL_RES_value(result);
 *     process_value(value);
 * }
 * ```
 *
 * Error forwarding is facilitated by a unified error ID space within Zstrong,
 * allowing errors from any part of the library to be handled uniformly.
 *
 * `ZL_Report` is a commonly used `Result` type that returns either a `size_t`
 * or an error code. Additional `Result` types can be declared with
 * `ZL_RESULT_DECLARE_TYPE(TypeName)`, creating a new type
 * `ZL_RESULT_OF(TypeName)`.
 */

/***********************
 * Zstrong Rich Errors *
 ***********************/

/**
 * When an error occurs, Zstrong can provide a rich error message using the
 * context object where the error has ocurred. This message is a string that can
 * be retrieved using ZS2_{context}_getErrorContextString()  which contains the
 * call stack of the error source. For example, @ref
 * ZL_CCtx_getErrorContextString is the variant that takes the compression
 * context as input. Below is an example of printing the rich error.
 *
 * Example:
 * ```c
 * ZL_Report const report = ZL_CCtx_compress(cctx, ...);
 * if (ZL_isError(report)) {
 *     fprint(stderr, "%s\n", ZL_CCtx_getErrorContextString(cctx, report));
 * }
 * ```
 */

//////////////////
// Declarations //
//////////////////

// The Result is a templated type that contains either a ZL_Error or an
// instance of the provided type. A declaration of the Result type
// for a particular type must be made via `ZL_RESULT_DECLARE_TYPE(type)`
// before it can be used. The type passed in must be a single, bare type name
// (e.g. `MyThingConstPtr`, but not `const MyThing*`). Typedef might be
// required to satisfy this condition.
// The name of the newly declared Result type is `ZL_RESULT_OF(type)`.
#define ZL_RESULT_DECLARE_TYPE(type) \
    ZL_RESULT_DECLARE_TYPE_IMPL(ZL_RESULT_OF(type), type)

// The type of the wrapping Result<T> type for a given inner type T. This
// Result wrapper must have been previously declared somewhere with
// ZL_RESULT_DECLARE_TYPE(type).
#define ZL_RESULT_OF(type) ZS_MACRO_CONCAT(ZL_Result_, type)

/**
 * This macro sets up a scope to be able to use the error-handling macros
 * defined below.
 *
 * @param _return_type_inner should name inner type of the ZL_Result type this
 *                           function returns. I.e., if your function returns
 *                           `ZL_RESULT_OF(T)`, you should pass `T` in here.
 * @param _context must be a pointer to one of the types which @ref
 *                ZL_GET_OPERATION_CONTEXT accepts.
 *
 * You should use it like:
 *
 * ```c
 * ZL_RESULT_OF(Foo) get_foo_from_compressor(
 *     ZL_Compressor* compressor, size_t idx) {
 *   ZL_RESULT_DECLARE_SCOPE(Foo, compressor);
 *
 *   // ...
 *
 *   ZL_ERR_IF_LE(ZL_Compressor_numFoos(compressor), idx, graph_invalid);
 *
 *   // ...
 *
 *   const Foo foo = ZL_Compressor_getFoo(compressor, idx);
 *   return ZL_WRAP_VALUE(foo);
 * }
 * ```
 */
#define ZL_RESULT_DECLARE_SCOPE(_return_type_inner, _context)               \
    typedef _return_type_inner ZL__RetTypeInner;                            \
    typedef ZL_RESULT_OF(_return_type_inner) ZL__RetType;                   \
    ZL_ErrorContext ZL__errorContext = { .opCtx = ZL_GET_OPERATION_CONTEXT( \
                                                 _context) };               \
    if (0) {                                                                \
        /* Use the typedefs to avoid -Wunused-local-typedef */              \
        ZL__RetTypeInner* ZL__tmpRetTypeInner = NULL;                       \
        ZL__RetType* ZL__tmpRetType           = NULL;                       \
        (void)ZL__tmpRetTypeInner, (void)ZL__tmpRetType;                    \
    }                                                                       \
    (void)ZL__errorContext

/// Helper to declare a result scope for functions which return ZL_Report
#define ZL_RESULT_DECLARE_SCOPE_REPORT(_context) \
    ZL_RESULT_DECLARE_SCOPE(size_t, _context)

#define ZL_ERR_CTX_PTR (&ZL__errorContext)

/**
 * Updates the error context. Useful when the context you want to use first has
 * to be initialized or pre-validated first.
 *
 * ```
 * ZL_Report serialize_something(...) {
 *   ZL_RESULT_DECLARE_SCOPE(Foo, NULL);
 *   ZL_CompressorSerializer serializer;
 *   ZL_ERR_IF_ERR(ZL_CompressorSerializer_init(&serializer));
 *   ZL_RESULT_UPDATE_SCOPE_CONTEXT(&serializer);
 *   // ...
 * }
 * ```
 */
#define ZL_RESULT_UPDATE_SCOPE_CONTEXT(_context)      \
    do {                                              \
        ZL__errorContext.opCtx = (_context) == NULL   \
                ? NULL                                \
                : ZL_GET_OPERATION_CONTEXT(_context); \
    } while (0)

///////////////
// Accessors //
///////////////

// Test if the Result is an error. Returns true if it is.
#define ZL_RES_isError(res) ((res)._code != ZL_ErrorCode_no_error)

// Returns the encapsulated value (on success only)
#define ZL_RES_value(res) ((res)._value._value)

// Returns the error code, of type ZL_ErrorCode
#define ZL_RES_code(res) ((res)._code)

/*-*********************************************
 *  Common Result types
 *-********************************************/

ZL_RESULT_DECLARE_TYPE(ZL_GraphID);
ZL_RESULT_DECLARE_TYPE(ZL_NodeID);

/*-*********************************************
 *  ZL_Report type
 *-*********************************************
 * ZL_Report is a priviledged Result type, which is employed everywhere within
 * zstrong. It is a Result of size_t, which means it represents either an error
 * or a size_t.
 *
 * When reporting an error, it can communicate an error code.
 * When reporting a success, it can communicate a quantity (typically a size).
 */

ZL_RESULT_DECLARE_TYPE(size_t);
typedef ZL_RESULT_OF(size_t) ZL_Report;

/// @returns true iff the report contains an error
ZL_INLINE int ZL_isError(ZL_Report report)
{
    return ZL_RES_isError(report);
}

/// @pre !ZL_isError(report)
/// @returns The value contained within the report
ZL_INLINE size_t ZL_validResult(ZL_Report report)
{
    assert(!ZL_isError(report));
    return ZL_RES_value(report);
}

/// @returns The error code contained within the report.
/// If !ZL_isError(report), then @returns ZL_ErrorCode_no_error.
ZL_INLINE ZL_ErrorCode ZL_errorCode(ZL_Report report)
{
    return ZL_RES_code(report);
}

// Retrieves a static string descriptor for a given error code.
const char* ZL_ErrorCode_toString(ZL_ErrorCode code);

/*-*********************************************
 *  ZL_Report generation
 *-*********************************************
 * Users may need to generate ZL_Report values,
 * for example within a Transform's binding.
 * The following methods makes this possible.
 */

/**
 * @returns a successful report containing a value.
 */
ZL_INLINE ZL_Report ZL_returnValue(size_t s)
{
    ZL_Report report;
    report._value._code  = ZL_ErrorCode_no_error;
    report._value._value = s;
    return report;
}

/**
 * @returns a successful report without a value (set to zero).
 */
ZL_INLINE ZL_Report ZL_returnSuccess(void)
{
    return ZL_returnValue(0);
}

/**
 * @returns a specific ZL_ErrorCode as a ZL_Report return type.
 */
ZL_Report ZL_returnError(ZL_ErrorCode err);

// Below API allows users to specify a formatted message when they
// create and return an error.
//
// Note(@Cyan): What's the current status of this error message ? Is it
// displayed in logs ? Is it tracked or registered in the Error object ?
//
// ZL_REPORT_ERROR() takes an error code suffix, e.g., `allocation`.
// ZL_REPORT_ERROR_CODE() takes an error code variable, or a full enum name
//
#define ZL_REPORT_ERROR(...) \
    ZS_MACRO_PAD2(ZL_RESULT_MAKE_ERROR, size_t, __VA_ARGS__)

#define ZL_REPORT_ERROR_CODE(...) \
    ZS_MACRO_PAD2(ZL_RESULT_MAKE_ERROR_CODE, size_t, __VA_ARGS__)

// Implementation detail (do not use directly)
ZL_Report ZL_reportError(
        const char* file,
        const char* func,
        int line,
        ZL_ErrorCode err,
        const char* fmt,
        ...);

/**
 * @returns `ZL_OperationContext*`, a possibly-`NULL` pointer to the
 * operation context owned by the provided @p context, which is where rich
 * error information is stored. It takes a mutable pointer to one of the
 * following types:
 *
 * - ZL_CCtx
 * - ZL_DCtx
 * - ZL_Encoder
 * - ZL_Decoder
 * - ZL_Graph
 * - ZL_Edge
 * - ZL_CompressorSerializer
 * - ZL_CompressorDeserializer
 * - ZL_Segmenter
 *
 * It will return `NULL` if the provided @p context is `NULL`.
 */
#define ZL_GET_OPERATION_CONTEXT(context) ZL_GET_OPERATION_CONTEXT_IMPL(context)

/**
 * @returns `ZL_ErrorContext*`, a possibly-`NULL` pointer to the default
 * error context owned by the provided @p context, which is used by the error
 * system to (1) store rich error information and (2) provide context to
 * errors that are created. It takes a mutable pointer to one of the
 * following types:
 *
 * - ZL_CCtx
 * - ZL_DCtx
 * - ZL_Encoder
 * - ZL_Decoder
 * - ZL_Graph
 * - ZL_Edge
 * - ZL_CompressorSerializer
 * - ZL_CompressorDeserializer
 * - ZL_Segmenter
 *
 * It will return `NULL` if the provided @p context is `NULL`.
 */
#define ZL_GET_DEFAULT_ERROR_CONTEXT(context) ZL_GET_ERROR_CONTEXT_IMPL(context)

/////////////////////////////////////////////
// New and Improved Error Handling Macros! //
/////////////////////////////////////////////

/**
 * Use these macros in functions in which you've previously invoked @ref
 * ZL_RESULT_DECLARE_SCOPE(), which means that you don't need to provide the
 * function's return type as part of every macro invocation!
 */

#define ZL_ERR_IF(expr, ...) \
    ZS_MACRO_PAD2(ZL_ERR_IF_UNARY_IMPL, expr, __VA_ARGS__)
#define ZL_ERR_IF_NOT(expr, ...) \
    ZS_MACRO_PAD2(ZL_ERR_IF_UNARY_IMPL, !(expr), __VA_ARGS__)
#define ZL_ERR_IF_EQ(lhs, rhs, ...) \
    ZS_MACRO_PAD4(ZL_ERR_IF_BINARY_IMPL, lhs, ==, rhs, __VA_ARGS__)
#define ZL_ERR_IF_NE(lhs, rhs, ...) \
    ZS_MACRO_PAD4(ZL_ERR_IF_BINARY_IMPL, lhs, !=, rhs, __VA_ARGS__)
#define ZL_ERR_IF_GE(lhs, rhs, ...) \
    ZS_MACRO_PAD4(ZL_ERR_IF_BINARY_IMPL, lhs, >=, rhs, __VA_ARGS__)
#define ZL_ERR_IF_LE(lhs, rhs, ...) \
    ZS_MACRO_PAD4(ZL_ERR_IF_BINARY_IMPL, lhs, <=, rhs, __VA_ARGS__)
#define ZL_ERR_IF_GT(lhs, rhs, ...) \
    ZS_MACRO_PAD4(ZL_ERR_IF_BINARY_IMPL, lhs, >, rhs, __VA_ARGS__)
#define ZL_ERR_IF_LT(lhs, rhs, ...) \
    ZS_MACRO_PAD4(ZL_ERR_IF_BINARY_IMPL, lhs, <, rhs, __VA_ARGS__)
#define ZL_ERR_IF_AND(lhs, rhs, ...) \
    ZS_MACRO_PAD4(ZL_ERR_IF_BINARY_IMPL, lhs, &&, rhs, __VA_ARGS__)
#define ZL_ERR_IF_OR(lhs, rhs, ...) \
    ZS_MACRO_PAD4(ZL_ERR_IF_BINARY_IMPL, lhs, ||, rhs, __VA_ARGS__)
#define ZL_ERR_IF_NN(expr, ...)             \
    ZS_MACRO_PAD4(                          \
            ZL_ERR_IF_BINARY_IMPL,          \
            (const char*)(const void*)expr, \
            !=,                             \
            (const char*)0,                 \
            __VA_ARGS__)
#define ZL_ERR_IF_NULL(expr, ...)           \
    ZS_MACRO_PAD4(                          \
            ZL_ERR_IF_BINARY_IMPL,          \
            (const char*)(const void*)expr, \
            ==,                             \
            (const char*)0,                 \
            __VA_ARGS__)

/**
 * If the provided expression evaluates to an error, bubbles that error up the
 * stack by returning it from the current scope.
 */
#define ZL_ERR_IF_ERR(...) ZS_MACRO_PAD1(ZL_ERR_IF_ERR_IMPL, __VA_ARGS__)

/// Unconditionally construct and return an error.
#define ZL_ERR(...) ZS_MACRO_PAD1(ZL_ERR_RET_IMPL, __VA_ARGS__)

/// Turn a @p value into a result.
#if defined(__cplusplus)
} // extern "C"

// C++ compatible version using helper function
#    define ZL_WRAP_VALUE(value) \
        (::openzl::detail::make_result_with_value<ZL__RetType>(value))
extern "C" {
#else
// C99 compound literal
#    define ZL_WRAP_VALUE(value)                                     \
        ((ZL__RetType){ ._value = { ._code  = ZL_ErrorCode_no_error, \
                                    ._value = (value) } })
#endif

/// Turn a @p error into a result, and try to add a stack frame to the traceback
/// in the error.
#define ZL_WRAP_ERROR(error) ZL_WRAP_ERROR_ADD_FRAME(error)

/// Turn a @p error into a result, without trying to record a stack frame.
#if defined(__cplusplus)
} // extern "C"
// C++ compatible version using helper function
#    define ZL_WRAP_ERROR_NO_FRAME(error) \
        (::openzl::detail::make_result_with_error<ZL__RetType>(error))
extern "C" {
#else
// C99 compound literal
#    define ZL_WRAP_ERROR_NO_FRAME(error) ((ZL__RetType){ ._error = (error) })
#endif

#define ZL_TRY_SET(_type, _var, _expr) ZL_TRY_SET_DT(_type, _var, _expr)

#define ZL_TRY_LET(_type, _var, _expr) ZL_TRY_LET_DT(_type, _var, _expr)

#define ZL_TRY_LET_CONST(_type, _var, _expr) \
    ZL_TRY_LET_CONST_DT(_type, _var, _expr)

///////////////////////////////////////////////
// Enhanced Control Flow with Error Handling //
///////////////////////////////////////////////

/**
 * In complex functions with multiple conditional checks, it is often necessary
 * to exit early when a condition that leads to an error is met. The following
 * macros facilitate this pattern by checking conditions and returning an error
 * immediately if the condition is true. This approach simplifies error handling
 * by avoiding deep nesting and improving readability.
 *
 * Usage Pattern:
 * - These macros check a specified condition and, if true, construct and return
 * an error, effectively exiting the function.
 * - If the condition is false, the function's execution continues as normal.
 * - Errors are constructed using a specified error suffix, from
 * 'zs2_errors_types.h'.
 * - Optionally, a format string can be provided for detailed error messages,
 * enhancing debugging and logging capabilities.
 *
 * Example:
 * ```c
 *  // Early exit if pointer allocation fails, with formatted error message
 *  ZL_RET_R_IF(allocation, ptr == NULL, "Failed to allocate %zu bytes", size);
 * ```
 *
 * Available Macros:
 * - `ZL_RET_R_IF(...)`: Checks a condition, returns an error if true.
 * - `ZL_RET_R_IF_NOT(...)`: Logical negation of `ZL_RET_R_IF`.
 * - `ZL_RET_R_IF_NULL(...)`: Checks if pointer is NULL, returns an error.
 * - `ZL_RET_R_IF_NN(...)`: Inverse of `ZL_RET_R_IF_NULL`.
 * - Comparison Macros: `ZL_RET_R_IF_EQ()`, `ZL_RET_R_IF_NE()`,
 * `ZL_RET_R_IF_GE()`, etc., for specific relational checks.
 * - Logical Macros: `ZL_RET_R_IF_AND()`, `ZL_RET_R_IF_OR()`, to handle
 * compound conditions.
 * - `ZL_RET_R_IF_ERR(...)`: returns if provided ZL_Report is an error
 * - `ZL_RET_R_ERR(...)`: Unconditionally returns an error with optional
 * formatted message.
 * - `ZL_RET_R(res)`: Directly returns the provided `ZL_Report`.
 */

#define ZL_RET_R_IF(...) ZL_RET_T_IF(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_NOT(...) ZL_RET_T_IF_NOT(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_NULL(...) ZL_RET_T_IF_NULL(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_NN(...) ZL_RET_T_IF_NN(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_EQ(...) ZL_RET_T_IF_EQ(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_NE(...) ZL_RET_T_IF_NE(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_GE(...) ZL_RET_T_IF_GE(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_LE(...) ZL_RET_T_IF_LE(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_GT(...) ZL_RET_T_IF_GT(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_LT(...) ZL_RET_T_IF_LT(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_AND(...) ZL_RET_T_IF_AND(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_OR(...) ZL_RET_T_IF_OR(size_t, __VA_ARGS__)
#define ZL_RET_R_IF_ERR(...) ZL_RET_T_IF_ERR(size_t, __VA_ARGS__)

/// Unconditionally constructs and returns an error.
/// Supports optional formatted string.
#define ZL_RET_R_ERR(...) ZL_RET_T_ERR(size_t, __VA_ARGS__)

/// Unconditionally return a `ZL_Report` wrapping @p err, which must be a
// `ZL_Error`.
#define ZL_RET_R_WRAP_ERR(err) ZL_RET_T_WRAP_ERR(size_t, err)

/// Unconditionally return a `ZL_Report` wrapping @p val, which must be a
/// `size_t`.
#define ZL_RET_R_VAL(val) \
    ZL_RET_T_RES(size_t, ZL_RESULT_WRAP_VALUE(size_t, val))

/// Unconditionally returns @p res, which must be a ZL_Report.
#define ZL_RET_R(res) ZL_RET_T_RES(size_t, res)

/**
 * These macros conditionally set a variable @p _var to the value in the
 * result returned by the expression @p _expr. If @p _expr evaluates to an
 * error, these macros cause the calling context to bubble up that error by
 * returning it. If it evaluates to a result that contains a value, it unwraps
 * that value and assigns it into the named variable.
 *
 * Equivalent to rust's ? operator, e.g. `let x: i32 = "123".parse()?;`.
 *
 * `ZL_TRY_SET_R()` assumes @p _var already exists. `ZL_TRY_LET_R()` and
 * `ZL_TRY_LET_CONST_R()` assume that @p _var doesn't already exist, and
 * declares such a variable. As the name hints, the variable that
 * `ZL_TRY_LET_CONST_R()` declares is declared as `const`.
 *
 * In these untyped variants, @p _expr must evaluate to a @ref ZL_Report,
 * @p _var must name a variable that either already is or will be of type
 * `size_t`, and the function in which these macros are invoked must have a
 * return type of @ref ZL_Report. Typed variants (with `_T` or `_TT` suffixes)
 * are described below, for when that's not the case, e.g., @ref ZL_TRY_SET_T
 * and @ref ZL_TRY_SET_TT.
 */

#define ZL_TRY_SET_R(_var, _expr) ZL_TRY_SET_T(size_t, _var, _expr)
#define ZL_TRY_LET_R(_var, _expr) ZL_TRY_LET_T(size_t, _var, _expr)
#define ZL_TRY_LET_CONST_R(_var, _expr) ZL_TRY_LET_CONST_T(size_t, _var, _expr)

/**
 * The following set of macros offer the same capabilities as above, but for
 * the more generic ZL_RESULT_OF(type) return type. As a consequence, the type
 * must be provided explicitly as the first argument of the macro.
 */

#define ZL_RET_T_IF(type, errcode, ...) \
    ZS_MACRO_PAD3(ZL_RET_IF_UNARY_IMPL, type, errcode, __VA_ARGS__)
#define ZL_RET_T_IF_NOT(type, errcode, ...) \
    ZS_MACRO_PAD3(ZL_RET_IF_NOT_UNARY_IMPL, type, errcode, __VA_ARGS__)
#define ZL_RET_T_IF_EQ(type, errcode, ...) \
    ZS_MACRO_PAD5(ZL_RET_IF_BINARY_IMPL, type, errcode, ==, __VA_ARGS__)
#define ZL_RET_T_IF_NE(type, errcode, ...) \
    ZS_MACRO_PAD5(ZL_RET_IF_BINARY_IMPL, type, errcode, !=, __VA_ARGS__)
#define ZL_RET_T_IF_GE(type, errcode, ...) \
    ZS_MACRO_PAD5(ZL_RET_IF_BINARY_IMPL, type, errcode, >=, __VA_ARGS__)
#define ZL_RET_T_IF_LE(type, errcode, ...) \
    ZS_MACRO_PAD5(ZL_RET_IF_BINARY_IMPL, type, errcode, <=, __VA_ARGS__)
#define ZL_RET_T_IF_GT(type, errcode, ...) \
    ZS_MACRO_PAD5(ZL_RET_IF_BINARY_IMPL, type, errcode, >, __VA_ARGS__)
#define ZL_RET_T_IF_LT(type, errcode, ...) \
    ZS_MACRO_PAD5(ZL_RET_IF_BINARY_IMPL, type, errcode, <, __VA_ARGS__)
#define ZL_RET_T_IF_AND(type, errcode, ...) \
    ZS_MACRO_PAD5(ZL_RET_IF_BINARY_IMPL, type, errcode, &&, __VA_ARGS__)
#define ZL_RET_T_IF_OR(type, errcode, ...) \
    ZS_MACRO_PAD5(ZL_RET_IF_BINARY_IMPL, type, errcode, ||, __VA_ARGS__)
#define ZL_RET_T_IF_NN(type, errcode, ...) \
    ZS_MACRO_PAD3(ZL_RET_IF_NN_IMPL, type, errcode, __VA_ARGS__)
#define ZL_RET_T_IF_NULL(type, errcode, ...) \
    ZS_MACRO_PAD3(ZL_RET_IF_NULL_IMPL, type, errcode, __VA_ARGS__)

/// Unconditionally construct and return an error.
#define ZL_RET_T_ERR(type, ...) \
    ZS_MACRO_PAD2(ZL_RET_ERR_IMPL, type, __VA_ARGS__)

/// Unconditionally return a `ZL_RESULT_OF(type)` wrapping @p err, which must
/// be a `ZL_Error`.
#define ZL_RET_T_WRAP_ERR(type, err) \
    ZL_RET_T_RES(type, ZL_RESULT_WRAP_ERROR(type, err))

/// Unconditionally return a `ZL_RESULT_OF(type)` wrapping @p val, which must
/// be of type @p type.
#define ZL_RET_T_VAL(type, val) \
    ZL_RET_T_RES(type, ZL_RESULT_WRAP_VALUE(type, val))

/// Unconditionally returns @p res, which must be of type ZL_RESULT_OF(type).
#define ZL_RET_T_RES(type, res) ZL_RET_IMPL(type, res)

/**
 * In functions that return a ZL_RESULT_OF(type), this is a quick helper macro
 * to bail if an internal call failed (produced a ZL_RESULT_OF(*) containing
 * an error).
 */
#define ZL_RET_T_IF_ERR(type, ...) \
    ZS_MACRO_PAD2(ZL_RET_IF_ERR_IMPL, type, __VA_ARGS__)

/**
 * Like @ref ZL_TRY_SET_R() and friends, but generified across different types.
 * I.e., it doesn't assume the result type of @p _expr is @ref ZL_Report and
 * that the type of @p _var is `size_t`. Instead, it takes @p _type, which
 * indicates both the type of @p _var and also the inner type of the @ref
 * ZS2_Result that @p _expr returns.
 */

#define ZL_TRY_SET_T(_type, _var, _expr) \
    ZL_TRY_SET_TT(size_t, _type, _var, _expr)

#define ZL_TRY_LET_T(_type, _var, _expr) \
    ZL_TRY_LET_TT(size_t, _type, _var, _expr)

#define ZL_TRY_LET_CONST_T(_type, _var, _expr) \
    ZL_TRY_LET_CONST_TT(size_t, _type, _var, _expr)

/**
 * Like ZL_TRY_LET_T() but doesn't assume the outer/return type is a
 * ZL_Report. Instead, takes two types, the outer type that this may return
 * as well as the inner type which is the type of result that the expression
 * produces.
 */
#define ZL_TRY_SET_TT(_outer_type, _inner_type, _var, _expr) \
    do {                                                     \
        ZL_RESULT_OF(_inner_type) _report = (_expr);         \
        ZL_RET_T_IF_ERR(_outer_type, _report);               \
        _var = ZL_RES_value(_report);                        \
    } while (0)

#define ZL_TRY_LET_TT(_outer_type, _inner_type, _var, _expr) \
    _inner_type _var;                                        \
    ZL_TRY_SET_TT(_outer_type, _inner_type, _var, _expr)

/**
 * Like ZL_TRY_LET_TT() but the variable it defines is declared as
 * const.
 */
#define ZL_TRY_LET_CONST_TT(_outer_type, _inner_type, _var, _expr)       \
    ZL_Error ZS_MACRO_CONCAT(_var, _tmp_error_storage);                  \
    const _inner_type _var =                                             \
            ZS_MACRO_CONCAT(ZL_RESULT_OF(_inner_type), _extract)(        \
                    _expr, &ZS_MACRO_CONCAT(_var, _tmp_error_storage));  \
    if (ZS_MACRO_CONCAT(_var, _tmp_error_storage)._code                  \
        != ZL_ErrorCode_no_error) {                                      \
        ZL_RET_T_WRAP_ERR(                                               \
                _outer_type, ZS_MACRO_CONCAT(_var, _tmp_error_storage)); \
    }                                                                    \
    do {                                                                 \
    } while (0)

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_ERRORS_H
