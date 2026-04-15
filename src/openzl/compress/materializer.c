// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/materializer.h"
#include "openzl/common/allocation.h" // ALLOC_*
#include "openzl/compress/localparams.h" // LP_getLocalRefParam, LP_getLocalIntParam

// ******************************************************************
// ZL_Materializer
// ******************************************************************

ZL_Materializer* ZL_Materializer_create(
        Arena* allocator,
        ZL_MaterializerDesc matDesc)
{
    ZL_Materializer* mat = ALLOC_Arena_malloc(allocator, sizeof(*mat));
    if (mat == NULL) {
        return NULL;
    }
    mat->persistentArena = allocator;
    mat->scratchArena    = ALLOC_StackArena_create();
    if (mat->scratchArena == NULL) {
        ALLOC_Arena_free(allocator, mat);
        return NULL;
    }
    mat->opaquePtr = matDesc.opaque;
    mat->opCtx     = NULL;
    return mat;
}

void ZL_Materializer_free(ZL_Materializer* mat)
{
    if (mat == NULL) {
        return;
    }
    ALLOC_Arena_freeArena(mat->scratchArena);
    ALLOC_Arena_free(mat->persistentArena, mat);
}

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

// ******************************************************************
// MaterializedParamMap
// ******************************************************************

// Validate that paramId is not invalid and not already in use by existing local
// params
ZL_Report MPM_validateMaterializedParamId(const ZL_LocalParams* lp, int paramId)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_EQ(
            paramId,
            ZL_LP_INVALID_PARAMID,
            GENERIC,
            "Materialized paramId cannot be ZL_LP_INVALID_PARAMID");

    ZL_RefParam rp = LP_getLocalRefParam(lp, paramId);
    ZL_ERR_IF_NE(
            rp.paramId,
            ZL_LP_INVALID_PARAMID,
            GENERIC,
            "Materialized paramId %d is already registered",
            paramId);

    ZL_IntParam ip = LP_getLocalIntParam(lp, paramId);
    ZL_ERR_IF_NE(
            ip.paramId,
            ZL_LP_INVALID_PARAMID,
            GENERIC,
            "Materialized paramId %d is already registered as an intParam",
            paramId);
    return ZL_returnSuccess();
}

ZL_Report MPM_addMaterializedRefParam(
        Arena* allocator,
        ZL_OperationContext* opCtx,
        ZL_LocalParams* lp,
        int paramId,
        const void* materializedObj)
{
    ZL_ASSERT_NN(allocator);
    ZL_ASSERT_NN(opCtx);
    ZL_ASSERT_NN(lp);
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);

    // Allocate new refParams array with one more entry
    size_t const newNbRefParams = lp->refParams.nbRefParams + 1;
    ZL_RefParam* newRefParams =
            ALLOC_Arena_malloc(allocator, newNbRefParams * sizeof(ZL_RefParam));
    ZL_ERR_IF_NULL(newRefParams, allocation);

    // Copy existing refParams
    for (size_t i = 0; i < lp->refParams.nbRefParams; i++) {
        newRefParams[i] = lp->refParams.refParams[i];
    }

    // Add the materialized param
    newRefParams[lp->refParams.nbRefParams] = (ZL_RefParam){
        .paramId   = paramId,
        .paramRef  = materializedObj,
        .paramSize = 0, // Size unknown for materialized objects
    };

    // Update the localParams
    lp->refParams.refParams   = newRefParams;
    lp->refParams.nbRefParams = newNbRefParams;

    return ZL_returnSuccess();
}

ZL_Report MPM_addOrReuseMaterializedParam(
        Arena* allocator,
        MaterializedParamMap* materializedParams,
        ZL_OperationContext* opCtx,
        ZL_LocalParams* lp,
        const ZL_MaterializerDesc* matDesc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    if (matDesc == NULL || matDesc->materializeFn == NULL) {
        return ZL_returnSuccess();
    }
    if (matDesc->dematerializeFn == NULL) {
        ZL_ERR(GENERIC,
               "Materializer must provide a valid dematerialize function pointer");
    }

    // Create key for lookup
    MaterializedParamKey key = {
        .localParams = *lp,
        .matDesc     = *matDesc,
    };

    // Search for existing materialized param with same key
    MaterializedParamMap_Entry const* existingEntry =
            MaterializedParamMap_find(materializedParams, &key);
    if (existingEntry != NULL) {
        return MPM_addMaterializedRefParam(
                allocator,
                opCtx,
                lp,
                matDesc->paramId,
                existingEntry->val.materializedParam);
    }

    // Not found, create new materialized param
    ZL_Materializer* mat = ZL_Materializer_create(allocator, *matDesc);
    ZL_ERR_IF_NULL(mat, allocation);
    ZL_RESULT_OF(ZL_VoidPtr) matResult = matDesc->materializeFn(mat, lp);
    ZL_Materializer_free(mat);
    ZL_ERR_IF_ERR(matResult);

    void* materialized = ZL_RES_value(matResult);

    MaterializedParamEntry newEntry = {
        .materializedParam = materialized,
    };

    MaterializedParamMap_Entry entryToInsert = {
        .key = key,
        .val = newEntry,
    };

    MaterializedParamMap_Insert insertResult =
            MaterializedParamMap_insert(materializedParams, &entryToInsert);
    if (insertResult.badAlloc) {
        ZL_Materializer* mat2 = ZL_Materializer_create(allocator, *matDesc);
        ZL_ERR_IF_NULL(mat2, allocation);
        matDesc->dematerializeFn(mat2, materialized);
        ZL_Materializer_free(mat2);
        ZL_ERR(allocation, "Failed to insert materialized param into map");
    }

    return MPM_addMaterializedRefParam(
            allocator, opCtx, lp, matDesc->paramId, materialized);
}

