// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * \file
 *
 * This file contains internal error helpers
 * which are used to indicate success or failure in zstrong.
 *
 */

#ifndef ZSTRONG_COMMON_ERRORS_INTERNAL_H
#define ZSTRONG_COMMON_ERRORS_INTERNAL_H

#include <stdarg.h> // va_list
#include <stddef.h> // size_t

#include "openzl/common/assertion.h"
#include "openzl/common/logging.h"
#include "openzl/common/operation_context.h"
#include "openzl/common/scope_context.h"
#include "openzl/shared/portability.h" // ZL_BEGIN_C_DECLS, ZL_END_C_DECLS
#include "openzl/zl_errors.h"          // ZL_Report

ZL_BEGIN_C_DECLS

#ifndef ZL_ERROR_ENABLE_LEAKY_ALLOCATIONS
#    define ZL_ERROR_ENABLE_LEAKY_ALLOCATIONS 0
#endif

#ifndef ZL_ERROR_ENABLE_STACKS
#    define ZL_ERROR_ENABLE_STACKS 1
#endif

/*************************************
 * ZL_ErrorCode Description Strings *
 *************************************/

/**
 * Having these all be macroes lets us concat this string into static error
 * info strings.
 */

/**
 * Maps the short suffix name for an error (e.g. `corruption`) to the actual
 * enum name (e.g., `ZL_ErrorCode_corruption`).
 */
#define ZL_EXPAND_ERRCODE(shortname) ZS_MACRO_CONCAT(ZL_ErrorCode_, shortname)

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

/***********************
 * ZL_EI: ZL_ErrorInfo *
 ***********************/

ZL_INLINE ZL_ErrorInfo ZL_EI_fromDy(ZL_DynamicErrorInfo* dy)
{
    ZL_ASSERT(!((uintptr_t)dy & 1));
    if ((uintptr_t)dy & 1) {
        return ZL_EE_EMPTY;
    }
    if (dy == NULL) {
        return ZL_EE_EMPTY;
    }
    dy = (ZL_DynamicErrorInfo*)(((uintptr_t)dy) | 1);
    return (ZL_ErrorInfo){ ._dy = dy };
}

ZL_INLINE ZL_ErrorInfo ZL_EI_fromSt(const ZL_StaticErrorInfo* const st)
{
    ZL_ASSERT(!((uintptr_t)st & 1));
    return (ZL_ErrorInfo){ ._st = st };
}

/******************
 * ZL_E: ZL_Error *
 ******************/

//////////////////
// Construction //
//////////////////

/**
 * Using this macro API allows users to specify a formatted message when they
 * create and return an error. Currently this message is discarded. But! We
 * expect to soon have the capability to capture this message information into
 * the error object, so this lets us start including that information that will
 * later be useful now as we migrate things to returning errors rather than
 * ASSERT-ing or REQUIRE-ing.
 *
 * ZL_E() takes an error code suffix, e.g., `allocation`, while ZL_E_CODE()
 * takes the full name (or a variable or something!).
 */
#define ZL_E(...) ZS_MACRO_PAD1(ZL_E_INNER, __VA_ARGS__)
#define ZL_E_CODE(...) ZS_MACRO_PAD1(ZL_E_CODE_INNER, __VA_ARGS__)

#define ZL_E_INNER(err, ...) \
    ZL_E_CODE_INNER(ZL_EXPAND_ERRCODE(err), __VA_ARGS__)
#define ZL_E_CODE_INNER(code, ...)  \
    ZL_E_create(                    \
            NULL,                   \
            ZL_GET_SCOPE_CONTEXT(), \
            __FILE__,               \
            __func__,               \
            __LINE__,               \
            code,                   \
            __VA_ARGS__)

#if ZL_ERROR_ENABLE_STATIC_ERROR_INFO
#    define ZL_EE_FROM_STATIC(name) \
        ZL_EI_fromSt(ZL_E_POINTER_TO_STATIC_ERROR_INFO(name))
#else
#    define ZL_EE_FROM_STATIC(name) ZL_EE_EMPTY
#endif

