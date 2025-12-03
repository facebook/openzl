// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * \file
 *
 * This file contains implementation details of the error framework that have
 * to be part of the public API but which users should not have to inspect,
 * understand, or even be aware of.
 */

#ifndef ZSTRONG_ZS2_ERRORS_DETAIL_H
#define ZSTRONG_ZS2_ERRORS_DETAIL_H

#include <stddef.h> // size_t
#include <string.h> // memset

#include "openzl/detail/zl_error_context.h"
#include "openzl/zl_errors_types.h"  // ZL_ErrorCode
#include "openzl/zl_macro_helpers.h" // ZS_MACRO_PAD1
#include "openzl/zl_portability.h"   // ZL_INLINE

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef ZL_ERROR_ENABLE_STATIC_ERROR_INFO
#    define ZL_ERROR_ENABLE_STATIC_ERROR_INFO 1
#endif

/*************************************
 * ZL_ErrorCode Description Strings *
 *************************************/

/**
 * Maps the short suffix name for an error (e.g. `corruption`) to the actual
 * enum name (e.g., `ZL_ErrorCode_corruption`).
 */
#define ZL_EXPAND_ERRCODE(shortname) ZS_MACRO_CONCAT(ZL_ErrorCode_, shortname)

/**
 * Having these all be macroes lets us concat this string into static error
 * info strings.
 */

/**
 * Takes an error name and resolves the description string for it.
 */
#define ZL_EXPAND_ERRCODE_DESC_STR(errcode) \
    ZL_ERRCODE_DESC_STR(ZL_EXPAND_ERRCODE(errcode))
#define ZL_ERRCODE_DESC_STR(errcode) ZS_MACRO_CONCAT(errcode, __desc_str)

#define ZL_ErrorCode_no_error__desc_str "No Error"
#define ZL_ErrorCode_GENERIC__desc_str "Generic"
#define ZL_ErrorCode_allocation__desc_str "Allocation"
#define ZL_ErrorCode_srcSize_tooSmall__desc_str "Source size too small"
#define ZL_ErrorCode_dstCapacity_tooSmall__desc_str \
    "Destination capacity too small"
#define ZL_ErrorCode_userBuffer_alignmentIncorrect__desc_str \
    "Buffer provided is incorrectly aligned for target type"
#define ZL_ErrorCode_userBuffers_invalidNum__desc_str \
    "Nb of Typed Buffers provided is incorrect for this frame"
#define ZL_ErrorCode_decompression_incorrectAPI__desc_str \
    "Used an invalid decompression API method for the target Type"
#define ZL_ErrorCode_header_unknown__desc_str "Unknown header"
#define ZL_ErrorCode_frameParameter_unsupported__desc_str \
    "Frame parameter unsupported"
#define ZL_ErrorCode_outputID_invalid__desc_str \
    "Frame doesn't host this many outputs"
#define ZL_ErrorCode_invalidRequest_singleOutputFrameOnly__desc_str \
    "This request only makes sense for Frames hosting a single Output"
#define ZL_ErrorCode_outputNotCommitted__desc_str "Output not committed"
#define ZL_ErrorCode_outputNotReserved__desc_str "Output has no buffer"
#define ZL_ErrorCode_compressionParameter_invalid__desc_str \
    "Compression parameter invalid"
#define ZL_ErrorCode_segmenter_inputNotConsumed__desc_str \
    "Segmenter did not consume entirely all inputs"
#define ZL_ErrorCode_graph_invalid__desc_str "Graph invalid"
#define ZL_ErrorCode_graph_nonserializable__desc_str \
    "Graph incompatible with serialization"
#define ZL_ErrorCode_graph_invalidNumInputs__desc_str "Graph invalid nb inputs"
#define ZL_ErrorCode_successor_invalid__desc_str \
    "Selected an invalid Successor Graph"
#define ZL_ErrorCode_successor_alreadySet__desc_str \
    "A Successor was already assigned for this Stream"
#define ZL_ErrorCode_successor_invalidNumInputs__desc_str \
    "Successor Graph receives an invalid number of Inputs"
#define ZL_ErrorCode_inputType_unsupported__desc_str \
    "Input Type not supported by selected Port"
#define ZL_ErrorCode_graphParameter_invalid__desc_str \
    "Graph was assigned an invalid Local Parameter"
#define ZL_ErrorCode_nodeParameter_invalid__desc_str "Node parameter invalid"
#define ZL_ErrorCode_nodeParameter_invalidValue__desc_str \
    "Node parameter invalid value"
#define ZL_ErrorCode_transform_executionFailure__desc_str \
    "Transform failed during execution"
#define ZL_ErrorCode_customNode_definitionInvalid__desc_str \
    "Custom node definition invalid"
#define ZL_ErrorCode_stream_wrongInit__desc_str \
    "Stream is not in a valid initialization stage"
#define ZL_ErrorCode_streamType_incorrect__desc_str \
    "An incompatible type is being used"
#define ZL_ErrorCode_streamCapacity_tooSmall__desc_str \
    "Stream internal capacity is not sufficient"
