// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMMON_INTROSPECTION_H
#define ZSTRONG_COMMON_INTROSPECTION_H

#include "openzl/zl_config.h"
#include "openzl/zl_errors.h"

#if defined(__cplusplus)
extern "C" {
#endif

#if ZL_ALLOW_INTROSPECTION

/**
 * Define an execution waypoint for compression introspection. When inserted
 * into an existing code block, this macro will grab the relevant
 * OperationContext from the @p ctx object and call the corresponding @p hook
 * function at the point of insertion.
 *
 * If @p hook is not provided (function pointer points to NULL), the operation
 * is aborted. This is to save on the expensive computation to fill __VA_ARGS__.
 * If introspection is disabled, the whole CWAYPOINT macro is no-oped.
 *
 * @note @p ctx must be a valid context object from which the OperationContext
 * can be extracted.
 */
#    define CWAYPOINT(hook, ctx, ...)                                 \
        do {                                                          \
            ZL_OperationContext* _oc = ZL_GET_OPERATION_CONTEXT(ctx); \
            ZL_ASSERT_NN(_oc);                                        \
            if (!_oc->hasCompressionHooks) {                          \
                break;                                                \
            }                                                         \
            if (_oc->compressIntrospectionHooks.hook != NULL) {       \
                /* allow passing a C++ class */                       \
                _oc->compressIntrospectionHooks.hook(                 \
                        _oc->compressIntrospectionHooks.opaque,       \
                        ctx,                                          \
                        __VA_ARGS__);                                 \
            }                                                         \
        } while (0)

#    define IF_CWAYPOINT_ENABLED(hook, ctx)                                 \
        ZL_OperationContext* _wpe_oc##hook = ZL_GET_OPERATION_CONTEXT(ctx); \
        ZL_ASSERT_NN(_wpe_oc##hook);                                        \
        if (_wpe_oc##hook->hasCompressionHooks                              \
            && _wpe_oc##hook->compressIntrospectionHooks.hook != NULL)

/**
 * Define an execution waypoint for decompression introspection. Same pattern
 * as CWAYPOINT but accesses the decompressIntrospectionHooks.
 *
 * Prefer using DWAYPOINT() when @p ctx supports ZL_GET_OPERATION_CONTEXT
 * directly. Use DWAYPOINT_WITH_OC() only when the operation context must be
 * resolved from a different object than the one passed to the hook callback
 * (e.g., when @p hook_ctx is const and ZL_GET_OPERATION_CONTEXT doesn't
 * accept it).
 *
 * @p oc_src is used to resolve the OperationContext (via
 * ZL_GET_OPERATION_CONTEXT), while @p hook_ctx is passed to the hook callback.
 */
#    define DWAYPOINT_WITH_OC(hook, oc_src, hook_ctx, ...)               \
        do {                                                             \
            ZL_OperationContext* _oc = ZL_GET_OPERATION_CONTEXT(oc_src); \
            ZL_ASSERT_NN(_oc);                                           \
            if (!_oc->hasDecompressionHooks) {                           \
                break;                                                   \
            }                                                            \
            if (_oc->decompressIntrospectionHooks.hook != NULL) {        \
                _oc->decompressIntrospectionHooks.hook(                  \
                        _oc->decompressIntrospectionHooks.opaque,        \
                        hook_ctx,                                        \
                        __VA_ARGS__);                                    \
            }                                                            \
        } while (0)

#    define DWAYPOINT(hook, ctx, ...) \
        DWAYPOINT_WITH_OC(hook, ctx, ctx, __VA_ARGS__)

#    define IF_DWAYPOINT_ENABLED(hook, ctx)                                 \
        ZL_OperationContext* _wpe_oc##hook = ZL_GET_OPERATION_CONTEXT(ctx); \
        ZL_ASSERT_NN(_wpe_oc##hook);                                        \
        if (_wpe_oc##hook->hasDecompressionHooks                            \
            && _wpe_oc##hook->decompressIntrospectionHooks.hook != NULL)

#else

#    define CWAYPOINT(hook, ctx, ...)
#    define IF_CWAYPOINT_ENABLED(hook, ctx) if (false)
#    define DWAYPOINT_WITH_OC(hook, oc_src, hook_ctx, ...)
#    define DWAYPOINT(hook, ctx, ...)
#    define IF_DWAYPOINT_ENABLED(hook, ctx) if (false)

#endif

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_COMMON_INTROSPECTION_H