/////////////////
// Destruction //
/////////////////

// There is no destructor for ZS2_Errors! The memory is managed elsewhere and
// therefore there's nothing to do to destroy an error.

///////////////
// Accessors //
///////////////

ZL_INLINE int ZL_E_isError(ZL_Error err)
{
    return err._code != ZL_ErrorCode_no_error;
}

ZL_INLINE ZL_ErrorCode ZL_E_code(ZL_Error err)
{
    return err._code;
}

ZL_INLINE const char* ZL_E_codeStr(ZL_Error err)
{
    return ZL_ErrorCode_toString(err._code);
}

/// Logs ZL_E_str(err._info) if level <= ZL_g_logLevel.
void ZL_E_log(ZL_Error err, int level);

/// Prints ZL_E_str(err._info) to stderr.
void ZL_E_print(ZL_Error err);

/// @returns A pretty-printed string containing all the information held in
/// @p info, or NULL if there is no error, or if @p info is NULL.
/// NOTE: This string is owned by err._info, and is tied to its lifetime.
char const* ZL_E_str(ZL_Error err);

////////////////
// Assertions //
////////////////

// These are assertion macroes (like those defined in assertion.h), but
// specialized for checking for errors.

#define ZL_ASSERT_SUCCESS(...) \
    ZS_MACRO_PAD1(ZL_ASSERT_SUCCESS_IMPL, __VA_ARGS__)

#define ZL_REQUIRE_SUCCESS(...) \
    ZS_MACRO_PAD1(ZL_REQUIRE_SUCCESS_IMPL, __VA_ARGS__)

#if ZL_ENABLE_ASSERT
#    define ZL_ASSERT_SUCCESS_IMPL(expr, ...) \
        ZL_ASSERTION_SUCCESS_IMPL(expr, "assertion", __VA_ARGS__)
#else
#    define ZL_ASSERT_SUCCESS_IMPL(expr, ...)            \
        ZL_LOG_IF(                                       \
                0 && (ZL_E_isError(ZL_RES_error(expr))), \
                ALWAYS,                                  \
                "Error: " __VA_ARGS__)
#endif // ZL_ENABLE_ASSERT

#if ZL_ENABLE_REQUIRE
#    define ZL_REQUIRE_SUCCESS_IMPL(expr, ...) \
        ZL_ASSERTION_SUCCESS_IMPL(expr, "requirement", __VA_ARGS__)
#else
#    define ZL_REQUIRE_SUCCESS_IMPL(expr, ...)           \
        ZL_LOG_IF(                                       \
                0 && (ZL_E_isError(ZL_RES_error(expr))), \
                ALWAYS,                                  \
                "Error: " __VA_ARGS__)
#endif // ZL_ENABLE_REQUIRE

// Implementation for ..._SUCCESS()
#define ZL_ASSERTION_SUCCESS_IMPL(expr, req_str, ...)                 \
    do {                                                              \
        ZL_Error _error = ZL_RES_error(expr);                         \
        if (ZL_UNLIKELY(ZL_E_isError(_error))) {                      \
            ZL_LOG(ALWAYS,                                            \
                   "Expression `%s' returned an error violating %s:", \
                   #expr,                                             \
                   req_str);                                          \
            ZL_LOG_IFNONEMPTY(ALWAYS, "Context: ", __VA_ARGS__);      \
            ZL_E_ADDFRAME(&_error, ZL_EE_EMPTY, "");                  \
            ZL_E_log(_error, ZL_LOG_LVL_ALWAYS);                      \
            ZL_ABORT();                                               \
        }                                                             \
    } while (0)

///////////////
// Mutations //
///////////////

/// Up-convert a (possibly-)static error into a dynamic error.
ZL_Error ZL_E_convertToDynamic(ZL_OperationContext* opCtx, ZL_Error err);

// Get the dynamic error info in the error. Returns nullptr if the error doesn't
// contain an error or doesn't point at a dynamic error.
ZL_DynamicErrorInfo* ZL_E_dy(ZL_Error err);