#define ZL_ErrorCode_streamParameter_invalid__desc_str \
    "Stream parameter invalid"
#define ZL_ErrorCode_parameter_invalid__desc_str "Parameter is invalid"
#define ZL_ErrorCode_formatVersion_unsupported__desc_str \
    "Format version unsupported"
#define ZL_ErrorCode_formatVersion_notSet__desc_str \
    "Format version is not set; it must be set via the ZL_CParam_formatVersion parameter"
#define ZL_ErrorCode_node_versionMismatch__desc_str \
    "Node is incompatible with requested format version"
#define ZL_ErrorCode_node_unexpected_input_type__desc_str \
    "Unexpected input type for node"
#define ZL_ErrorCode_node_invalid_input__desc_str \
    "Input does not respect conditions for this node"
#define ZL_ErrorCode_node_invalid__desc_str "Invalid Node ID"
#define ZL_ErrorCode_nodeExecution_invalidOutputs__desc_str \
    "node execution has resulted in an incorrect configuration of outputs"
#define ZL_ErrorCode_nodeRegen_countIncorrect__desc_str \
    "node is requested to regenerate an incorrect number of streams"
#define ZL_ErrorCode_logicError__desc_str "Internal logic error"
#define ZL_ErrorCode_invalidTransform__desc_str "Invalid transform ID"
#define ZL_ErrorCode_internalBuffer_tooSmall__desc_str \
    "Internal buffer too small"
#define ZL_ErrorCode_corruption__desc_str "Corruption detected"
#define ZL_ErrorCode_outputs_tooNumerous__desc_str \
    "Too many outputs: unsupported by claimed format version"
#define ZL_ErrorCode_temporaryLibraryLimitation__desc_str \
    "Temporary OpenZL library limitation"
#define ZL_ErrorCode_compressedChecksumWrong__desc_str \
    "Compressed checksum mismatch (corruption after compression)"
#define ZL_ErrorCode_contentChecksumWrong__desc_str \
    "Content checksum mismatch (either corruption after compression or corruption during compression or decompression)"
#define ZL_ErrorCode_srcSize_tooLarge__desc_str "Source size too large"
#define ZL_ErrorCode_integerOverflow__desc_str "Integer overflow"
#define ZL_ErrorCode_invalidName__desc_str "Invalid name of graph component"

/**********************
 * ZL_StaticErrorInfo *
 **********************/

// TODO: resolve ABI concerns by adding a struct version variable at the
// beginning of the struct?
struct ZL_StaticErrorInfo_s {
    ZL_ErrorCode code;
    const char* fmt; // unformatted error message
    const char* file;
    const char* func;
    int line;
};

#if ZL_ERROR_ENABLE_STATIC_ERROR_INFO
// MSVC doesn't allow __func__ in static initializers (it's a variable, not a
// constant) Use __FUNCTION__ instead for MSVC (it's a string literal constant)
#    ifdef _MSC_VER
#        define ZL_FUNC_NAME __FUNCTION__
#    else
#        define ZL_FUNC_NAME __func__
#    endif
#    define ZL_E_DECLARE_STATIC_ERROR_INFO(name, c, f) \
        static const ZL_StaticErrorInfo name = {       \
            c, (f), __FILE__, ZL_FUNC_NAME, __LINE__   \
        }
#    define ZL_E_POINTER_TO_STATIC_ERROR_INFO(name) (&(name))
#else
#    define ZL_E_DECLARE_STATIC_ERROR_INFO(name, f, ...) \
        do {                                             \
        } while (0)
#    define ZL_E_POINTER_TO_STATIC_ERROR_INFO(name) (NULL)
#endif

/*************
 * ZL_Error *
 *************/

/**
 * Internally, there are two kinds of error info objects: dynamic and static,
 * which are, like they sound, respectively dynamically allocated (and which
 * must be freed) or statically allocated, but which therefore can't contain
 * any runtime information.
 *
 * You should never assign or dereference these pointers directly:
 *
 * 1. We pack metadata into the unused bits of the pointers, which needs to be
 *    masked out to retrieve the actual pointer.
 * 2. You need interact with that metadata to figure out or set which pointer
 *    is active.
 *
 * Instead:
 *
 * You should use @ref ZL_E_dy or @ref ZL_E_st to check for the presence of and
 * to extract the (possibly NULL) pointers to the sub-types.
 *
 * You should use @ref ZL_EI_fromDy or @ref ZL_EI_fromSt to construct this
 * object from one of those pointer.
 */
union ZL_ErrorInfo_u {
    ZL_DynamicErrorInfo* _dy;
    const ZL_StaticErrorInfo* _st;
};

/**
 * ZL_Error:
 *
 * The ZL_Error represents an optional failure. (If _code is
 * ZL_ErrorCode_no_error, the object represents a success condition.) Depending
 * on how it was constructed, the error may be "bare" (_info == NULL) or "rich",
 * in which case it has an _info struct that can contain additional context and
 * information about the error.
 *
 * The ZL_Error is usually returned-by-value, and therefore the definition
 * needs to be publicly available. However, users should not directly interact
 * with the members of the struct, and should instead use the various accessors
 * and methods made available in the public API.
 */
