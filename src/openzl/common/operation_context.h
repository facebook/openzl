// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_COMMON_OPERATION_CONTEXT_H
#define ZSTRONG_COMMON_OPERATION_CONTEXT_H

#include "openzl/common/introspection.h"
#include "openzl/common/vector.h"
#include "openzl/detail/zl_error_context.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

// Forward declare to avoid dependencies
typedef struct ZL_DynamicErrorInfo_s ZL_DynamicErrorInfo;

typedef enum {
    ZL_Operation_compress,
    ZL_Operation_decompress,
    ZL_Operation_createCGraph,
    ZL_Operation_serializeCompressor,
    ZL_Operation_deserializeCompressor,
} ZL_Operation;

DECLARE_VECTOR_POINTERS_TYPE(ZL_DynamicErrorInfo)

DECLARE_VECTOR_TYPE(ZL_Error)

struct ZL_OperationContext_s {
    ZL_Operation operation;

    // Owning references to the rich error info structs that are pointed-to
    // by error objects as they bubble up the stack and are (potentiall)
    // returned to the user.
    VECTOR_POINTERS(ZL_DynamicErrorInfo) errorInfos;

    // Non-owning references to errors (owned by the errorInfos field) which
    // have been marked as warnings (i.e., are no longer exposed to callers
    // by being bubbled up the stack as operations fail and return errors,
    // but are instead recorded here to be queried as desired.
    //
    // Somewhat non-intuitively, at present, warnings are a subset of errors.
    // (They are the subset of errors that have been explicitly marked
    // recoverable, more or less.)
    VECTOR(ZL_Error) warnings;

    ZL_ErrorContext* defaultScopeContext;

    // Introspection hooks for the current operation. Realistically only
    // relevant for CCtx and DCtx. These allow the user to execute custom code
    // snippets at specified CWAYPOINT()s / DWAYPOINT()s within the operation.
    // See common/introspection.h for more details.
    ZL_CompressIntrospectionHooks compressIntrospectionHooks;
    bool hasCompressionHooks;
    bool hasDecompressionHooks;
};

void ZL_OC_init(ZL_OperationContext* opCtx);

/// Releases the resources owned by the @p opCtx.
void ZL_OC_destroy(ZL_OperationContext* opCtx);

/// Mark the start of an operation, and reset the operation context.
void ZL_OC_startOperation(ZL_OperationContext* opCtx, ZL_Operation op);

/// Set the error flag on the operation context, and get a pointer to the
/// ZL_DynamicErrorInfo that can be filled with context about the error.
ZL_DynamicErrorInfo* ZL_OC_setError(ZL_OperationContext* opCtx);

/// Store the provided error in the warning vector.
/// @returns Whether storing the error was successful (i.e., due to the error
/// not being managed by this context or due to an allocation or capacity
/// failure in the underlying storage).
bool ZL_OC_markAsWarning(ZL_OperationContext* opCtx, ZL_Error error);

/// Clear the error flag on the operation context, and reset the
/// ZL_DynamicErrorInfo.
void ZL_OC_clearErrors(ZL_OperationContext* opCtx);

/// @returns The number of errors (of all types) currently stored in this
/// context.
size_t ZL_OC_numErrors(const ZL_OperationContext* opCtx);

/// @returns The number of warnings currently stored in this context.
size_t ZL_OC_numWarnings(const ZL_OperationContext* opCtx);

/// @returns The current error info or NULL if there is no error.
ZL_DynamicErrorInfo const* ZL_OC_getError(
        ZL_OperationContext const* opCtx,
        ZL_ErrorCode opCode);

/// @returns The idx'th warning stored in the context.
ZL_Error ZL_OC_getWarning(ZL_OperationContext const* opCtx, size_t idx);

ZL_Error_Array ZL_OC_getWarnings(ZL_OperationContext const* opCtx);

/// @returns The context string for the provided error, if that error is
/// managed by this operation context. Otherwise returns NULL.
const char* ZL_OC_getErrorContextString(
        const ZL_OperationContext* cctx,
        ZL_Error error);

/// @returns The default scope context that points to this operation context.
ZL_ErrorContext const* ZL_OC_defaultScopeContext(
        ZL_OperationContext const* opCtx);

ZL_END_C_DECLS

#endif
