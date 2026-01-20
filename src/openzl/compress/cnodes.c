// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/cnodes.h"
#include "openzl/common/allocation.h" // ALLOC_*, ZL_zeroes
#include "openzl/common/assertion.h"
#include "openzl/common/limits.h" // ZL_ENCODER_CUSTOM_NODE_LIMIT
#include "openzl/common/vector.h"
#include "openzl/common/wire_format.h" // trt_standard
#include "openzl/compress/cnode.h"
#include "openzl/compress/localparams.h"
#include "openzl/shared/mem.h" // ZL_memcpy
#include "openzl/shared/xxhash.h"
#include "openzl/zl_errors.h"

size_t MaterializedParamMap_hash(const MaterializedParamKey* key)
{
    XXH3_state_t hs;
    XXH3_INITSTATE(&hs);
    XXH3_64bits_reset(&hs);

    size_t lpHash = ZL_LocalParams_hash(&key->localParams);
    XXH3_64bits_update(&hs, &lpHash, sizeof(lpHash));

    XXH3_64bits_update(
            &hs,
            &key->materializer.materializeFn,
            sizeof(key->materializer.materializeFn));
    XXH3_64bits_update(
            &hs,
            &key->materializer.dematerializeFn,
            sizeof(key->materializer.dematerializeFn));
    XXH3_64bits_update(
            &hs, &key->materializer.opaque, sizeof(key->materializer.opaque));

    return XXH3_64bits_digest(&hs);
}

bool MaterializedParamMap_eq(
        const MaterializedParamKey* lhs,
        const MaterializedParamKey* rhs)
{
    if (!ZL_LocalParams_eq(&lhs->localParams, &rhs->localParams)) {
        return false;
    }

    if (lhs->materializer.materializeFn != rhs->materializer.materializeFn
        || lhs->materializer.dematerializeFn
                != rhs->materializer.dematerializeFn) {
        return false;
    }

    if (lhs->materializer.opaque != rhs->materializer.opaque) {
        return false;
    }

    return true;
}

ZL_Report CTM_init(CNodes_manager* ctm, ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    VECTOR_INIT(ctm->cnodes, ZL_ENCODER_CUSTOM_NODE_LIMIT);
    ctm->materializedParams =
            MaterializedParamMap_create(ZL_ENCODER_CUSTOM_NODE_LIMIT);
    ZL_OpaquePtrRegistry_init(&ctm->opaquePtrs);
    ctm->opCtx = opCtx;
    // Note: this Arena could also be borrowed from the cgraph,
    // but it doesn't have one (yet).
    // Note 2: this is why this init function can fail
    ctm->allocator = ALLOC_HeapArena_create();
    ZL_ERR_IF_NULL(ctm->allocator, allocation);
    return ZL_returnSuccess();
}

void CTM_destroy(CNodes_manager* ctm)
{
    ZL_DLOG(OBJ, "CTM_destroy");
    ZL_ASSERT_NN(ctm);
    ZL_OpaquePtrRegistry_destroy(&ctm->opaquePtrs);
    // Dematerialize all materialized params before freeing any corresponding
    // CNodes
    MaterializedParamMap_Iter iter =
            MaterializedParamMap_iter(&ctm->materializedParams);
    MaterializedParamMap_Entry const* entry;
    while ((entry = MaterializedParamMap_Iter_next(&iter)) != NULL) {
        if (entry->val.materializedParam != NULL
            && entry->key.materializer.dematerializeFn != NULL) {
            entry->key.materializer.dematerializeFn(
                    NULL, entry->val.materializedParam);
        }
    }
    MaterializedParamMap_destroy(&ctm->materializedParams);
    VECTOR_DESTROY(ctm->cnodes);
    ALLOC_Arena_freeArena(ctm->allocator);
    ZL_zeroes(ctm, sizeof(*ctm));
}