void MPM_dematerializeAllParams(
        MaterializedParamMap* materializedParams,
        Arena* allocator)
{
    MaterializedParamMap_Iter iter =
            MaterializedParamMap_iter(materializedParams);
    MaterializedParamMap_Entry const* entry;
    while ((entry = MaterializedParamMap_Iter_next(&iter)) != NULL) {
        if (entry->val.materializedParam != NULL) {
            ZL_ASSERT_NN(entry->key.matDesc.dematerializeFn);
            ZL_Materializer* mat =
                    ZL_Materializer_create(allocator, entry->key.matDesc);
            if (mat == NULL) {
                continue;
            }
            entry->key.matDesc.dematerializeFn(
                    mat, entry->val.materializedParam);
            ZL_Materializer_free(mat);
        }
    }
}

// ******************************************************************
// On-the-fly Materialization (for runtime params)
// ******************************************************************

ZL_RESULT_OF(OneshotMaterializationResult)
MPM_materializeOneshot(
        Arena* allocator,
        ZL_OperationContext* opCtx,
        const ZL_LocalParams* runtimeParams,
        const ZL_MaterializerDesc* matDesc)
{
    ZL_RESULT_DECLARE_SCOPE(OneshotMaterializationResult, opCtx);
    ZL_ASSERT_NN(allocator);
    ZL_ASSERT_NN(opCtx);
    ZL_ASSERT_NN(matDesc);
    ZL_ASSERT_NN(runtimeParams);
    // This should be impossible, since we check the validity of the
    // materializer when it's first registered alongside the base node.
    ZL_ERR_IF_NULL(
            matDesc->dematerializeFn,
            logicError,
            "Materializer must provide a valid materialize function pointer");

    ZL_ERR_IF_ERR(
            MPM_validateMaterializedParamId(runtimeParams, matDesc->paramId));

    ZL_Materializer* mat = ZL_Materializer_create(allocator, *matDesc);
    ZL_ERR_IF_NULL(mat, allocation);
    ZL_RESULT_OF(ZL_VoidPtr)
    matResult = matDesc->materializeFn(mat, runtimeParams);
    ZL_Materializer_free(mat);
    ZL_ERR_IF_ERR(matResult);
    void* materialized = ZL_RES_value(matResult);

    OneshotMaterializationResult retval = {
        .modifiedParams  = *runtimeParams,
        .materializedObj = materialized,
        .matDesc         = *matDesc,
    };

    ZL_Report addResult = MPM_addMaterializedRefParam(
            allocator,
            opCtx,
            &retval.modifiedParams,
            matDesc->paramId,
            materialized);

    if (ZL_isError(addResult)) {
        // Clean up materialized object
        ZL_Materializer* mat2 = ZL_Materializer_create(allocator, *matDesc);
        if (mat2 != NULL) {
            matDesc->dematerializeFn(mat2, materialized);
            ZL_Materializer_free(mat2);
        }
        ZL_ERR_IF_ERR(addResult);
    }

    return ZL_WRAP_VALUE(retval);
}

void MPM_dematerializeOneshot(
        Arena* allocator,
        OneshotMaterializationResult* matResult)
{
    ZL_ASSERT_NN(allocator);
    ZL_ASSERT_NN(matResult);

    if (matResult->materializedObj == NULL) {
        return;
    }

    // This should be impossible, since nonnull materializedObj indicates
    // materialization happened via MPM_materializeOneshot, and we check the
    // validity of the materializer in MPM_materializeOneshot
    ZL_ASSERT_NN(matResult->matDesc.dematerializeFn);

    ZL_Materializer* mat =
            ZL_Materializer_create(allocator, matResult->matDesc);
    if (mat != NULL) {
        matResult->matDesc.dematerializeFn(mat, matResult->materializedObj);
        ZL_Materializer_free(mat);
    }
}
