// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_DETAIL_ZS2_ERRORS_CONTEXT_H
#define ZSTRONG_DETAIL_ZS2_ERRORS_CONTEXT_H

#ifdef __cplusplus
#    include <cstddef> // nullptr_t
#endif

#include "openzl/zl_opaque_types.h"
#include "openzl/zl_portability.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct ZL_OperationContext_s ZL_OperationContext;

typedef struct {
    /// The current nodeID or 0 for unset / unknown.
    ZL_NodeID nodeID;
    /// The current graphID or 0 for unset / unknown.
    ZL_GraphID graphID;
    /// The current transformID or 0 for unset / unknown.
    ZL_IDType transformID;
    /// The name of the component, may be NULL.
    char const* name;
} ZL_GraphContext;

typedef struct {
    /// Pointer to the operation context to store dynamic error info in, or NULL
    /// to opt out of dynamic error info.
    ZL_OperationContext* opCtx;
    ZL_GraphContext graphCtx;
} ZL_ErrorContext;

// Forward declare so we don't need to include these headers.
ZL_CONST_FN ZL_OperationContext* ZL_Compressor_getOperationContext(
        ZL_Compressor* ctx);
ZL_CONST_FN ZL_OperationContext* ZL_CCtx_getOperationContext(ZL_CCtx* ctx);
ZL_CONST_FN ZL_OperationContext* ZL_DCtx_getOperationContext(ZL_DCtx* ctx);
ZL_CONST_FN ZL_OperationContext* ZL_Encoder_getOperationContext(
        ZL_Encoder* ctx);
ZL_CONST_FN ZL_OperationContext* ZL_Decoder_getOperationContext(
        ZL_Decoder* ctx);
ZL_CONST_FN ZL_OperationContext* ZL_Graph_getOperationContext(ZL_Graph* ctx);
ZL_CONST_FN ZL_OperationContext* ZL_Edge_getOperationContext(ZL_Edge* ctx);
ZL_CONST_FN ZL_OperationContext* ZL_CompressorSerializer_getOperationContext(
        ZL_CompressorSerializer* ctx);
ZL_CONST_FN ZL_OperationContext* ZL_CompressorDeserializer_getOperationContext(
        ZL_CompressorDeserializer* ctx);
ZL_CONST_FN ZL_OperationContext* ZL_Segmenter_getOperationContext(
        ZL_Segmenter* ctx);
ZL_CONST_FN ZL_OperationContext* ZL_ErrorContext_getOperationContext(
        ZL_ErrorContext* ctx);
ZL_CONST_FN ZL_OperationContext* ZL_NULL_getOperationContext(void* ctx);

#ifdef __cplusplus
}

ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_Compressor* ctx)
{
    return ZL_Compressor_getOperationContext(ctx);
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_CCtx* ctx)
{
    return ZL_CCtx_getOperationContext(ctx);
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_DCtx* ctx)
{
    return ZL_DCtx_getOperationContext(ctx);
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_Encoder* ctx)
{
    return ZL_Encoder_getOperationContext(ctx);
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_Decoder* ctx)
{
    return ZL_Decoder_getOperationContext(ctx);
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_Graph* ctx)
{
    return ZL_Graph_getOperationContext(ctx);
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_Edge* ctx)
{
    return ZL_Edge_getOperationContext(ctx);
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_CompressorSerializer* ctx)
{
    return ZL_CompressorSerializer_getOperationContext(ctx);
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_CompressorDeserializer* ctx)
{
    return ZL_CompressorDeserializer_getOperationContext(ctx);
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_Segmenter* ctx)
{
    return ZL_Segmenter_getOperationContext(ctx);
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_ErrorContext* ctx)
{
    return ZL_ErrorContext_getOperationContext(ctx);
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        ZL_OperationContext* opCtx)
{
    return opCtx;
}
ZL_INLINE ZL_CONST_FN ZL_OperationContext* ZL_getOperationContextImpl(
        std::nullptr_t)
{
    return nullptr;
}

extern "C" {

#    define ZL_GET_OPERATION_CONTEXT_IMPL(ctx) ZL_getOperationContextImpl(ctx)

#else

#    define ZL_GET_OPERATION_CONTEXT_IMPL(ctx)                                             \
        _Generic(                                                                          \
                (ctx),                                                                     \
                ZL_Compressor*: ZL_Compressor_getOperationContext(                         \
                        (void*)(ctx)),                                                     \
                ZL_CCtx*: ZL_CCtx_getOperationContext((void*)(ctx)),                       \
                ZL_DCtx*: ZL_DCtx_getOperationContext((void*)(ctx)),                       \
                ZL_Encoder*: ZL_Encoder_getOperationContext((void*)(ctx)),                 \
                ZL_Decoder*: ZL_Decoder_getOperationContext((void*)(ctx)),                 \
                ZL_Graph*: ZL_Graph_getOperationContext((void*)(ctx)),                     \
                ZL_Edge*: ZL_Edge_getOperationContext((void*)(ctx)),                       \
                ZL_CompressorSerializer*: ZL_CompressorSerializer_getOperationContext(     \
                        (void*)(ctx)),                                                     \
                ZL_CompressorDeserializer*: ZL_CompressorDeserializer_getOperationContext( \
                        (void*)(ctx)),                                                     \
                ZL_Segmenter*: ZL_Segmenter_getOperationContext((void*)(ctx)),             \
                ZL_ErrorContext*: ZL_ErrorContext_getOperationContext(                     \
                        (void*)(ctx)),                                                     \
                ZL_OperationContext*: (ctx),                                               \
                void*: ZL_NULL_getOperationContext(ctx))

#endif

ZL_ErrorContext* ZL_OperationContext_getDefaultErrorContext(
        ZL_OperationContext* opCtx);

#define ZL_GET_ERROR_CONTEXT_IMPL(ctx) \
    (ZL_OperationContext_getDefaultErrorContext(ZL_GET_OPERATION_CONTEXT(ctx)))

#if defined(__cplusplus)
}
#endif

#endif // ZSTRONG_DETAIL_ZS2_ERRORS_CONTEXT_H