void CTM_reset(CNodes_manager* ctm)
{
    ZL_DLOG(FRAME, "CTM_reset");
    ZL_ASSERT_NN(ctm);
    ZL_OpaquePtrRegistry_reset(&ctm->opaquePtrs);
    // Dematerialize all materialized params before freeing any corresponding
    // CNodes
    MaterializedParamMap_Iter iter =
            MaterializedParamMap_iter(&ctm->materializedParams);
    MaterializedParamMap_Entry const* entry;
    while ((entry = MaterializedParamMap_Iter_next(&iter)) != NULL) {
        if (entry->val.materializedParam != NULL
            && entry->key.materializer.dematerializeFn != NULL) {
            entry->key.materializer.dematerializeFn(
                    NULL, entry->val.materializedParam);
        }
    }
    MaterializedParamMap_clear(&ctm->materializedParams);
    VECTOR_RESET(ctm->cnodes);
    ALLOC_Arena_freeAll(ctm->allocator);
}

// Implementation Note: NULL is a valid return value for @bufferPtr,
// therefore it can't be used as an error signal
static ZL_Report
CTM_transferBuffer(CNodes_manager* ctm, const void** bufferPtr, size_t nbytes)
{
    if (nbytes == 0) {
        *bufferPtr = NULL;
    } else {
        void* const dst = ALLOC_Arena_malloc(ctm->allocator, nbytes);
        ZL_RET_R_IF_NULL(allocation, dst);
        ZL_memcpy(dst, *bufferPtr, nbytes);
        *bufferPtr = dst;
    }
    return ZL_returnSuccess();
}

static ZL_Report CTM_transferLocalParams(
        CNodes_manager* cnm,
        ZL_LocalParams* lp)
{
    return LP_transferLocalParams(cnm->allocator, lp);
}

// Validate that paramId is not invalid and not already in use by existing local
// params
static ZL_Report validateMaterializedParamId(ZL_LocalParams* lp, int paramId)
{
    ZL_RET_R_IF_EQ(
            node_invalid,
            paramId,
            ZL_LP_INVALID_PARAMID,
            "Materialized paramId cannot be ZL_LP_INVALID_PARAMID");

    ZL_RefParam rp = LP_getLocalRefParam(lp, paramId);
    ZL_RET_R_IF_NE(
            node_invalid,
            rp.paramId,
            ZL_LP_INVALID_PARAMID,
            "Materialized paramId %d is already registered",
            paramId);

    ZL_IntParam ip = LP_getLocalIntParam(lp, paramId);
    ZL_RET_R_IF_NE(
            node_invalid,
            ip.paramId,
            ZL_LP_INVALID_PARAMID,
            "Materialized paramId %d is already registered as an intParam",
            paramId);
    return ZL_returnSuccess();
}

/* CTM_addMaterializedRefParam():
 * Add a materialized param to the refParams of the local params.
 * @return : success, or error if paramId is already in use
 */
