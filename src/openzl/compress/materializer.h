// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_MATERIALIZER_H
#define ZSTRONG_COMPRESS_MATERIALIZER_H

#include "openzl/common/allocation.h" // Arena
#include "openzl/common/map.h"
#include "openzl/dict/materializer_ctx.h" // struct ZL_Materializer_s
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h" // ZL_RESULT_DECLARE_TYPE_IMPL, ZL_RESULT_OF

ZL_BEGIN_C_DECLS

// ******************************************************************
// MaterializedParamMap
// ******************************************************************

typedef struct {
    ZL_LocalParams localParams;
    ZL_MaterializerDesc matDesc; // The description provided at registration
} MaterializedParamKey;

typedef struct {
    void* materializedParam; // The materialized object
} MaterializedParamEntry;

// Custom hash and equality functions for MaterializedParamKey. Uses the
// LocalParams object and ZL_MaterializerDesc for the comparison.
size_t MaterializedParamMap_hash(const MaterializedParamKey* key);
bool MaterializedParamMap_eq(
        const MaterializedParamKey* lhs,
        const MaterializedParamKey* rhs);

ZL_DECLARE_CUSTOM_MAP_TYPE(
        MaterializedParamMap,
        MaterializedParamKey,
        MaterializedParamEntry);

/* MPM_validateMaterializedParamId():
 * Validate that paramId is not invalid and not already in use by existing
 * local params.
 * @return : success, or error if paramId is invalid or already in use
 */
ZL_Report MPM_validateMaterializedParamId(
        const ZL_LocalParams* lp,
        int paramId);

/* MPM_addMaterializedRefParam():
 * Add a materialized param to the refParams of the local params.
 * @return : success, or error if paramId is already in use
 */
ZL_Report MPM_addMaterializedRefParam(
        Arena* allocator,
        ZL_OperationContext* opCtx,
        ZL_LocalParams* lp,
        int paramId,
        const void* materializedObj);

/* MPM_addOrReuseMaterializedParam():
 * Find or create a materialized param in the registry. This function will
 * handle cleaning up the materialized object if an error occurs.
 * @return : success, or error
 */
ZL_Report MPM_addOrReuseMaterializedParam(
        Arena* allocator,
        Arena* scratchAllocator,
        MaterializedParamMap* materializedParams,
        ZL_OperationContext* opCtx,
        ZL_LocalParams* lp,
        const ZL_MaterializerDesc* matDesc);

/* MPM_dematerializeAllParams():
 * Function called before MaterializedParamMap_destroy() to free all non-arena
 * memory allocated by materializers.
 */
void MPM_dematerializeAllParams(MaterializedParamMap* materializedParams);

// ******************************************************************
// MaterializedParamMap: Static functions
// ******************************************************************

typedef struct {
    ZL_LocalParams modifiedParams;
    void* materializedObj;
    ZL_MaterializerDesc matDesc;
} OneshotMaterializationResult;
ZL_RESULT_DECLARE_TYPE(OneshotMaterializationResult);

/**
 * @brief Materialize runtime params on-the-fly
 *
 * Materializes the params using the provided materializer and returns modified
 * params with the materialized ref added. The returned context must be cleaned
 * up with MPM_dematerializeOneshot() after use.
 *
 * @param allocator Arena for allocations that should persist during use
 * @param opCtx Operation context for error reporting
 * @param runtimeParams The runtime params to materialize (will be copied)
 * @param matDesc The materializer descriptor
 * @return On success, returns modified params and associated objects required
 * to dematerialize.
 *
 * Note: we cannot simply return the modified local params because deallocation
 * would require freeing a const pointer.
 */
ZL_RESULT_OF(OneshotMaterializationResult)
MPM_materializeOneshot(
        Arena* allocator,
        Arena* scratchAllocator,
        ZL_OperationContext* opCtx,
        const ZL_LocalParams* runtimeParams,
        const ZL_MaterializerDesc* matDesc);

/**
 * @brief Clean up on-the-fly materialized params
 *
 * Dematerializes the object and frees all associated resources.
 */
void MPM_dematerializeOneshot(
        Arena* allocator,
        OneshotMaterializationResult* matResult);

ZL_END_C_DECLS

#endif
