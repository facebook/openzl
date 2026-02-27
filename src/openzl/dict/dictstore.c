// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dict/dictstore.h"

#include "openzl/common/allocation.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/limits.h"
#include "openzl/dict/dict.h"
#include "openzl/dict/dictbundle.h"
#include "openzl/dict/sha256.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h" // ZL_IDType

ZL_DictStore* ZL_DictStore_create(void)
{
    ZL_DictStore* store = ZL_malloc(sizeof(ZL_DictStore));
    if (store == NULL) {
        return NULL;
    }

    store->arena = ALLOC_HeapArena_create();

    store->bundles =
            ZL_BundleMap_createInArena(store->arena, ZL_DICTIONARY_LIMIT);
    store->dicts = ZL_DictMap_createInArena(store->arena, ZL_DICTIONARY_LIMIT);

    return store;
}

void ZL_DictStore_reset(ZL_DictStore* store)
{
    if (store == NULL) {
        return;
    }

    // Dematerialize all dicts
    ZL_DictMap_Iter dictIter = ZL_DictMap_iter(&store->dicts);
    ZL_DictMap_Entry const* dictEntry;
    while ((dictEntry = ZL_DictMap_Iter_next(&dictIter)) != NULL) {
        ZL_Dict* dict = dictEntry->val;
        dict->dematerializeFn(dict->dictObj);
    }

    ALLOC_Arena_freeAll(store->arena);

    // Recreate the maps with fresh allocations
    store->bundles =
            ZL_BundleMap_createInArena(store->arena, ZL_DICTIONARY_LIMIT);
    store->dicts = ZL_DictMap_createInArena(store->arena, ZL_DICTIONARY_LIMIT);
}

void ZL_DictStore_free(ZL_DictStore* store)
{
    if (store == NULL) {
        return;
    }

    // Dematerialize all dicts
    ZL_DictMap_Iter dictIter = ZL_DictMap_iter(&store->dicts);
    ZL_DictMap_Entry const* dictEntry;
    while ((dictEntry = ZL_DictMap_Iter_next(&dictIter)) != NULL) {
        ZL_Dict* dict = dictEntry->val;
        dict->dematerializeFn(dict->dictObj);
    }
    ALLOC_Arena_freeAll(store->arena);
    ZL_free(store);
}

// ==================== ZL_Dict manip ===================
/**
 * Materialize and store a dict within the dict store. Requires either a
 * compressor or dctx to do the materialization
 */
ZL_RESULT_OF(ZL_SHA256)
ZL_DictStore_matererializeDict(
        ZL_DictStore* store,
        ZL_Compressor* compressor,
        const void* rawDict,
        size_t rawDictSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SHA256, NULL);

    ZL_ASSERT_NN(store, "store cannot be NULL");
    ZL_ASSERT_NN(compressor, "compressor cannot be NULL");
    ZL_ASSERT_NN(rawDict, "rawDict cannot be NULL");
    ZL_ASSERT_GT(rawDictSize, 0, "rawDictSize cannot be 0");

    ZL_RESULT_OF(ZL_ParsedDict)
    parsedRes = ZL_Dict_parse(NULL, rawDict, rawDictSize);
    ZL_ERR_IF_ERR(parsedRes);
    ZL_ParsedDict parsed = ZL_RES_value(parsedRes);

    // Check if dict already exists
    if (ZL_DictMap_find(&store->dicts, &parsed.hash)) {
        return ZL_WRAP_VALUE(parsed.hash);
    }

    // Allocate and initialize ZL_Dict structure
    ZL_Dict* dict = ALLOC_Arena_malloc(store->arena, sizeof(ZL_Dict));
    ZL_ERR_IF_NULL(dict, allocation, "Failed to allocate ZL_Dict");

    dict->codecId = parsed.codecId;
    dict->hash    = parsed.hash;
    // TODO(csv): next diff, call ZL_Compressor_getMaterializer(ZL_SHA256)
    // 3. Double-check that the compressor has a record of mapping that codec to
    // the SHA of the dict
    // 4. Materialize using the codec's materialization function and add it to
    // the dict store
    dict->dematerializeFn = NULL;
    dict->dictObj         = NULL;

    // Add to store
    ZL_DictMap_Entry entry         = { .key = parsed.hash, .val = dict };
    ZL_DictMap_Insert insertResult = ZL_DictMap_insert(&store->dicts, &entry);
    if (insertResult.badAlloc) {
        ALLOC_Arena_free(store->arena, dict);
        ZL_ERR(allocation, "Failed to insert dict into store map");
    }

    return ZL_WRAP_VALUE(parsed.hash);
}