static ZL_Report CTM_addMaterializedRefParam(
        CNodes_manager* cnm,
        ZL_LocalParams* lp,
        int paramId,
        const void* materializedObj)
{
    ZL_ASSERT_NN(cnm);
    ZL_ASSERT_NN(lp);
    ZL_RESULT_DECLARE_SCOPE_REPORT(cnm->opCtx);

    // Allocate new refParams array with one more entry
    size_t const newNbRefParams = lp->refParams.nbRefParams + 1;
    ZL_RefParam* newRefParams   = ALLOC_Arena_malloc(
            cnm->allocator, newNbRefParams * sizeof(ZL_RefParam));
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

/* CTM_transferStreamTypes():
 * Transfer streamType information from user-controlled memory
 * towards internal memory,
 * so that user-controlled memory can be flushed / released after registration.
 * @outST[0] is an in+out parameter,
 * it is modified to point into internal memory.
 */
static ZL_Report
CTM_transferStreamTypes(CNodes_manager* cnm, const ZL_Type** outST, size_t nbST)
{
    ZL_DLOG(BLOCK, "CTM_transferStreamTypes : nbST=%zu", nbST);
    void const* buffer = *outST;
    ZL_RET_R_IF_ERR(CTM_transferBuffer(cnm, &buffer, nbST * sizeof(**outST)));
    *outST = buffer;
    return ZL_returnSuccess();
}

static ZL_Report CTM_transferPrivateParam(
        CNodes_manager* cnm,
        InternalTransform_Desc* itd)
{
    ZL_ASSERT_NN(itd);
    ZL_DLOG(BLOCK, "CTM_transferPrivateParam: ppSize=%zu", itd->ppSize);
    return CTM_transferBuffer(cnm, &itd->privateParam, itd->ppSize);
}

/**
 * Find or create a materialized param in the registry. This function will
 * handle cleaning up the materialized object if an error occurs.
 */
static ZL_Report CTM_addOrReuseMaterializedParam(
        CNodes_manager* ctm,
        ZL_LocalParams* lp,
        const ZL_MaterializerDesc* materializer)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(ctm->opCtx);
    if (materializer == NULL || materializer->materializeFn == NULL) {
        return ZL_returnSuccess();
    }
    if (materializer->dematerializeFn == NULL) {
        ZL_ERR(GENERIC,
               "Materializer must provide a valid dematerialize function pointer");
    }

    // Create key for lookup
    MaterializedParamKey key = {
        .localParams  = *lp,
        .materializer = *materializer,
    };

    // Search for existing materialized param with same key
    MaterializedParamMap_Entry const* existingEntry =
            MaterializedParamMap_find(&ctm->materializedParams, &key);
    if (existingEntry != NULL) {
        return CTM_addMaterializedRefParam(
                ctm,
                lp,
                materializer->paramId,
                existingEntry->val.materializedParam);
    }

    // Not found, create new materialized param
    ZL_RESULT_OF(ZL_VoidPtr) matResult = materializer->materializeFn(NULL, lp);
    ZL_ERR_IF_ERR(matResult);

    void* materialized = ZL_RES_value(matResult);

    MaterializedParamEntry newEntry = {
        .materializedParam = materialized,
    };

    MaterializedParamMap_Entry entryToInsert = {
        .key = key,
        .val = newEntry,
    };

    MaterializedParamMap_Insert insertResult = MaterializedParamMap_insert(
            &ctm->materializedParams, &entryToInsert);
    if (insertResult.badAlloc) {
        materializer->dematerializeFn(NULL, materialized);
        ZL_ERR(allocation, "Failed to insert materialized param into map");
    }

    return CTM_addMaterializedRefParam(
            ctm, lp, materializer->paramId, materialized);
}

/*
 * CTM_registerCNode() :
 * @return : ID of the registered CTransform *from a CTM perspective*.
 * The method copies parameters (integer and general) within local storage.
 * General Parameters are aligned on 8-bytes boundaries by default.
 * (Note : there is no way to control alignment of general parameters now,
 *         maybe this could be added later if needed).
 *
 * The following registration functions are essentially helpers.
 * They can also be achieved with just CTM_registerCNode(),
 * requiring just a few more lines at the invocation place
 * (nodemgr.c mostly for the time being).
 */