struct ZL_Error_s {
    ZL_ErrorCode _code;
    ZL_ErrorInfo _info;
};

#if defined(__cplusplus)
// C++ compatible versions using constructor syntax
#    define ZL_EE_EMPTY (ZL_ErrorInfo{ nullptr })
#    define ZL_E_EMPTY (ZL_Error{ ZL_ErrorCode_no_error, ZL_EE_EMPTY })
#else
// C99 compound literals
#    define ZL_EE_EMPTY ((ZL_ErrorInfo){ ._st = NULL })
#    define ZL_E_EMPTY \
        ((ZL_Error){ ._code = ZL_ErrorCode_no_error, ._info = ZL_EE_EMPTY })
#endif

// Returns the ZL_Error descriptor from a ZL_RESULT_OF(<T>) object.
#define ZL_RES_error(res) ((res)._error)

#define ZL_E_DECLARE_WITH_STATIC_AND_CONTEXT(        \
        name, static_error, scope_context, err, ...) \
    ZL_Error const name = ZL_E_create(               \
            static_error,                            \
            scope_context,                           \
            __FILE__,                                \
            __func__,                                \
            __LINE__,                                \
            ZL_EXPAND_ERRCODE(err),                  \
            __VA_ARGS__)

/**
 * Actual implementation function which accepts all of the explicit arguments
 * that are set up for you by the macros elsewhere. Prefer to use those macros
 * rather than this function directly.
 * - `file` arg is intended to be filled with __FILE__ macro.
 * - `func` arg is intended to be filled with __func__ macro.
 * - `line` arg is intended to be filled with __LINE__ macro.
 */
ZL_Error ZL_E_create(
        ZL_StaticErrorInfo const* st,
        ZL_ErrorContext const* ctx,
        const char* file,
        const char* func,
        int line,
        ZL_ErrorCode code,
        const char* fmt,
        ...);

//////////////////
// Modification //
//////////////////

/**
 * Append a formatted string to the error's message. May be a no-op if the
 * error doesn't have a rich error info set up internally.
 */
void ZL_E_appendToMessage(ZL_Error err, char const* fmt, ...);

/**
 * Attempts to add more information to the error represented by @p error.
 * Narrowly, this means trying to append a stack frame to the stacktrace that
 * rich errors accumulate. In service of that, it also tries to up-convert the
 * error to a rich error if it isn't already. @p fmt and optional additional
 * following args can also be used to append an arbitrary formatted string of
 * information into the error.
 *
 * This function can be called directly, but is primarily used indirectly.
 * Firstly, if you want to invoke this function yourself, it's easier to use
 * @ref ZL_E_ADDFRAME_PUBLIC instead since it populates some of the arguments
 * for you. Secondly, this is an implementation detail mostly here to be used
 * by @ref ZL_RET_T_IF_ERR and friends, which call this to add more context to
 * the error as it passes by.
 *
 * @note OpenZL must have been compiled with ZL_ERROR_ENABLE_STACKS defined to
 *       true for this to do anything. (This is the default.)
 *
 * @returns the modified error.
 */
ZL_Error ZL_E_addFrame_public(
        ZL_ErrorContext const* ctx,
        ZL_Error error,
        const char* file,
        const char* func,
        int line,
        const char* fmt,
        ...);

/**
 * Macro wrapper around @ref ZL_E_addFrame_public that automatically adds the
 * file, function, and line number macros to the args.
 */
#define ZL_E_ADDFRAME_PUBLIC(ctx, error, ...) \
    ZL_E_addFrame_public(ctx, error, __FILE__, __func__, __LINE__, __VA_ARGS__)

/**
 * ZL_Error_Array: a const view into an array of errors, returned by some
 * public APIs.
 */
typedef struct {
    const ZL_Error* errors;
    size_t size;
} ZL_Error_Array;

/////////////////
// Destruction //
/////////////////

// There is no destructor for ZS2_Errors! The memory is managed elsewhere and
// therefore there's nothing to do to destroy an error.

/**************
 * ZS2_Result *
 **************/

////////////////////////////
// Implementation Details //
////////////////////////////