// Get the static error info in the error. Returns nullptr if the error doesn't
// contain an error or doesn't point at a static error.
const ZL_StaticErrorInfo* ZL_E_st(ZL_Error err);

// Append the current frame to the stack in the rich error.
#define ZL_E_ADDFRAME(e, backup, ...) \
    ZL_E_addFrame(                    \
            ZL_GET_SCOPE_CONTEXT(),   \
            e,                        \
            backup,                   \
            __FILE__,                 \
            __func__,                 \
            __LINE__,                 \
            __VA_ARGS__)

#if ZL_ERROR_ENABLE_STACKS
void ZL_E_addFrame(
        ZL_ErrorContext const* scopeCtx,
        ZL_Error* e,
        ZL_ErrorInfo backup,
        const char* file,
        const char* func,
        int line,
        char const* fmt,
        ...);
#else
ZL_INLINE void ZL_E_addFrame(
        ZL_ErrorContext const* scopeCtx,
        ZL_Error* e,
        ZL_ErrorInfo backup,
        const char* file,
        const char* func,
        int line,
        char const* fmt,
        ...)
{
    (void)scopeCtx;
    (void)file;
    (void)func;
    (void)line;
    (void)fmt;
    return e;
}
#endif // ZS_ENABLE_ERROR_STACKS

void ZL_E_changeErrorCode(ZL_Error* err, ZL_ErrorCode code);

/**
 * This macro provides an alternative mechanism for surfacing errors to the
 * user. This is intended for errors from which you can recover and continue
 * without globally failing the user's operation. In those cases, you don't
 * want to report the failure encountered by returning errors up the stack,
 * which requires also failing each enclosing operation. Instead, you can pass
 * the error into this macro and convert it to a warning and return a normal
 * success. All warnings encountered can be separately queried by the user
 * after the operation finishes.
 *
 * Unlike most error handling macroes, which implicitly try to capture the
 * operation context, you must explicitly provide a context into which this
 * warning can be captured. (At the moment, it must be the same context that
 * manages the error, if it is managed.) The error can then be discarded.
 */
#define ZL_E_convertToWarning(ctx, err)                                      \
    do {                                                                     \
        ZL_Error _error = (err);                                             \
        if (ZL_E_isError(_error)) {                                          \
            ZL_OperationContext* _op_ctx = ZL_GET_OPERATION_CONTEXT(ctx);    \
            ZL_ASSERT_NN(_op_ctx);                                           \
            ZL_ErrorContext _scope_ctx = { .opCtx = _op_ctx };               \
            /* adding a frame will also trigger static -> dynamic error info \
             * conversion */                                                 \
            ZL_E_addFrame(                                                   \
                    &_scope_ctx,                                             \
                    &_error,                                                 \
                    ZL_EE_EMPTY,                                             \
                    __FILE__,                                                \
                    __func__,                                                \
                    __LINE__,                                                \
                    "Converted to warning.");                                \
            ZL_DynamicErrorInfo* _dy = ZL_E_dy(_error);                      \
            ZL_ASSERT_NN(_dy);                                               \
            ZL_OC_markAsWarning(_op_ctx, _error);                            \
        }                                                                    \
    } while (0)

// Clear out the error info pointer to help avoid errors when the context object
// has been freed.
void ZL_E_clearInfo(ZL_Error* err);

/*********************
 * The ZL_ErrorFrame *
 *********************/

/**
 * The ZL_ErrorFrame records a stack frame in which the error was either
 * generated or propagated.
 */
typedef struct {
    const char* file;
    const char* func;
    int line;
    const char* message;
} ZL_ErrorFrame;

/*****************************
 * ZS2_EE: The ZL_ErrorInfo *
 *****************************/

/// Create a new ZL_DynamicErrorInfo.
ZL_ErrorInfo ZL_EE_create(void);
ZL_DynamicErrorInfo* ZL_DEE_create(void);

