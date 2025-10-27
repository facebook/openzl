// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_COMMON_SCOPE_CONTEXT_H
#define ZSTRONG_COMMON_SCOPE_CONTEXT_H

#include <string.h>

#include "openzl/common/operation_context.h"
#include "openzl/detail/zl_error_context.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_dtransform.h"

/**
 * @file This method of getting an ErrorContext is deprecated in favor of
 * openzl/detail/zl_error_context.h
 */

ZL_BEGIN_C_DECLS

/// @returns a pointer to the current scope context, or NULL if no scope context
/// is available.
///
/// A scope context is available if:
/// - A ZL_SCOPE_CONTEXT() was called in this scope giving an explicit context.
/// - A `ZL_CCtx* cctx` variable exists in the scope.
/// - A `ZL_DCtx* dctx` variable exists in the scope.
/// - A `ZL_Encoder* eictx` variable exists in the scope.
/// - A `ZL_Decoder* dictx` variable exists in the scope.
/// - A `ZL_Compressor* cgraph` variable exists in the scope.
///
/// If multiple scope contexts are available, then the first in the list is
/// used.
///
/// NOTE: Including this header makes it illegal to create a variable named
/// `cctx`, `dctx`, `eictx`, `dictx`, or `cgraph` that is not a pointer.
#define ZL_GET_SCOPE_CONTEXT() ZL_GET_SCOPE_CONTEXT_IMPL()

/// Sets the current scope scope context.
/// @param ctx must be the current context object, which is one of:
/// - ZL_CCtx
/// - ZL_Compressor
/// - ZL_Encoder
/// - ZL_DCtx
/// - ZL_Decoder
#define ZL_SCOPE_CONTEXT(ctx)                                             \
    ZL_ScopeContext ZL__scopeContext = { ZL_GET_OPERATION_CONTEXT(ctx) }; \
    (void)ZL__scopeContext

/// Sets the current scope context, and also set the graph context.
/// @param ctx @see ZL_SCOPE_CONTEXT.
/// @param ... An initializer for the ZL_GraphContext.
#define ZL_SCOPE_GRAPH_CONTEXT(ctx, ...)                                \
    ZL_ScopeContext ZL__scopeContext = { ZL_GET_OPERATION_CONTEXT(ctx), \
                                         __VA_ARGS__ };                 \
    (void)ZL__scopeContext

typedef ZL_ErrorContext ZL_ScopeContext;

// Implementation details

static inline void ZL__scopeContext(void) {}
static inline void cctx(void) {}
static inline void dctx(void) {}
static inline void cgraph(void) {}
static inline void eictx(void) {}
static inline void dictx(void) {}
static inline void gctx(void) {}
static inline void sctx(void) {}

static inline ZL_ScopeContext const* ZL_scopeContextFirstNotNull(
        ZL_ScopeContext const* ctx0,
        ZL_ScopeContext const* ctx1,
        ZL_ScopeContext const* ctx2,
        ZL_ScopeContext const* ctx3,
        ZL_ScopeContext const* ctx4,
        ZL_ScopeContext const* ctx5,
        ZL_ScopeContext const* ctx6,
        ZL_ScopeContext const* ctx7)
{
    if (ctx0 != NULL)
        return ctx0;
    if (ctx1 != NULL)
        return ctx1;
    if (ctx2 != NULL)
        return ctx2;
    if (ctx3 != NULL)
        return ctx3;
    if (ctx4 != NULL)
        return ctx4;
    if (ctx5 != NULL)
        return ctx5;
    if (ctx6 != NULL)
        return ctx6;
    if (ctx7 != NULL)
        return ctx7;
    return NULL;
}

#ifdef __cplusplus

ZL_END_C_DECLS

template <typename T>
ZL_ScopeContext const* ZL_getScopeContext(T ctx)
{
    (void)ctx;
    return NULL;
}
template <>
inline ZL_ScopeContext const* ZL_getScopeContext(ZL_ScopeContext* ctx)
{
    return ctx;
}
template <>
inline ZL_ScopeContext const* ZL_getScopeContext(ZL_CCtx* ctx)
{
    return ZL_OC_defaultScopeContext(ZL_CCtx_getOperationContext(ctx));
}
template <>
inline ZL_ScopeContext const* ZL_getScopeContext(ZL_DCtx* ctx)
{
    return ZL_OC_defaultScopeContext(ZL_DCtx_getOperationContext(ctx));
}
template <>
inline ZL_ScopeContext const* ZL_getScopeContext(ZL_Compressor* ctx)
{
    return ZL_OC_defaultScopeContext(ZL_Compressor_getOperationContext(ctx));
}
template <>
inline ZL_ScopeContext const* ZL_getScopeContext(ZL_Encoder* ctx)
{
    return ZL_OC_defaultScopeContext(ZL_Encoder_getOperationContext(ctx));
}
template <>
inline ZL_ScopeContext const* ZL_getScopeContext(ZL_Decoder* ctx)
{
    return ZL_OC_defaultScopeContext(ZL_Decoder_getOperationContext(ctx));
}
template <>
inline ZL_ScopeContext const* ZL_getScopeContext(ZL_Graph* ctx)
{
    return ZL_OC_defaultScopeContext(ZL_Graph_getOperationContext(ctx));
}
template <>
inline ZL_ScopeContext const* ZL_getScopeContext(ZL_Edge* ctx)
{
    return ZL_OC_defaultScopeContext(ZL_Edge_getOperationContext(ctx));
}

