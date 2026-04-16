// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/cdictmgr.h"

#include <string.h>

#include "openzl/common/allocation.h"
#include "openzl/common/unique_id.h"
#include "openzl/dict/bundle.h"
#include "openzl/dict/dict.h"
#include "openzl/dict/dict_constants.h"
#include "openzl/shared/xxhash.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

/* ================================================================
 * Composite key hash and equality
 * ================================================================ */

static ZL_MaterializerDesc2 CDictMgr_zeroMatDesc(void)
{
    ZL_MaterializerDesc2 desc;
    memset(&desc, 0, sizeof(desc));
    return desc;
}

size_t CDictMgr_DictMap_hash(const CDictMgr_DictKey* key)
{
    XXH3_state_t hs;
    XXH3_INITSTATE(&hs);
    XXH3_64bits_reset(&hs);

    size_t idHash = ZL_UniqueID_hash(&key->id);
    XXH3_64bits_update(&hs, &idHash, sizeof(idHash));

    XXH3_64bits_update(
            &hs,
            &key->matDesc.materializeFn,
            sizeof(key->matDesc.materializeFn));
    XXH3_64bits_update(
            &hs,
            &key->matDesc.dematerializeFn,
            sizeof(key->matDesc.dematerializeFn));
    XXH3_64bits_update(
            &hs, &key->matDesc.opaque.ptr, sizeof(key->matDesc.opaque.ptr));

    return XXH3_64bits_digest(&hs);
}

bool CDictMgr_DictMap_eq(
        const CDictMgr_DictKey* lhs,
        const CDictMgr_DictKey* rhs)
{
    if (!ZL_UniqueID_eq(&lhs->id, &rhs->id)) {
        return false;
    }

    if (lhs->matDesc.materializeFn != rhs->matDesc.materializeFn
        || lhs->matDesc.dematerializeFn != rhs->matDesc.dematerializeFn
        || lhs->matDesc.opaque.ptr != rhs->matDesc.opaque.ptr) {
        return false;
    }

    return true;
}

/* ================================================================
 * MParam map hash and equality
 * ================================================================ */

size_t CDictMgr_MParamMap_hash(const ZL_MParamID* key)
{
    return ZL_UniqueID_hash(&key->id);
}

bool CDictMgr_MParamMap_eq(const ZL_MParamID* lhs, const ZL_MParamID* rhs)
{
    return ZL_UniqueID_eq(&lhs->id, &rhs->id);
}

/* ================================================================
 * Lifecycle
 * ================================================================ */

ZL_Report CDictMgr_init(
        CDictMgr* mgr,
        const Nodes_manager* nmgr,
        const GraphsMgr* gm,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    memset(mgr, 0, sizeof(*mgr));
    mgr->nmgr  = nmgr;
    mgr->gm    = gm;
    mgr->opCtx = opCtx;
    mgr->arena = ALLOC_HeapArena_create();
    ZL_ERR_IF_NULL(mgr->arena, allocation);
    mgr->scratchArena = ALLOC_HeapArena_create();
    if (mgr->scratchArena == NULL) {
        ALLOC_Arena_freeArena(mgr->arena);
        mgr->arena = NULL;
        ZL_ERR(allocation);
    }
    mgr->dictsByID =
            CDictMgr_DictMap_createInArena(mgr->arena, ZL_MAX_DICTS_PER_BUNDLE);
    mgr->mparamBlobs = CDictMgr_MParamMap_createInArena(
            mgr->arena, ZL_MAX_DICTS_PER_BUNDLE);
    return ZL_returnSuccess();
}

void CDictMgr_destroy(CDictMgr* mgr)
{
    if (mgr == NULL)
        return;
    CDictMgr_DictMap_destroy(&mgr->dictsByID);
    CDictMgr_MParamMap_destroy(&mgr->mparamBlobs);
    if (mgr->scratchArena != NULL) {
        ALLOC_Arena_freeArena(mgr->scratchArena);
    }
    if (mgr->arena != NULL) {
        ALLOC_Arena_freeArena(mgr->arena);
    }
    memset(mgr, 0, sizeof(*mgr));
}

/* ================================================================
 * Internal: cache a parsed dict (or return the existing cached copy)
 * ================================================================ */

// returns NULL if no dict/materializer match is found
// Note: This performs a linear scan over all CNodes O(numCNodes) on every call,
// making fat-bundle loading O(numDicts × numCNodes). If this becomes a problem,
// consider a more clever solution.
static const ZL_MaterializerDesc2* CDictMgr_findMaterializer(
        const CDictMgr* mgr,
        ZL_DictID dictID,
        TransformType_e codecType)
{
    ZL_ASSERT_NN(mgr);
    const CNodes_manager* ctm = &mgr->nmgr->ctm;
    const ZL_IDType nbCNodes  = CTM_nbCNodes(ctm);
    for (ZL_IDType id = 0; id < nbCNodes; ++id) {
        CNodeID cnodeID    = { id };
        const CNode* cnode = CTM_getCNode(ctm, cnodeID);
        if (cnode == NULL)
            continue;
        if (cnode->nodetype != node_internalTransform)
            continue;
        if (cnode->publicIDtype != codecType)
            continue;
        if (ZL_UniqueID_eq(
                    &cnode->transformDesc.publicDesc.dictID.id, &dictID.id)
            && cnode->transformDesc.publicDesc.dictMat.materializeFn != NULL) {
            return &cnode->transformDesc.publicDesc.dictMat;
        }
    }
    return NULL;
}