/// Free a ZL_ErrorInfo pointer.
void ZL_EE_free(ZL_ErrorInfo ei);
void ZL_DEE_free(ZL_DynamicErrorInfo* info);

/// Clear the ZL_ErrorInfo object for reuse.
void ZL_EE_clear(ZL_ErrorInfo ei);
void ZL_DEE_clear(ZL_DynamicErrorInfo* info);

///////////////
// Accessors //
///////////////

/// @returns the code stored in the error info.
/// Returns ZL_ErrorCode_no_error if no error is stored.
ZL_ErrorCode ZL_EE_code(ZL_ErrorInfo ei);
ZL_ErrorCode ZL_DEE_code(ZL_DynamicErrorInfo const* info);

/// @returns the message stored in the error info or NULL if no
/// message is stored, or there is no error.
char const* ZL_EE_message(ZL_ErrorInfo ei);

/// @returns the number of stack frames stored in the error info.
size_t ZL_EE_nbStackFrames(ZL_ErrorInfo ei);

/// @returns The stack frame stored at index @p idx
/// @pre @p idx must be less than ZL_EE_nbStackFrames(info).
ZL_ErrorFrame ZL_EE_stackFrame(ZL_ErrorInfo ei, size_t idx);

/// @returns The graph context stored in the error info.
ZL_GraphContext ZL_EE_graphContext(ZL_ErrorInfo ei);

/// @returns A pretty-printed string containing all the information held in
/// @p info, or NULL if there is no error, or if @p info is NULL.
/// NOTE: This string is owned by @p info, and is tied to its lifetime.
char const* ZL_EE_str(ZL_ErrorInfo ei);

/// Logs ZL_EE_str(info) if level <= ZL_g_logLevel.
void ZL_EE_log(ZL_ErrorInfo ei, int level);

/*************
 * ZL_Result *
 *************/

//////////////////
// Construction //
//////////////////

#undef ZL_RESULT_MAKE_ERROR
#define ZL_RESULT_MAKE_ERROR(type, ...) \
    ZL_RESULT_WRAP_ERROR(type, ZL_E(__VA_ARGS__))

#undef ZL_RESULT_MAKE_ERROR_CODE
#define ZL_RESULT_MAKE_ERROR_CODE(type, ...) \
    ZL_RESULT_WRAP_ERROR(type, ZL_E_CODE(__VA_ARGS__))

///////////////
// Modifiers //
///////////////

// Clears the errors info pointer if it is an error
#define ZL_RES_clearInfo(res) \
    ZL_E_clearInfo(ZL_RES_isError(res) ? &ZL_RES_error(res) : NULL)

// If the provided result has an error...
#define ZL_RES_convertToWarning(ctx, res) \
    ZL_E_convertToWarning(ctx, ZL_RES_error(res))

/////////////////////
// Checking Macros //
/////////////////////

/**
 * Like ZL_RET_T_IF_ERR(), but checks that the error is one that is allowed
 * to be generated by the zstrong internals, and coerces it to a logicError if
 * not. See ZL_RET_IF_ERR_COERCE_IMPL() for details/motivation.
 */
#define ZL_RET_T_IF_ERR_COERCE(type, ...) \
    ZS_MACRO_PAD2(ZL_RET_IF_ERR_COERCE_IMPL, type, __VA_ARGS__)

/**
 * Like ZL_RET_R_IF_ERR(), but checks that the error is one that is allowed
 * to be generated by the zstrong internals, and coerces it to a logicError if
 * not. See ZL_RET_IF_ERR_COERCE_IMPL() for details/motivation.
 */
#define ZL_RET_R_IF_ERR_COERCE(...) ZL_RET_T_IF_ERR_COERCE(size_t, __VA_ARGS__)

////////////////////////////
// Implementation Details //
////////////////////////////

#undef ZL_RET_IMPL
#define ZL_RET_IMPL(type, res)                                        \
    do {                                                              \
        ZL_RESULT_OF(type) __tmp_res = (res);                         \
        if (ZL_RES_isError(__tmp_res)) {                              \
            ZL_E_ADDFRAME(&ZL_RES_error(__tmp_res), ZL_EE_EMPTY, ""); \
        }                                                             \
        return __tmp_res;                                             \
    } while (0)