// The ZS2_Result should be treated as an opaque type. Its members should never
// be accessed directly. You should only interact with it via the accessors and
// methods provided.
#define ZL_RESULT_DECLARE_TYPE_IMPL(_result_type, _value_type)           \
    typedef struct {                                                     \
        ZL_ErrorCode _code;                                              \
        _value_type _value;                                              \
    } ZS_MACRO_CONCAT(_result_type, _inner);                             \
                                                                         \
    union ZL_NODISCARD ZS_MACRO_CONCAT(_result_type, _u) {               \
        ZL_RESULT_DECLARE_INNER_TYPES(_value_type)                       \
        /* The first members of both structs are also a ZL_ErrorCode. */ \
        /* We alias the _code here with those members, so that we can */ \
        /* access it no matter which member is active. The C11 spec */   \
        /* explicitly allows this, in section 6.5.2.3.6. */              \
        ZL_ErrorCode _code;                                              \
        ZS_MACRO_CONCAT(_result_type, _inner) _value;                    \
        ZL_Error _error;                                                 \
    };                                                                   \
    typedef union ZS_MACRO_CONCAT(_result_type, _u) _result_type;        \
                                                                         \
    ZL_INLINE _value_type ZS_MACRO_CONCAT(_result_type, _extract)(       \
            const _result_type result, ZL_Error* const error)            \
    {                                                                    \
        /* Avoids using the inactive members of the union. */            \
        if (ZL_RES_isError(result)) {                                    \
            *error = result._error;                                      \
            _value_type dummy_value;                                     \
            memset(&dummy_value, 0, sizeof(dummy_value));                \
            return dummy_value;                                          \
        } else {                                                         \
            *error = ZL_E_EMPTY;                                         \
            return result._value._value;                                 \
        }                                                                \
    }                                                                    \
                                                                         \
    typedef int ZS_MACRO_CONCAT(_result_type, _fake_type_needs_semicolon)

#if defined(__cplusplus)
#    define ZL_RESULT_DECLARE_INNER_TYPES(_value_type) \
        using ValueType = _value_type;
#else
#    define ZL_RESULT_DECLARE_INNER_TYPES(_value_type) /* nothing */
#endif

#define ZL_RESULT_MAKE_ERROR(type, err, ...) \
    ZL_RESULT_MAKE_ERROR_CODE(type, ZL_EXPAND_ERRCODE(err), __VA_ARGS__)

#define ZL_RESULT_MAKE_ERROR_CODE(type, ...) \
    ZS_MACRO_PAD4(ZL_RESULT_MAKE_ERROR_CODE_INNER, "", "", type, __VA_ARGS__)

#if defined(__cplusplus)
} // extern "C"

// Helper functions for C++ to avoid compound literals
// These must be outside extern "C" block
namespace openzl::detail {
template <typename T>
inline T make_result_with_error(ZL_Error err)
{
    T result;
    result._error = err;
    return result;
}

template <typename T, typename V>
inline T make_result_with_value(V value)
{
    T result;
    result._value._code  = ZL_ErrorCode_no_error;
    result._value._value = value;
    return result;
}
} // namespace openzl::detail

// C++ compatible versions using helper functions
#    define ZL_RESULT_MAKE_ERROR_CODE_INNER(               \
            __PLACEHOLDER1__, __PLACEHOLDER2__, type, ...) \
        (::openzl::detail::make_result_with_error<         \
                ZL_RESULT_OF(type)>(ZL_E_create(           \
                NULL, NULL, __FILE__, __func__, __LINE__, __VA_ARGS__)))

#    define ZL_RESULT_WRAP_ERROR(type, err) \
        (::openzl::detail::make_result_with_error<ZL_RESULT_OF(type)>(err))

#    define ZL_RESULT_WRAP_VALUE(type, value) \
        (::openzl::detail::make_result_with_value<ZL_RESULT_OF(type)>(value))

extern "C" {
#else
// C99 compound literals
#    define ZL_RESULT_MAKE_ERROR_CODE_INNER(               \
            __PLACEHOLDER1__, __PLACEHOLDER2__, type, ...) \
        ((ZL_RESULT_OF(type)){ ._error = ZL_E_create(      \
                                       NULL,               \
                                       NULL,               \
                                       __FILE__,           \
                                       __func__,           \
                                       __LINE__,           \
                                       __VA_ARGS__) })

#    define ZL_RESULT_WRAP_ERROR(type, err) \
        ((ZL_RESULT_OF(type)){ ._error = (err) })

#    define ZL_RESULT_WRAP_VALUE(type, value) \
        ((ZL_RESULT_OF(type)){                  \
                ._value = {                     \
                  ._code = ZL_ErrorCode_no_error, \
                  ._value = (value),            \
              },                                \
      })
#endif

#define ZL_RET_IF_NOT_UNARY_IMPL(type, errcode, expr, ...) \
    ZL_RET_IF_UNARY_IMPL(type, errcode, !(expr), __VA_ARGS__)

#define ZL_RET_IF_NN_IMPL(type, errcode, expr, ...) \
    ZL_RET_IF_BINARY_IMPL(                          \
            type,                                   \
            errcode,                                \
            !=,                                     \
            (const char*)(const void*)expr,         \
            (const char*)0,                         \
            __VA_ARGS__)

#define ZL_RET_IF_NULL_IMPL(type, errcode, expr, ...) \
    ZL_RET_IF_BINARY_IMPL(                            \
            type,                                     \
            errcode,                                  \
            ==,                                       \
            (const char*)(const void*)expr,           \
            (const char*)0,                           \
            __VA_ARGS__)

#define ZL_RET_IMPL(type, res) \
    do {                       \
        return (res);          \
    } while (0)