static ZL_RESULT_OF(ZL_DictConstPtr) CDictMgr_cacheDict(
        CDictMgr* mgr,
        const ZL_ParsedDict* parsed,
        const ZL_MaterializerDesc2* matDesc)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_DictConstPtr, mgr->opCtx);
    ZL_ASSERT_NN(matDesc);
    ZL_ASSERT_NN(matDesc->materializeFn);
    CDictMgr_DictKey lookupKey = {
        .id      = parsed->dictID.id,
        .matDesc = *matDesc,
    };

    const CDictMgr_DictMap_Entry* existing =
            CDictMgr_DictMap_find(&mgr->dictsByID, &lookupKey);
    if (existing != NULL) {
        return ZL_WRAP_VALUE(existing->val);
    }

    ZL_Dict* dict = (ZL_Dict*)ALLOC_Arena_calloc(mgr->arena, sizeof(ZL_Dict));
    ZL_ERR_IF_NULL(dict, allocation);

    dict->dictID             = parsed->dictID;
    dict->materializingCodec = parsed->materializingCodec;
    dict->codecType          = parsed->codecType;
    dict->packedSize         = parsed->packedSize;

    // We free all memory in the scratch allocator, so don't
    // pass an arena that's not cleared.
    ZL_ASSERT_EQ(ALLOC_Arena_memUsed(mgr->scratchArena), 0);
    ZL_Materializer matCtx = {
        .persistentArena = mgr->arena,
        .scratchArena    = mgr->scratchArena,
        .opaquePtr       = matDesc->opaque.ptr,
        .opCtx           = mgr->opCtx,
    };

    ZL_RESULT_OF(ZL_VoidPtr)
    obj = matDesc->materializeFn(
            &matCtx, parsed->dictContent, parsed->contentSize);

    ALLOC_Arena_freeAll(mgr->scratchArena);
    ZL_ERR_IF_ERR(obj);
    dict->dictObj = ZL_RES_value(obj);

    CDictMgr_DictMap_Entry entry = { .key = lookupKey, .val = dict };
    CDictMgr_DictMap_Insert ins =
            CDictMgr_DictMap_insert(&mgr->dictsByID, &entry);
    ZL_ERR_IF(ins.badAlloc, allocation);
    return ZL_WRAP_VALUE(dict);
}

/* ================================================================
 * CDictMgr_loadFatBundle
 * ================================================================ */

ZL_RESULT_OF(ZL_DictBundleConstPtr)
CDictMgr_loadFatBundle(
        CDictMgr* mgr,
        const void* serializedFatBundle,
        size_t serializedFatBundleSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_DictBundleConstPtr, mgr->opCtx);
    if (mgr->bundle != NULL) {
        ZL_ERR(GENERIC, "CDictMgr already has a fat bundle loaded");
    }

    ZL_DictBundle* bundle = (ZL_DictBundle*)ALLOC_Arena_malloc(
            mgr->arena, sizeof(ZL_DictBundle));
    ZL_ERR_IF_NULL(bundle, allocation);

    ZL_Report infoResult = BundleInfo_parse(
            &bundle->info,
            serializedFatBundle,
            serializedFatBundleSize,
            mgr->arena);
    ZL_ERR_IF_ERR(infoResult);

    // If we have pre-declared a bundle ID, it must match
    if (ZL_UniqueID_isValid(&mgr->bundleID.id)) {
        ZL_ERR_IF_NOT(
                ZL_UniqueID_eq(&mgr->bundleID.id, &bundle->info.bundleID.id),
                dictNoRecord,
                "bundle ID mismatch");
    } else {
        ZL_ERR_IF_ERR(CDictMgr_setBundleID(mgr, &bundle->info.bundleID));
    }

    ZL_ERR_IF_NE(
            (int)bundle->info.isFatBundle,
            1,
            dict_corruption,
            "expected isFatBundle=1 for fat bundle");

    if (bundle->info.numDicts > 0) {
        bundle->dicts = (const ZL_Dict**)ALLOC_Arena_malloc(
                mgr->arena, bundle->info.numDicts * sizeof(const ZL_Dict*));
        ZL_ERR_IF_NULL(
                bundle->dicts,
                allocation,
                "failed to allocate dicts array for %zu dicts",
                bundle->info.numDicts);
    } else {
        bundle->dicts = NULL;
    }

    size_t totalConsumed = ZL_RES_value(infoResult);
    size_t remaining     = serializedFatBundleSize - totalConsumed;
    const unsigned char* p =
            (const unsigned char*)serializedFatBundle + totalConsumed;

    for (size_t i = 0; i < bundle->info.numDicts; i++) {
        ZL_RESULT_OF(ZL_DictConstPtr)
        dictRes = CDictMgr_loadDict(mgr, p, remaining);
        ZL_ERR_IF_ERR(dictRes);
        bundle->dicts[i] = ZL_RES_value(dictRes);
        ZL_ERR_IF_NOT(
                ZL_UniqueID_eq(
                        &bundle->dicts[i]->dictID.id,
                        &bundle->info.dictIDs[i].id),
                dict_corruption,
                "dict ID mismatch");

        size_t const dictWireSize = bundle->dicts[i]->packedSize;
        p += dictWireSize;
        remaining -= dictWireSize;
        totalConsumed += dictWireSize;
    }
    bundle->packedSize = totalConsumed;

    mgr->bundle = bundle;

    return ZL_RESULT_WRAP_VALUE(ZL_DictBundleConstPtr, mgr->bundle);
}

