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

#else

#    define CWAYPOINT(hook, ctx, ...)
#    define IF_CWAYPOINT_ENABLED(hook, ctx) if (false)

#endif

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_COMMON_INTROSPECTION_H