#define ZL_RET_ERR_IMPL(type, errcode, ...)                                \
    do {                                                                   \
        ZL_E_DECLARE_STATIC_ERROR_INFO(                                    \
                __zl_static_error_info,                                    \
                ZL_EXPAND_ERRCODE(errcode),                                \
                "Unconditional failure: " ZL_EXPAND_ERRCODE_DESC_STR(      \
                        errcode) ": " ZS_MACRO_1ST_ARG(__VA_ARGS__));      \
        ZL_RET_ERR_WITH_STATIC_AND_CONTEXT(                                \
                type,                                                      \
                ZL_E_POINTER_TO_STATIC_ERROR_INFO(__zl_static_error_info), \
                NULL,                                                      \
                errcode,                                                   \
                __VA_ARGS__);                                              \
    } while (0)

#define ZL_RET_ERR_WITH_STATIC_AND_CONTEXT(                \
        type, static_error, scope_context, errcode, ...)   \
    do {                                                   \
        ZL_E_DECLARE_WITH_STATIC_AND_CONTEXT(              \
                __zl_tmp_error,                            \
                static_error,                              \
                scope_context,                             \
                errcode,                                   \
                "Unconditional failure: " __VA_ARGS__);    \
        ZL_E_appendToMessage(__zl_tmp_error, "\n\t");      \
        return ZL_RESULT_WRAP_ERROR(type, __zl_tmp_error); \
    } while (0)

#define ZL_RET_IF_ERR_IMPL(type, expr, ...)                       \
    do {                                                          \
        ZL_RESULT_OF(type)                                        \
        _result = ZL_RESULT_WRAP_ERROR(type, ZL_RES_error(expr)); \
        if (ZL_UNLIKELY(ZL_RES_isError(_result))) {               \
            ZL_RES_error(_result) = ZL_E_ADDFRAME_PUBLIC(         \
                    NULL, ZL_RES_error(_result), __VA_ARGS__);    \
            return _result;                                       \
        }                                                         \
    } while (0)

// undef'ed and replaced in the internal errors header
#define ZL_RET_IF_UNARY_IMPL(type, errcode, expr, ...)                     \
    do {                                                                   \
        ZL_E_DECLARE_STATIC_ERROR_INFO(                                    \
                __zl_static_error_info,                                    \
                ZL_EXPAND_ERRCODE(errcode),                                \
                "Check `" #expr "' failed: " ZL_EXPAND_ERRCODE_DESC_STR(   \
                        errcode) ": " ZS_MACRO_1ST_ARG(__VA_ARGS__));      \
        ZL_RET_IF_UNARY_IMPL_WITH_STATIC_AND_CONTEXT(                      \
                type,                                                      \
                ZL_E_POINTER_TO_STATIC_ERROR_INFO(__zl_static_error_info), \
                NULL,                                                      \
                errcode,                                                   \
                expr,                                                      \
                __VA_ARGS__);                                              \
    } while (0)

// undef'ed and replaced in the internal errors header
#define ZL_RET_IF_BINARY_IMPL(type, errcode, op, lhs, rhs, ...)            \
    do {                                                                   \
        ZL_E_DECLARE_STATIC_ERROR_INFO(                                    \
                __zl_static_error_info,                                    \
                ZL_EXPAND_ERRCODE(errcode),                                \
                "Check `" #lhs " " #op " " #rhs                            \
                "' failed: " ZL_EXPAND_ERRCODE_DESC_STR(                   \
                        errcode) ": " ZS_MACRO_1ST_ARG(__VA_ARGS__));      \
        ZL_RET_IF_BINARY_IMPL_WITH_STATIC_AND_CONTEXT(                     \
                type,                                                      \
                ZL_E_POINTER_TO_STATIC_ERROR_INFO(__zl_static_error_info), \
                NULL,                                                      \
                errcode,                                                   \
                op,                                                        \
                lhs,                                                       \
                rhs,                                                       \
                __VA_ARGS__);                                              \
    } while (0)

