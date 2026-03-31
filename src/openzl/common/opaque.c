// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/opaque.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/limits.h"

void ZL_OpaquePtr_free(ZL_OpaquePtr opaque)
{
    if (opaque.freeFn != NULL) {
        opaque.freeFn(opaque.freeOpaquePtr, opaque.ptr);
    }
}

void ZL_OpaquePtrRegistry_init(ZL_OpaquePtrRegistry* registry)
{
    VECTOR_INIT(registry->ptrs, ZL_CONTAINER_SIZE_LIMIT);
}

void ZL_OpaquePtrRegistry_destroy(ZL_OpaquePtrRegistry* registry)
{
    const size_t size = VECTOR_SIZE(registry->ptrs);
    for (size_t i = 0; i < size; ++i) {
        ZL_OpaquePtr_free(VECTOR_AT(registry->ptrs, i));
    }
    VECTOR_DESTROY(registry->ptrs);
}

void ZL_OpaquePtrRegistry_reset(ZL_OpaquePtrRegistry* registry)
{
    ZL_OpaquePtrRegistry_destroy(registry);
    ZL_OpaquePtrRegistry_init(registry);
}

ZL_Report ZL_OpaquePtrRegistry_register(
        ZL_OpaquePtrRegistry* registry,
        ZL_OpaquePtr opaque)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (opaque.freeFn == NULL) {
        // Free is no-op => do not track
        return ZL_returnSuccess();
    }
    const bool success = VECTOR_PUSHBACK(registry->ptrs, opaque);
    if (!success) {
        ZL_OpaquePtr_free(opaque);
        ZL_ERR(allocation, "Failed to pushback to opaque vector");
    }
    return ZL_returnSuccess();
}