ZL_RESULT_OF(ZL_SHA256)
ZL_DictStore_materializeDict(
        ZL_DictStore* store,
        ZL_DCtx* dctx,
        const void* rawDict,
        size_t rawDictSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SHA256, NULL);

    ZL_ASSERT_NN(store, "store cannot be NULL");
    ZL_ASSERT_NN(dctx, "compressor cannot be NULL");
    ZL_ASSERT_NN(rawDict, "rawDict cannot be NULL");

    ZL_RESULT_OF(ZL_ParsedDict)
    parsedRes = ZL_Dict_parse(NULL, rawDict, rawDictSize);
    ZL_ERR_IF_ERR(parsedRes);
    ZL_ParsedDict parsed = ZL_RES_value(parsedRes);

    // Check if dict already exists
    if (ZL_DictStore_hasDict(store, parsed.hash)) {
        return ZL_WRAP_VALUE(parsed.hash);
    }

    // Allocate and initialize ZL_Dict structure
    ZL_Dict* dict = ALLOC_Arena_malloc(store->arena, sizeof(ZL_Dict));
    ZL_ERR_IF_NULL(dict, allocation, "Failed to allocate ZL_Dict");

    dict->codecId = parsed.codecId;
    dict->hash    = parsed.hash;
    // TODO(csv): next diff, call ZL_Compressor_getMaterializer(ZL_SHA256)
    // 3. Double-check that the compressor has a record of mapping that codec to
    // the SHA of the dict
    // 4. Materialize using the codec's materialization function and add it to
    // the dict store
    dict->dematerializeFn = NULL;
    dict->dictObj         = NULL;

    // Add to store
    ZL_DictMap_Entry entry         = { .key = parsed.hash, .val = dict };
    ZL_DictMap_Insert insertResult = ZL_DictMap_insert(&store->dicts, &entry);
    if (insertResult.badAlloc) {
        dict->dematerializeFn(dict->dictObj);
        ALLOC_Arena_free(store->arena, dict);
        ZL_ERR(allocation, "Failed to insert dict into store map");
    }

    return ZL_WRAP_VALUE(parsed.hash);
}

bool ZL_DictStore_hasDict(ZL_DictStore* store, ZL_SHA256 dictID)
{
    if (store == NULL) {
        return false;
    }
    return ZL_DictMap_find(&store->dicts, &dictID) != NULL;
}

// ================= ZL_DictBundle manip ================

ZL_RESULT_OF(ZL_SHA256)
ZL_DictStore_materializeBundle(
        ZL_DictStore* store,
        void* rawBundle,
        size_t rawBundleSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SHA256, NULL);

    ZL_ASSERT_NN(store, "store cannot be NULL");
    ZL_ASSERT_NN(rawBundle, "rawBundle cannot be NULL");

    ZL_RESULT_OF(ZL_DictBundlePtr)
    bundleRes = DictBundle_create(NULL, store->arena, rawBundle, rawBundleSize);
    ZL_ERR_IF_ERR(bundleRes);
    ZL_DictBundle* bundle = ZL_RES_value(bundleRes);

    // Check if bundle already exists
    if (ZL_DictStore_hasBundle(store, bundle->bundleId)) {
        DictBundle_free(store->arena, bundle);
        return ZL_WRAP_VALUE(bundle->bundleId);
    }

    // Insert the bundle into the store's map
    ZL_BundleMap_Entry entry = { .key = bundle->bundleId, .val = bundle };
    ZL_BundleMap_Insert insertResult =
            ZL_BundleMap_insert(&store->bundles, &entry);
    if (insertResult.badAlloc) {
        DictBundle_free(store->arena, bundle);
        ZL_ERR(allocation, "Failed to insert bundle into store map");
    }
    return ZL_WRAP_VALUE(bundle->bundleId);
}

bool ZL_DictStore_hasBundle(ZL_DictStore* store, ZL_SHA256 bundleID)
{
    if (store == NULL) {
        return false;
    }
    return ZL_BundleMap_find(&store->bundles, &bundleID) != NULL;
}

ZL_DictBundle* ZL_DictStore_getBundle(ZL_DictStore* store, ZL_SHA256 bundleID)
{
    if (store == NULL) {
        return NULL;
    }
    const ZL_BundleMap_Entry* entry =
            ZL_BundleMap_find(&store->bundles, &bundleID);
    return entry != NULL ? entry->val : NULL;
}

bool ZL_DictStore_bundleIsComplete(ZL_DictStore* store, ZL_SHA256 bundleID)
{
    if (store == NULL) {
        return false;
    }

    ZL_DictBundle* bundle = ZL_DictStore_getBundle(store, bundleID);
    if (bundle == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < bundle->numDicts; i++) {
        if (!ZL_DictStore_hasDict(store, bundle->dicts[i])) {
            return false;
        }
    }
    return true;
}