/* ================================================================
 * CDictMgr_loadDict
 * ================================================================ */

ZL_RESULT_OF(ZL_DictConstPtr)
CDictMgr_loadDict(CDictMgr* mgr, const void* serialBuffer, size_t bufferMaxSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_DictConstPtr, mgr->opCtx);
    ZL_RESULT_OF(ZL_ParsedDict)
    dictResult = Dict_parse(serialBuffer, bufferMaxSize);
    ZL_ERR_IF_ERR(dictResult);

    ZL_ParsedDict const parsed = ZL_RES_value(dictResult);
    ZL_ERR_IF_GT(
            parsed.packedSize,
            bufferMaxSize,
            dict_corruption,
            "dict packedSize exceeds remaining buffer");
    const ZL_MaterializerDesc2* matDesc =
            CDictMgr_findMaterializer(mgr, parsed.dictID, parsed.codecType);
    ZL_ERR_IF_NULL(
            matDesc, noValidMaterialization, "no materializer found for dict");

    ZL_RESULT_OF(ZL_DictConstPtr)
    dictRes = CDictMgr_cacheDict(mgr, &parsed, matDesc);
    ZL_ERR_IF_ERR(dictRes);

    const ZL_Dict* dict = ZL_RES_value(dictRes);

    return ZL_WRAP_VALUE(dict);
}

/* ================================================================
 * Accessors
 * ================================================================ */

const ZL_Dict* CDictMgr_findDict(
        const CDictMgr* mgr,
        const ZL_DictID* id,
        const ZL_MaterializerDesc2* matDesc)
{
    CDictMgr_DictKey lookupKey = {
        .id      = id->id,
        .matDesc = (matDesc != NULL) ? *matDesc : CDictMgr_zeroMatDesc(),
    };
    const CDictMgr_DictMap_Entry* entry =
            CDictMgr_DictMap_find(&mgr->dictsByID, &lookupKey);
    return (entry != NULL) ? entry->val : NULL;
}

ZL_Report CDictMgr_setBundleID(CDictMgr* mgr, const ZL_BundleID* id)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(mgr->opCtx);
    ZL_ERR_IF(
            ZL_UniqueID_isValid(&mgr->bundleID.id),
            dict_materialization,
            "Bundle ID already set");
    ZL_ERR_IF_NOT(
            ZL_UniqueID_isValid(&id->id), dictNoRecord, "Invalid bundle ID");
    mgr->bundleID = *id;
    return ZL_returnSuccess();
}

const ZL_BundleID* CDictMgr_getBundleID(const CDictMgr* mgr)
{
    return ZL_UniqueID_isValid(&mgr->bundleID.id) ? &mgr->bundleID : NULL;
}

/**
 * Store a raw MParam blob for later CBOR serialization. The blob data is
 * copied into the CDictMgr's arena.
 *
 * @param id   The MParam ID to associate with this blob.
 * @param data Raw serialized MParam blob (dict wire format).
 * @param size Size of the blob in bytes.
 */
static ZL_Report CDictMgr_storeMParamBlob(
        CDictMgr* mgr,
        ZL_MParamID id,
        const void* data,
        size_t size)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(mgr->opCtx);
    ZL_ERR_IF_NULL(data, GENERIC, "MParam blob data must not be null");
    ZL_ERR_IF_EQ(size, 0, GENERIC, "MParam blob size must be > 0");

    void* copy = ALLOC_Arena_malloc(mgr->arena, size);
    ZL_ERR_IF_NULL(copy, allocation);
    memcpy(copy, data, size);

    ZL_MParam val = {
        .mparamID = id,
        .content  = copy,
        .size     = size,
    };
    CDictMgr_MParamMap_Entry mapEntry = { .key = id, .val = val };
    CDictMgr_MParamMap_Insert ins =
            CDictMgr_MParamMap_insert(&mgr->mparamBlobs, &mapEntry);
    ZL_ERR_IF(ins.badAlloc, allocation, "Failed to insert MParam blob");

    return ZL_returnSuccess();
}

const ZL_MParam* CDictMgr_getMParam(const CDictMgr* mgr, ZL_MParamID id)
{
    const CDictMgr_MParamMap_Entry* entry =
            CDictMgr_MParamMap_find(&mgr->mparamBlobs, &id);
    return (entry != NULL) ? &entry->val : NULL;
}