ZL_BEGIN_C_DECLS

#    define ZL_GET_SCOPE_CONTEXT_IMPL()                \
        ZL_scopeContextFirstNotNull(                   \
                ZL_getScopeContext(&ZL__scopeContext), \
                ZL_getScopeContext(cctx),              \
                ZL_getScopeContext(dctx),              \
                ZL_getScopeContext(eictx),             \
                ZL_getScopeContext(dictx),             \
                ZL_getScopeContext(cgraph),            \
                ZL_getScopeContext(gctx),              \
                ZL_getScopeContext(sctx))

#else

ZL_INLINE ZL_ScopeContext const* ZL_launderScopeContext(void const* ctx)
{
    return (ZL_ScopeContext const*)ctx;
}

ZL_INLINE void* ZL_unconstify(void const* ptr)
{
    void* mut;
    memcpy(&mut, &ptr, sizeof(ptr));
    return mut;
}

#    define ZL_GET_SCOPE_CONTEXT_IMPL_SCOPE_CONTEXT() \
        (ZL_launderScopeContext(                      \
                _Generic(                             \
                        ZL__scopeContext,             \
                        default: NULL,                \
                        ZL_ScopeContext: &ZL__scopeContext)))
#    define ZL_GET_SCOPE_CONTEXT_IMPL_CCTX()                 \
        (ZL_launderScopeContext(                             \
                _Generic(                                    \
                        cctx,                                \
                        default: NULL,                       \
                        ZL_CCtx*: ZL_OC_defaultScopeContext( \
                                ZL_CCtx_getOperationContext( \
                                        ZL_unconstify((void const*)cctx))))))
#    define ZL_GET_SCOPE_CONTEXT_IMPL_DCTX()                 \
        (ZL_launderScopeContext(                             \
                _Generic(                                    \
                        dctx,                                \
                        default: NULL,                       \
                        ZL_DCtx*: ZL_OC_defaultScopeContext( \
                                ZL_DCtx_getOperationContext( \
                                        ZL_unconstify((void const*)dctx))))))
#    define ZL_GET_SCOPE_CONTEXT_IMPL_EICTX()                   \
        (ZL_launderScopeContext(                                \
                _Generic(                                       \
                        eictx,                                  \
                        default: NULL,                          \
                        ZL_Encoder*: ZL_OC_defaultScopeContext( \
                                ZL_Encoder_getOperationContext( \
                                        ZL_unconstify((void const*)eictx))))))
#    define ZL_GET_SCOPE_CONTEXT_IMPL_DICTX()                   \
        (ZL_launderScopeContext(                                \
                _Generic(                                       \
                        dictx,                                  \
                        default: NULL,                          \
                        ZL_Decoder*: ZL_OC_defaultScopeContext( \
                                ZL_Decoder_getOperationContext( \
                                        ZL_unconstify((void const*)dictx))))))
#    define ZL_GET_SCOPE_CONTEXT_IMPL_CGRAPH()                     \
        (ZL_launderScopeContext(                                   \
                _Generic(                                          \
                        cgraph,                                    \
                        default: NULL,                             \
                        ZL_Compressor*: ZL_OC_defaultScopeContext( \
                                ZL_Compressor_getOperationContext( \
                                        ZL_unconstify(             \
                                                (void const*)cgraph))))))
#    define ZL_GET_SCOPE_CONTEXT_IMPL_GCTX()                  \
        (ZL_launderScopeContext(                              \
                _Generic(                                     \
                        gctx,                                 \
                        default: NULL,                        \
                        ZL_Graph*: ZL_OC_defaultScopeContext( \
                                ZL_Graph_getOperationContext( \
                                        ZL_unconstify((void const*)gctx))))))
#    define ZL_GET_SCOPE_CONTEXT_IMPL_SCTX()                 \
        (ZL_launderScopeContext(                             \
                _Generic(                                    \
                        sctx,                                \
                        default: NULL,                       \
                        ZL_Edge*: ZL_OC_defaultScopeContext( \
                                ZL_Edge_getOperationContext( \
                                        ZL_unconstify((void const*)sctx))))))

#    define ZL_GET_SCOPE_CONTEXT_IMPL()                    \
        ZL_scopeContextFirstNotNull(                       \
                ZL_GET_SCOPE_CONTEXT_IMPL_SCOPE_CONTEXT(), \
                ZL_GET_SCOPE_CONTEXT_IMPL_CCTX(),          \
                ZL_GET_SCOPE_CONTEXT_IMPL_DCTX(),          \
                ZL_GET_SCOPE_CONTEXT_IMPL_EICTX(),         \
                ZL_GET_SCOPE_CONTEXT_IMPL_DICTX(),         \
                ZL_GET_SCOPE_CONTEXT_IMPL_CGRAPH(),        \
                ZL_GET_SCOPE_CONTEXT_IMPL_GCTX(),          \
                ZL_GET_SCOPE_CONTEXT_IMPL_SCTX())

#endif

ZL_END_C_DECLS

#endif