#define ZL_RET_IF_UNARY_IMPL_WITH_STATIC_AND_CONTEXT(                 \
        type, static_error, scope_context, errcode, expr, ...)        \
    do {                                                              \
        if (ZL_UNLIKELY(expr)) {                                      \
            ZL_E_DECLARE_WITH_STATIC_AND_CONTEXT(                     \
                    __zl_tmp_error,                                   \
                    static_error,                                     \
                    scope_context,                                    \
                    errcode,                                          \
                    "Check `%s' failed",                              \
                    #expr);                                           \
            ZL_E_appendToMessage(__zl_tmp_error, "\n\t" __VA_ARGS__); \
            return ZL_RESULT_WRAP_ERROR(type, __zl_tmp_error);        \
        }                                                             \
    } while (0)

// Controls whether to use the verbose non-standards-compliant binary assertion
// implementation (if enabled) or the fallback standards-compliant version (if
// disabled).
#ifndef ZL_ENABLE_RET_IF_ARG_PRINTING
#    if defined(__GNUC__) && !defined(__cplusplus)
#        define ZL_ENABLE_RET_IF_ARG_PRINTING 1
#    elif defined(__cplusplus) && (defined(__GNUC__) || defined(__clang__))
#        define ZL_ENABLE_RET_IF_ARG_PRINTING 1
#        define ZL_TYPEOF(x) decltype(x)
#    else
#        define ZL_ENABLE_RET_IF_ARG_PRINTING 0
#    endif
#endif

#ifndef ZL_TYPEOF
#    define ZL_TYPEOF(x) __typeof__(x)
#endif

#if ZL_ENABLE_RET_IF_ARG_PRINTING
/* We can only evaluate the lhs and rhs arguments once (they might have
 * side-effects). But we use their values twice. So we need to store the
 * results of their evaluation in temporary variables. But it's very difficult
 * to know what type those temporaries should have.
 *
 * So we do this weird typeof(...) dance to get temporaries that preserve or
 * exceed the signedness / precision / pointerness of the arguments. When the
 * the arguments are arithmetic types, the `L + (L - R)` construct resolves to
 * a suitably promoted type. When the arguments are pointers it just resoves to
 * the pointer type (`T* + (T* - T*)` becomes `T* + ptrdiff_t`, which is `T*`).
 *
 * Annoyingly, we can't use lhs and rhs directly in the typeof expression,
 * because clang has a warning (-Wnull-pointer-subtraction) that fires if one
 * of the arguments is a null pointer. So we use surrogate variables.
 */
#    define ZL_RET_IF_BINARY_IMPL_WITH_STATIC_AND_CONTEXT(                    \
            type, static_error, scope_context, errcode, op, lhs, rhs, ...)    \
        do {                                                                  \
            const ZL_TYPEOF(lhs) __dbg_lhs_type = (ZL_TYPEOF(lhs))0;          \
            const ZL_TYPEOF(rhs) __dbg_rhs_type = (ZL_TYPEOF(rhs))0;          \
            const ZL_TYPEOF(                                                  \
                    (__dbg_lhs_type) + ((__dbg_lhs_type) - (__dbg_rhs_type))) \
                    __dbg_lhs = (ZL_TYPEOF(                                   \
                            (__dbg_lhs_type)                                  \
                            + ((__dbg_lhs_type) - (__dbg_rhs_type))))(lhs);   \
            const ZL_TYPEOF(                                                  \
                    (__dbg_lhs_type) + ((__dbg_lhs_type) - (__dbg_rhs_type))) \
                    __dbg_rhs = (ZL_TYPEOF(                                   \
                            (__dbg_lhs_type)                                  \
                            + ((__dbg_lhs_type) - (__dbg_rhs_type))))(rhs);   \
            if (0 && ((lhs)op(rhs))) /* enforce type comparability */ {       \
            }                                                                 \
            if (ZL_UNLIKELY(__dbg_lhs op __dbg_rhs)) {                        \
                ZL_E_DECLARE_WITH_STATIC_AND_CONTEXT(                         \
                        __zl_tmp_error,                                       \
                        static_error,                                         \
                        scope_context,                                        \
                        errcode,                                              \
                        ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG(                 \
                                __dbg_lhs,                                    \
                                "Check `%s %s %s' failed where:\n\tlhs = ",   \
                                "\n\trhs = ",                                 \
                                ""),                                          \
                        #lhs,                                                 \
                        #op,                                                  \
                        #rhs,                                                 \
                        ZS_GENERIC_PRINTF_CAST(__dbg_lhs),                    \
                        ZS_GENERIC_PRINTF_CAST(__dbg_rhs));                   \
                ZL_E_appendToMessage(__zl_tmp_error, "\n\t" __VA_ARGS__);     \
                return ZL_RESULT_WRAP_ERROR(type, __zl_tmp_error);            \
            }                                                                 \
        } while (0)
#else
/* Otherwise, if we don't have typeof(), it's impossible to store the results
 * safely. So we just evaluate and use them directly in the comparison, and
 * give up the ability to print their values.
 */