static ZL_RESULT_OF(CNodeID) CTM_registerCNode(
        CNodes_manager* ctm,
        const CNode* srcCNode,
        const char* prefix)
{
    ZL_ASSERT_NN(ctm);
    ZL_ASSERT_NN(srcCNode);
    ZL_DLOG(BLOCK,
            "CTM_registerCNode (type: %u) (for local ID=%u)",
            srcCNode->nodetype,
            VECTOR_SIZE(ctm->cnodes));
    ZL_ASSERT_NULL(
            srcCNode->transformDesc.publicDesc.opaque.freeFn,
            "Must already be registered with ZL_OpaquePtrRegistry");

    ZL_IDType const lnid = (ZL_IDType)VECTOR_SIZE(ctm->cnodes);

    // Need to check the name before pushing into the vector
    ZL_Name name;
    ZL_RET_T_IF_ERR(CNodeID, ZL_Name_init(&name, ctm->allocator, prefix, lnid));

    ZL_RET_T_IF_NOT(
            CNodeID,
            temporaryLibraryLimitation,
            VECTOR_PUSHBACK(ctm->cnodes, *srcCNode));

    CNode* const cnode = &VECTOR_AT(ctm->cnodes, lnid);
    ZL_DLOG(SEQ, "cnode address = %p", cnode);

    cnode->maybeName                     = name;
    cnode->transformDesc.publicDesc.name = ZL_Name_unique(&name);

    // Copy parameters into local storage (no dependency on input's memory)
    switch (cnode->nodetype) {
        /* only localParams */
        case node_internalTransform:
            ZL_RET_T_IF_ERR(
                    CNodeID,
                    CTM_transferPrivateParam(ctm, &cnode->transformDesc));
            ZL_MIEncoderDesc* const trDesc = &cnode->transformDesc.publicDesc;
            ZL_RET_T_IF_ERR(
                    CNodeID,
                    CTM_transferLocalParams(ctm, &trDesc->localParams));
            // Materialize params if materializer is provided (with
            // deduplication)
            if (trDesc->materializer.materializeFn != NULL) {
                // Validate provided params don't contain the paramId reserved
                // for the materializer
                ZL_RET_T_IF_ERR(
                        CNodeID,
                        validateMaterializedParamId(
                                &trDesc->localParams,
                                trDesc->materializer.paramId));
                // Add the materialized object to refParams
                ZL_RET_T_IF_ERR(
                        CNodeID,
                        CTM_addOrReuseMaterializedParam(
                                ctm,
                                &trDesc->localParams,
                                &trDesc->materializer));
            }
            // A valid transform must have at least one input
            ZL_RET_T_IF_LT(
                    CNodeID,
                    node_invalid_input,
                    CNODE_getNbInputPorts(cnode),
                    1,
                    "Transform '%s' must declare at least 1 Input Port!",
                    CNODE_getName(cnode));
            // and at most ZL_runtimeNodeInputLimit() inputs
            ZL_RET_T_IF_GT(
                    CNodeID,
                    node_invalid_input,
                    CNODE_getNbInputPorts(cnode),
                    ZL_runtimeNodeInputLimit(ZL_MAX_FORMAT_VERSION),
                    "Too many inputs (%u) defined for transform '%s' (max=%u)",
                    CNODE_getNbInputPorts(cnode),
                    CNODE_getName(cnode),
                    ZL_runtimeNodeInputLimit(ZL_MAX_FORMAT_VERSION));
            ZL_RET_T_IF_ERR(
                    CNodeID,
                    CTM_transferStreamTypes(
                            ctm, &trDesc->gd.inputTypes, trDesc->gd.nbInputs));
            ZL_RET_T_IF_ERR(
                    CNodeID,
                    CTM_transferStreamTypes(
                            ctm, &trDesc->gd.soTypes, trDesc->gd.nbSOs));
            ZL_RET_T_IF_ERR(
                    CNodeID,
                    CTM_transferStreamTypes(
                            ctm, &trDesc->gd.voTypes, trDesc->gd.nbVOs));
            // Add automatic state ID when none provided
            if (trDesc->trStateMgr.optionalStateID == 0) {
                // Note: currently, void* opaque pointers are not exposed.
                // So using @transform_f only for the key.
                trDesc->trStateMgr.optionalStateID = (size_t)XXH3_64bits(
                        &trDesc->transform_f, sizeof(trDesc->transform_f));
            }
            break;
        case node_illegal:
            // We should never reach here with an illegal node
            ZL_ASSERT_FAIL("Impossible, illegal node type");
            ZL_RET_T_ERR(
                    CNodeID, GENERIC, "Trying to register an illegal node");
        default:
            ZL_ASSERT_FAIL(
                    "node type (%u) not possible at this stage",
                    cnode->nodetype);
    }
    return ZL_RESULT_WRAP_VALUE(CNodeID, (CNodeID){ lnid });
}

