// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dict/materializer_ctx.h"

#include "openzl/common/allocation.h"
#include "openzl/common/unique_id.h"
#include "openzl/zl_materializer.h"

ZL_CONST_FN
ZL_OperationContext* ZL_Materializer_getOperationContext(ZL_Materializer* mat)
{
    if (mat == NULL) {
        return NULL;
    }
    return mat->opCtx;
}

void* ZL_Materializer_allocate(ZL_Materializer* mat, size_t size)
{
    if (mat == NULL || mat->persistentArena == NULL) {
        return NULL;
    }
    return ALLOC_Arena_malloc(mat->persistentArena, size);
}

void* ZL_Materializer_getScratchSpace(ZL_Materializer* mat, size_t size)
{
    if (mat == NULL || mat->scratchArena == NULL) {
        return NULL;
    }
    return ALLOC_Arena_malloc(mat->scratchArena, size);
}

void ZL_NOOP_DEMATERIALIZE(ZL_Materializer* matCtx, void* materialized)
        ZL_NOEXCEPT_FUNC_PTR
{
    (void)matCtx;
    (void)materialized;
    return;
}

bool ZL_MParamID_hasValue(const ZL_MParamID* id)
{
    if (id == NULL) {
        return false;
    }
    return ZL_UniqueID_isValid(&id->id);
}