#    define ZL_RET_IF_BINARY_IMPL_WITH_STATIC_AND_CONTEXT(                 \
            type, static_error, scope_context, errcode, op, lhs, rhs, ...) \
        do {                                                               \
            if (ZL_UNLIKELY((lhs)op(rhs))) {                               \
                ZL_E_DECLARE_WITH_STATIC_AND_CONTEXT(                      \
                        __zl_tmp_error,                                    \
                        static_error,                                      \
                        scope_context,                                     \
                        errcode,                                           \
                        "Check `%s %s %s' failed",                         \
                        #lhs,                                              \
                        #op,                                               \
                        #rhs);                                             \
                ZL_E_appendToMessage(__zl_tmp_error, "\n\t" __VA_ARGS__);  \
                return ZL_RESULT_WRAP_ERROR(type, __zl_tmp_error);         \
            }                                                              \
        } while (0)
#endif

//////////////////////////////////////////////
// New Errors System Implementation Details //
//////////////////////////////////////////////

#define ZL_WRAP_ERROR_ADD_FRAME(error) \
    ZL_WRAP_ERROR_NO_FRAME(            \
            ZL_E_ADDFRAME_PUBLIC(&ZL__errorContext, error, "Wrapping error."))

#define ZL_ERR_RET_IMPL(errcode, ...)                                      \
    do {                                                                   \
        ZL_E_DECLARE_STATIC_ERROR_INFO(                                    \
                __zl_static_error_info,                                    \
                ZL_EXPAND_ERRCODE(errcode),                                \
                "Unconditional failure: " ZL_EXPAND_ERRCODE_DESC_STR(      \
                        errcode) ": " ZS_MACRO_1ST_ARG(__VA_ARGS__));      \
        ZL_E_DECLARE_WITH_STATIC_AND_CONTEXT(                              \
                __zl_tmp_error,                                            \
                ZL_E_POINTER_TO_STATIC_ERROR_INFO(__zl_static_error_info), \
                &ZL__errorContext,                                         \
                errcode,                                                   \
                "Unconditional failure: " __VA_ARGS__);                    \
        ZL_E_appendToMessage(__zl_tmp_error, "\n\t");                      \
        return ZL_WRAP_ERROR_NO_FRAME(__zl_tmp_error);                     \
    } while (0)

#define ZL_ERR_IF_ERR_IMPL(expr, ...)                                       \
    do {                                                                    \
        ZL__RetType _result = ZL_WRAP_ERROR_NO_FRAME(ZL_RES_error(expr));   \
        if (ZL_UNLIKELY(ZL_RES_isError(_result))) {                         \
            ZL_RES_error(_result) = ZL_E_ADDFRAME_PUBLIC(                   \
                    &ZL__errorContext, ZL_RES_error(_result), __VA_ARGS__); \
            return _result;                                                 \
        }                                                                   \
    } while (0)

#define ZL_ERR_IF_UNARY_IMPL(expr, errcode, ...)                               \
    do {                                                                       \
        ZL_E_DECLARE_STATIC_ERROR_INFO(                                        \
                __zl_static_error_info,                                        \
                ZL_EXPAND_ERRCODE(errcode),                                    \
                "Check `" #expr "' failed: " ZL_EXPAND_ERRCODE_DESC_STR(       \
                        errcode) ": " ZS_MACRO_1ST_ARG(__VA_ARGS__));          \
        if (ZL_UNLIKELY(expr)) {                                               \
            ZL_E_DECLARE_WITH_STATIC_AND_CONTEXT(                              \
                    __zl_tmp_error,                                            \
                    ZL_E_POINTER_TO_STATIC_ERROR_INFO(__zl_static_error_info), \
                    &ZL__errorContext,                                         \
                    errcode,                                                   \
                    "Check `%s' failed",                                       \
                    #expr);                                                    \
            ZL_E_appendToMessage(__zl_tmp_error, "\n\t" __VA_ARGS__);          \
            return ZL_WRAP_ERROR_NO_FRAME(__zl_tmp_error);                     \
        }                                                                      \
    } while (0)

// Controls whether to use the verbose non-standards-compliant binary assertion
// implementation (if enabled) or the fallback standards-compliant version (if
// disabled).
#ifndef ZL_ENABLE_ERR_IF_ARG_PRINTING
#    if defined(__GNUC__)
#        define ZL_ENABLE_ERR_IF_ARG_PRINTING 1
#    else
#        define ZL_ENABLE_ERR_IF_ARG_PRINTING 0
#    endif
#endif

#if ZL_ENABLE_ERR_IF_ARG_PRINTING
/* We can only evaluate the lhs and rhs arguments once (they might have
 * side-effects). But we use their values twice. So we need to store the
 * results of their evaluation in temporary variables. But it's very difficult
 * to know what type those temporaries should have.
 *
 * So we do this weird typeof(...) dance to get temporaries that preserve or
 * exceed the signedness / precision / pointerness of the arguments. When the
 * the arguments are arithmetic types, the `L + (L - R)` construct resolves to
 * a suitably promoted type. When the arguments are pointers it just resoves to
 * the pointer type (`T* + (T* - T*)` becomes `T* + ptrdiff_t`, which is `T*`).
 *
 * Annoyingly, we can't use lhs and rhs directly in the typeof expression,
 * because clang has a warning (-Wnull-pointer-subtraction) that fires if one
 * of the arguments is a null pointer. So we use surrogate variables.
 */

#    define ZL_ERR_IF_BINARY_IMPL(lhs, op, rhs, errcode, ...)                                      \
        do {                                                                                       \
            ZL_E_DECLARE_STATIC_ERROR_INFO(                                                        \
                    __zl_static_error_info,                                                        \
                    ZL_EXPAND_ERRCODE(errcode),                                                    \
                    "Check `" #lhs " " #op " " #rhs                                                \
                    "' failed: " ZL_EXPAND_ERRCODE_DESC_STR(                                       \
                            errcode) ": " ZS_MACRO_1ST_ARG(__VA_ARGS__));                          \
            const __typeof__(lhs) __dbg_lhs_type = (__typeof__(lhs))0;                             \
            const __typeof__(rhs) __dbg_rhs_type = (__typeof__(rhs))0;                             \
            const __typeof__((__dbg_lhs_type) + ((__dbg_lhs_type) - (__dbg_rhs_type))) __dbg_lhs = \
                    (__typeof__((__dbg_lhs_type) + ((__dbg_lhs_type) - (__dbg_rhs_type))))(lhs);   \
            const __typeof__((__dbg_lhs_type) + ((__dbg_lhs_type) - (__dbg_rhs_type))) __dbg_rhs = \
                    (__typeof__((__dbg_lhs_type) + ((__dbg_lhs_type) - (__dbg_rhs_type))))(rhs);   \
            if (0 && ((lhs)op(rhs))) /* enforce type comparability */ {                            \
            }                                                                                      \
            if (ZL_UNLIKELY(__dbg_lhs op __dbg_rhs)) {                                             \
                ZL_E_DECLARE_WITH_STATIC_AND_CONTEXT(                                              \
                        __zl_tmp_error,                                                            \
                        ZL_E_POINTER_TO_STATIC_ERROR_INFO(                                         \
                                __zl_static_error_info),                                           \
                        &ZL__errorContext,                                                         \
                        errcode,                                                                   \
                        ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG(                                      \
                                __dbg_lhs,                                                         \
                                "Check `%s %s %s' failed where:\n\tlhs = ",                        \
                                "\n\trhs = ",                                                      \
                                ""),                                                               \
                        #lhs,                                                                      \
                        #op,                                                                       \
                        #rhs,                                                                      \
                        ZS_GENERIC_PRINTF_CAST(__dbg_lhs),                                         \
                        ZS_GENERIC_PRINTF_CAST(__dbg_rhs));                                        \
                ZL_E_appendToMessage(__zl_tmp_error, "\n\t" __VA_ARGS__);                          \
                return ZL_WRAP_ERROR_NO_FRAME(__zl_tmp_error);                                     \
            }                                                                                      \
        } while (0)
#else
/* Otherwise, if we don't have typeof(), it's impossible to store the results
 * safely. So we just evaluate and use them directly in the comparison, and
 * give up the ability to print their values.
 */
#    define ZL_ERR_IF_BINARY_IMPL(lhs, op, rhs, errcode, ...)             \
        do {                                                              \
            ZL_E_DECLARE_STATIC_ERROR_INFO(                               \
                    __zl_static_error_info,                               \
                    ZL_EXPAND_ERRCODE(errcode),                           \
                    "Check `" #lhs " " #op " " #rhs                       \
                    "' failed: " ZL_EXPAND_ERRCODE_DESC_STR(              \
                            errcode) ": " ZS_MACRO_1ST_ARG(__VA_ARGS__)); \
            if (ZL_UNLIKELY((lhs)op(rhs))) {                              \
                ZL_E_DECLARE_WITH_STATIC_AND_CONTEXT(                     \
                        __zl_tmp_error,                                   \
                        ZL_E_POINTER_TO_STATIC_ERROR_INFO(                \
                                __zl_static_error_info),                  \
                        &ZL__errorContext,                                \
                        errcode,                                          \
                        "Check `%s %s %s' failed",                        \
                        #lhs,                                             \
                        #op,                                              \
                        #rhs);                                            \
                ZL_E_appendToMessage(__zl_tmp_error, "\n\t" __VA_ARGS__); \
                return ZL_WRAP_ERROR_NO_FRAME(__zl_tmp_error);            \
            }                                                             \
        } while (0)
#endif

#define ZL_TRY_SET_DT(_expr_inner_type, _var, _expr)                  \
    do {                                                              \
        ZL_RESULT_OF(_expr_inner_type) __zl_try_set_result = (_expr); \
        ZL_ERR_IF_ERR(__zl_try_set_result);                           \
        _var = ZL_RES_value(__zl_try_set_result);                     \
    } while (0)

#define ZL_TRY_LET_DT(_expr_inner_type, _var, _expr) \
    _expr_inner_type _var;                           \
    ZL_TRY_SET_DT(_expr_inner_type, _var, _expr)

#define ZL_TRY_LET_CONST_DT(_expr_inner_type, _var, _expr)              \
    ZL_Error ZS_MACRO_CONCAT(_var, _tmp_error_storage);                 \
    const _expr_inner_type _var =                                       \
            ZS_MACRO_CONCAT(ZL_RESULT_OF(_expr_inner_type), _extract)(  \
                    _expr, &ZS_MACRO_CONCAT(_var, _tmp_error_storage)); \
    if (ZS_MACRO_CONCAT(_var, _tmp_error_storage)._code                 \
        != ZL_ErrorCode_no_error) {                                     \
        return ZL_WRAP_ERROR_ADD_FRAME(                                 \
                ZS_MACRO_CONCAT(_var, _tmp_error_storage));             \
    }                                                                   \
    do {                                                                \
    } while (0)

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_ERRORS_DETAIL_H