ZL_RESULT_OF(CNodeID)
CTM_registerCustomTransform(
        CNodes_manager* ctm,
        const InternalTransform_Desc* ctd)
{
    ZL_DLOG(BLOCK, "CTM_registerCustomTransform");
    ZL_RET_T_IF_ERR(
            CNodeID,
            ZL_OpaquePtrRegistry_register(
                    &ctm->opaquePtrs, ctd->publicDesc.opaque));
    CNode cnode      = { .nodetype      = node_internalTransform,
                         .publicIDtype  = trt_custom,
                         .transformDesc = *ctd,
                         .baseNodeID    = ZL_NODE_ILLEGAL };
    const char* name = ctd->publicDesc.name;
    // Registered => No need to free
    cnode.transformDesc.publicDesc.opaque.freeFn = NULL;
    return CTM_registerCNode(ctm, &cnode, name);
}

ZL_RESULT_OF(CNodeID)
CTM_registerStandardTransform(
        CNodes_manager* ctm,
        const InternalTransform_Desc* ctd,
        unsigned minFormatVersion,
        unsigned maxFormatVersion)
{
    ZL_DLOG(BLOCK, "CTM_registerStandardTransform");
    ZL_RET_T_IF_ERR(
            CNodeID,
            ZL_OpaquePtrRegistry_register(
                    &ctm->opaquePtrs, ctd->publicDesc.opaque));
    CNode cnode      = { .nodetype         = node_internalTransform,
                         .publicIDtype     = trt_standard,
                         .minFormatVersion = minFormatVersion,
                         .maxFormatVersion = maxFormatVersion,
                         .transformDesc    = *ctd,
                         .baseNodeID       = ZL_NODE_ILLEGAL };
    const char* name = ctd->publicDesc.name;
    // Registered => No need to free
    cnode.transformDesc.publicDesc.opaque.freeFn = NULL;
    return CTM_registerCNode(ctm, &cnode, name);
}

ZL_RESULT_OF(CNodeID)
CTM_parameterizeNode(
        CNodes_manager* ctm,
        const CNode* srcCNode,
        const ZL_ParameterizedNodeDesc* desc)
{
    ZL_RET_T_IF_NE(
            CNodeID,
            node_invalid,
            srcCNode->nodetype,
            node_internalTransform,
            "Invalid CNode");
    CNode clonedCNode      = *srcCNode;
    clonedCNode.baseNodeID = desc->node;
    if (desc->localParams) {
        clonedCNode.transformDesc.publicDesc.localParams = *desc->localParams;
    }
    if (desc->name == NULL) {
        const ZL_Name name = CNODE_getNameObj(srcCNode);
        // Use the name prefix rather than the unique name, because this node
        // needs a new non-anchor name.
        const char* prefix = ZL_Name_prefix(&name);
        return CTM_registerCNode(ctm, &clonedCNode, prefix);
    } else {
        return CTM_registerCNode(ctm, &clonedCNode, desc->name);
    }
}

void CTM_rollback(CNodes_manager* ctm, CNodeID id)
{
    // Note that local params and materialized params are not rolled back. Local
    // params will stay allocated in the arena and materialized params will stay
    // allocated in the vector.
    ZL_ASSERT_EQ(id.cnid + 1, VECTOR_SIZE(ctm->cnodes));
    VECTOR_POPBACK(ctm->cnodes);
}

const CNode* CTM_getCNode(const CNodes_manager* ctm, CNodeID cnodeid)
{
    ZL_ASSERT_NN(ctm);
    if (cnodeid.cnid >= VECTOR_SIZE(ctm->cnodes))
        return NULL;
    return &VECTOR_AT(ctm->cnodes, cnodeid.cnid);
}

ZL_IDType CTM_nbCNodes(const CNodes_manager* ctm)
{
    return (ZL_IDType)VECTOR_SIZE(ctm->cnodes);
}
