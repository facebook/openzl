// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/operation_context.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "openzl/common/errors_internal.h"

void ZL_OC_init(ZL_OperationContext* opCtx)
{
    if (opCtx == NULL) {
        return;
    }
    memset(opCtx, 0, sizeof(*opCtx));
    VECTOR_INIT(opCtx->errorInfos, 1024);
    VECTOR_INIT(opCtx->warnings, 1024);
    opCtx->hasCompressionHooks   = false;
    opCtx->hasDecompressionHooks = false;
}

void ZL_OC_destroy(ZL_OperationContext* opCtx)
{
    if (opCtx == NULL) {
        return;
    }
    free(opCtx->defaultScopeContext);
    VECTOR_DESTROY(opCtx->warnings);
    for (size_t i = 0; i < VECTOR_SIZE(opCtx->errorInfos); i++) {
        ZL_DEE_free(VECTOR_AT(opCtx->errorInfos, i));
    }
    VECTOR_DESTROY(opCtx->errorInfos);
    memset(opCtx, 0, sizeof(*opCtx));
}

void ZL_OC_startOperation(ZL_OperationContext* opCtx, ZL_Operation op)
{
    if (opCtx == NULL) {
        return;
    }
    opCtx->operation = op;
    if (opCtx->defaultScopeContext == NULL)
        opCtx->defaultScopeContext =
                (ZL_ErrorContext*)malloc(sizeof(ZL_ErrorContext));
    if (opCtx->defaultScopeContext != NULL) {
        memset(opCtx->defaultScopeContext, 0, sizeof(ZL_ErrorContext));
        opCtx->defaultScopeContext->opCtx = opCtx;
    }
    ZL_OC_clearErrors(opCtx);
}

ZL_DynamicErrorInfo* ZL_OC_setError(ZL_OperationContext* opCtx)
{
    if (opCtx == NULL) {
        return NULL;
    }
    ZL_DynamicErrorInfo* dee = ZL_DEE_create();
    if (!dee || !VECTOR_PUSHBACK(opCtx->errorInfos, dee)) {
        ZL_DEE_free(dee);
        return NULL;
    }
    return dee;
}

bool ZL_OC_markAsWarning(ZL_OperationContext* opCtx, ZL_Error error)
{
    if (opCtx == NULL) {
        return false;
    }
    if (!ZL_E_isError(error)) {
        return false;
    }
    if (ZL_E_dy(error) == NULL) {
        error = ZL_E_convertToDynamic(opCtx, error);
        if (ZL_E_dy(error) == NULL) {
            return false;
        }
    }
    ZL_DynamicErrorInfo* dy = ZL_E_dy(error);
    bool owned              = false;
    for (size_t i = 0; i < VECTOR_SIZE(opCtx->errorInfos); i++) {
        if (VECTOR_AT(opCtx->errorInfos, i) == dy) {
            owned = true;
            break;
        }
    }
    if (!owned) {
        return false;
    }
    return VECTOR_PUSHBACK(opCtx->warnings, error);
}

void ZL_OC_clearErrors(ZL_OperationContext* opCtx)
{
    if (opCtx == NULL) {
        return;
    }
    VECTOR_CLEAR(opCtx->warnings);
    for (size_t i = 0; i < VECTOR_SIZE(opCtx->errorInfos); i++) {
        ZL_DEE_free(VECTOR_AT(opCtx->errorInfos, i));
    }
    VECTOR_CLEAR(opCtx->errorInfos);
}

size_t ZL_OC_numErrors(const ZL_OperationContext* opCtx)
{
    if (opCtx == NULL) {
        return 0;
    }
    return VECTOR_SIZE(opCtx->errorInfos);
}

size_t ZL_OC_numWarnings(const ZL_OperationContext* opCtx)
{
    if (opCtx == NULL) {
        return 0;
    }
    return VECTOR_SIZE(opCtx->warnings);
}

ZL_DynamicErrorInfo const* ZL_OC_getLastError(ZL_OperationContext const* opCtx)
{
    if (opCtx == NULL)
        return NULL;
    if (VECTOR_SIZE(opCtx->errorInfos) == 0)
        return NULL;
    ZL_DynamicErrorInfo* dee =
            VECTOR_AT(opCtx->errorInfos, VECTOR_SIZE(opCtx->errorInfos) - 1);
    if (ZL_DEE_code(dee) == ZL_ErrorCode_no_error)
        return NULL;
    // Allow error codes to mismatch if both are errors

    return dee;
}

ZL_Error ZL_OC_getWarning(ZL_OperationContext const* opCtx, size_t idx)
{
    if (opCtx == NULL || idx >= ZL_OC_numWarnings(opCtx)) {
        return (ZL_Error){ ._code = ZL_ErrorCode_no_error };
    }
    return VECTOR_AT(opCtx->warnings, idx);
}

ZL_Error_Array ZL_OC_getWarnings(ZL_OperationContext const* opCtx)
{
    if (opCtx == NULL)
        return (ZL_Error_Array){ .errors = NULL, .size = 0 };
    size_t size = ZL_OC_numWarnings(opCtx);
    return (ZL_Error_Array){ .errors =
                                     size ? VECTOR_DATA(opCtx->warnings) : NULL,
                             .size = size };
}

ZL_DynamicErrorInfo const* ZL_OC_findError(
        const ZL_OperationContext* opCtx,
        ZL_Error error)
{
    if (opCtx == NULL) {
        return NULL;
    }
    ZL_DynamicErrorInfo* const dy = ZL_E_dy(error);
    if (dy == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < VECTOR_SIZE(opCtx->errorInfos); i++) {
        if (dy == VECTOR_AT(opCtx->errorInfos, i)) {
            return dy;
        }
    }
    return NULL;
}

const char* ZL_OC_getErrorContextString(
        const ZL_OperationContext* opCtx,
        ZL_Error error)
{
    const ZL_DynamicErrorInfo* const dy = ZL_OC_findError(opCtx, error);
    if (dy != NULL) {
        return ZL_E_str(error);
    }

    // TODO: handle static infos?
    return "Error does not belong to this context object, you must pass this "
           "report into the context that created the error (ZL_CCtx for "
           "compression, ZL_DCtx for decompression, ZL_Compressor for graph "
           "creation)";
}

bool ZL_OperationContext_ownsError(
        const ZL_OperationContext* opCtx,
        ZL_Error* error)
{
    if (error == NULL) {
        return false;
    }
    return ZL_OC_findError(opCtx, *error) != NULL;
}

ZL_ErrorContext const* ZL_OC_defaultScopeContext(
        ZL_OperationContext const* opCtx)
{
    if (opCtx == NULL) {
        return NULL;
    }
    return opCtx->defaultScopeContext;
}

ZL_ErrorContext* ZL_OperationContext_getDefaultErrorContext(
        ZL_OperationContext* opCtx)
{
    if (opCtx == NULL) {
        return NULL;
    }
    return opCtx->defaultScopeContext;
}

ZL_OperationContext* ZL_ErrorContext_getOperationContext(
        ZL_ErrorContext* errCtx)
{
    if (errCtx == NULL) {
        return NULL;
    }
    return errCtx->opCtx;
}

ZL_OperationContext* ZL_NULL_getOperationContext(void* ctx)
{
    ZL_ASSERT_NULL(ctx);
    return NULL;
}