// TODO: only difference with public API here is now that these find the scope
// ctx
#undef ZL_RET_ERR_IMPL
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
                ZL_GET_SCOPE_CONTEXT(),                                    \
                errcode,                                                   \
                __VA_ARGS__);                                              \
    } while (0)

#undef ZL_RET_IF_UNARY_IMPL
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
                ZL_GET_SCOPE_CONTEXT(),                                    \
                errcode,                                                   \
                expr,                                                      \
                __VA_ARGS__);                                              \
    } while (0)

#undef ZL_RET_IF_BINARY_IMPL
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
                ZL_GET_SCOPE_CONTEXT(),                                    \
                errcode,                                                   \
                op,                                                        \
                lhs,                                                       \
                rhs,                                                       \
                __VA_ARGS__);                                              \
    } while (0)

#undef ZL_RET_IF_ERR_IMPL
#define ZL_RET_IF_ERR_IMPL(type, expr, ...)                              \
    do {                                                                 \
        ZL_RESULT_OF(type)                                               \
        _result = ZL_RESULT_WRAP_ERROR(type, ZL_RES_error(expr));        \
        if (ZL_UNLIKELY(ZL_RES_isError(_result))) {                      \
            ZL_E_DECLARE_STATIC_ERROR_INFO(                              \
                    __zl_static_error_info,                              \
                    ZL_ErrorCode_GENERIC,                                \
                    "Forwarding error: " ZS_MACRO_1ST_ARG(__VA_ARGS__)); \
            ZL_E_ADDFRAME(                                               \
                    &ZL_RES_error(_result),                              \
                    ZL_EE_FROM_STATIC(__zl_static_error_info),           \
                    __VA_ARGS__);                                        \
            return _result;                                              \
        }                                                                \
    } while (0)

/**
 * Some errors have special meaning, and
 */
#define ZL_RET_IF_ERR_COERCE_IMPL(type, res, ...)                                                                                                                                                                     \
    do {                                                                                                                                                                                                              \
        ZL_RESULT_OF(type) _res = (res);                                                                                                                                                                              \
        if (ZL_RES_isError(_res)) {                                                                                                                                                                                   \
            ZL_ASSERT_NE(                                                                                                                                                                                             \
                    (int)ZL_E_code(ZL_RES_error(_res)),                                                                                                                                                               \
                    (int)ZL_ErrorCode_dstCapacity_tooSmall,                                                                                                                                                           \
                    "A call inside the zstrong internals produced dstCapacity_tooSmall, which should only be used at the end when interacting with the user-provided output buffer.");                                \
            if (ZL_E_code(ZL_RES_error(_res))                                                                                                                                                                         \
                == ZL_ErrorCode_dstCapacity_tooSmall) {                                                                                                                                                               \
                ZL_E_changeErrorCode(                                                                                                                                                                                 \
                        &ZL_RES_error(_res), ZL_ErrorCode_logicError);                                                                                                                                                \
                ZL_E_appendToMessage(                                                                                                                                                                                 \
                        ZL_RES_error(_res),                                                                                                                                                                           \
                        "A call inside the zstrong internals produced dstCapacity_tooSmall, which should only be used at the end when interacting with the user-provided output buffer. Coerced to logicError.\n\t"); \
            }                                                                                                                                                                                                         \
        }                                                                                                                                                                                                             \
        ZL_RET_IF_ERR_IMPL(type, _res, __VA_ARGS__);                                                                                                                                                                  \
    } while (0)

/**
 * Adds a ZL_GraphContext to the error scope
 */
#define ZL_RESULT_SCOPE_ADD_GRAPH_CONTEXT(...)   \
    do {                                         \
        ZL__errorContext.graphCtx = __VA_ARGS__; \
    } while (0)

ZL_END_C_DECLS

#endif // ZSTRONG_COMMON_ERRORS_INTERNAL_H
