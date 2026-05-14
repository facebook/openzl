// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/allocation.h"
#include "openzl/common/assertion.h"
#include "openzl/common/map.h"
#include "openzl/dict/dict_constants.h"

#include <string.h>

#include "openzl/zl_dictloader.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_unique_id.h"

/* ================================================================
 * Map types for bundle and dict caches
 * ================================================================ */

// Map: ZL_UniqueID -> const ZL_DictBundle*
typedef const ZL_DictBundle* FBDLBundlePtr;
static size_t FBDLBundleMap_hash(const ZL_UniqueID* key)
{
    return ZL_UniqueID_hash(key);
}
static bool FBDLBundleMap_eq(const ZL_UniqueID* lhs, const ZL_UniqueID* rhs)
{
    return ZL_UniqueID_eq(lhs, rhs);
}
ZL_DECLARE_CUSTOM_MAP_TYPE(FBDLBundleMap, ZL_UniqueID, FBDLBundlePtr);

// Map: ZL_UniqueID -> const ZL_Dict*
typedef const ZL_Dict* FBDLDictPtr;
static size_t FBDLDictMap_hash(const ZL_UniqueID* key)
{
    return ZL_UniqueID_hash(key);
}
static bool FBDLDictMap_eq(const ZL_UniqueID* lhs, const ZL_UniqueID* rhs)
{
    return ZL_UniqueID_eq(lhs, rhs);
}
ZL_DECLARE_CUSTOM_MAP_TYPE(FBDLDictMap, ZL_UniqueID, FBDLDictPtr);

/* ================================================================
 * ZL_FatBundleDictLoader struct
 * ================================================================ */

struct ZL_FatBundleDictLoader_s {
    ZL_DictLoader* baseLoader;
    Arena* persistentArena;
    FBDLBundleMap bundleMap;
    FBDLDictMap dictMap;
};

/* ================================================================
 * Internal fetchDictBundle implementation
 * ================================================================ */

static ZL_RESULT_OF(ZL_DictBundleConstPtr)
        fbdl_fetchDictBundle(ZL_DictLoader* loader, const ZL_BundleID* id)
                ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE(ZL_DictBundleConstPtr, NULL);
    ZL_ERR_IF_NULL(loader, GENERIC);
    ZL_ERR_IF_NULL(id, GENERIC);

    ZL_FatBundleDictLoader* fbdl =
            (ZL_FatBundleDictLoader*)ZL_DictLoader_getOpaque(loader);
    ZL_ERR_IF_NULL(fbdl, GENERIC);

    const FBDLBundleMap_Entry* entry =
            FBDLBundleMap_find(&fbdl->bundleMap, &id->id);
    ZL_ERR_IF_NULL(
            entry, dictNoRecord, "no bundle loaded for the requested bundleID");

    return ZL_WRAP_VALUE(entry->val);
}

/* ================================================================
 * Public API
 * ================================================================ */

ZL_FatBundleDictLoader* ZL_FatBundleDictLoader_create(void)
{
    Arena* persistentArena = ALLOC_HeapArena_create();
    if (persistentArena == NULL) {
        return NULL;
    }

    ZL_FatBundleDictLoader* fbdl = (ZL_FatBundleDictLoader*)ALLOC_Arena_calloc(
            persistentArena, sizeof(ZL_FatBundleDictLoader));
    if (fbdl == NULL) {
        ALLOC_Arena_freeArena(persistentArena);
        return NULL;
    }
    fbdl->persistentArena = persistentArena;

    // Create the internal base loader with our fetchDictBundle vtable
    ZL_DictLoaderDesc desc = {
        .opaque.ptr      = fbdl,
        .fetchDictBundle = fbdl_fetchDictBundle,
    };
    fbdl->baseLoader = ZL_DictLoader_create(&desc);
    if (fbdl->baseLoader == NULL) {
        ALLOC_Arena_freeArena(persistentArena);
        return NULL;
    }

    fbdl->bundleMap = FBDLBundleMap_createInArena(
            persistentArena, ZL_MAX_BUNDLES_PER_FATBUNDLE_LOADER);
    fbdl->dictMap =
            FBDLDictMap_createInArena(persistentArena, ZL_MAX_DICTS_PER_BUNDLE);

    return fbdl;
}

void ZL_FatBundleDictLoader_free(ZL_FatBundleDictLoader* loader)
{
    if (loader == NULL) {
        return;
    }

    // Dematerialize all dicts
    FBDLDictMap_Iter iter = FBDLDictMap_iter(&loader->dictMap);
    const FBDLDictMap_Entry* entry;
    while ((entry = FBDLDictMap_Iter_next(&iter)) != NULL) {
        const ZL_Dict* dict = entry->val;
        if (dict != NULL && dict->dictObj != NULL) {
            ZL_DictLoader_dematerialize(
                    loader->baseLoader,
                    dict->materializingCodec,
                    dict->isCustomCodec,
                    dict->dictObj);
        }
    }

    FBDLDictMap_destroy(&loader->dictMap);
    FBDLBundleMap_destroy(&loader->bundleMap);

    ZL_DictLoader_free(loader->baseLoader);

    ALLOC_Arena_freeArena(loader->persistentArena);
}

ZL_Report ZL_FatBundleDictLoader_loadFatBundle(
        ZL_FatBundleDictLoader* loader,
        const void* fatBundle,
        size_t fatBundleSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_NULL(loader, GENERIC);
    ZL_ERR_IF_NULL(fatBundle, GENERIC);

    // Parse bundle info header
    ZL_RESULT_OF(ZL_BundleInfo)
    infoResult = ZL_BundleInfo_parse(fatBundle, fatBundleSize);
    ZL_ERR_IF_ERR(infoResult);
    ZL_BundleInfo info = ZL_RES_value(infoResult);

    ZL_ERR_IF_NE(
            (int)info.isFatBundle,
            1,
            dict_corruption,
            "expected isFatBundle=1 for fat bundle");

    // Check for duplicate bundle
    const FBDLBundleMap_Entry* existingBundle =
            FBDLBundleMap_find(&loader->bundleMap, &info.bundleID.id);
    if (existingBundle != NULL) {
        return ZL_returnSuccess(); // already loaded
    }

    // Allocate bundle
    ZL_DictBundle* bundle = (ZL_DictBundle*)ALLOC_Arena_malloc(
            loader->persistentArena, sizeof(ZL_DictBundle));
    ZL_ERR_IF_NULL(bundle, allocation);

    // Copy dictIDs into the persistent arena since the parsed view points
    // into the (potentially temporary) fatBundle input buffer.
    if (info.numDicts > 0) {
        ZL_DictID* arenaDictIDs = (ZL_DictID*)ALLOC_Arena_malloc(
                loader->persistentArena, info.numDicts * sizeof(ZL_DictID));
        ZL_ERR_IF_NULL(arenaDictIDs, allocation);
        memcpy(arenaDictIDs, info.dictIDs, info.numDicts * sizeof(ZL_DictID));
        info.dictIDs = arenaDictIDs;
    }
    bundle->info = info;

    // Allocate the dicts pointer array
    if (info.numDicts > 0) {
        bundle->dicts = (const ZL_Dict**)ALLOC_Arena_malloc(
                loader->persistentArena,
                bundle->info.numDicts * sizeof(const ZL_Dict*));
        ZL_ERR_IF_NULL(
                bundle->dicts,
                allocation,
                "failed to allocate dicts array for %zu dicts",
                bundle->info.numDicts);
    } else {
        bundle->dicts = NULL;
    }

    // Parse and materialize each dict
    size_t totalConsumed   = info.packedSize;
    size_t remaining       = fatBundleSize - totalConsumed;
    const unsigned char* p = (const unsigned char*)fatBundle + totalConsumed;

    for (size_t i = 0; i < bundle->info.numDicts; i++) {
        ZL_RESULT_OF(ZL_ParsedDict) dictResult = ZL_Dict_parse(p, remaining);
        ZL_ERR_IF_ERR(dictResult);

        ZL_ParsedDict const parsed = ZL_RES_value(dictResult);

        // Validate that this dict's ID matches the declared ID in the info
        ZL_ERR_IF_NOT(
                ZL_UniqueID_eq(&parsed.dictID.id, &bundle->info.dictIDs[i].id),
                dict_corruption,
                "fat bundle dict[%zu] ID does not match declared dictID",
                i);

        // Check dict dedup cache
        const FBDLDictMap_Entry* existingDict =
                FBDLDictMap_find(&loader->dictMap, &parsed.dictID.id);
        if (existingDict != NULL) {
            ZL_ERR_IF_NOT(
                    ZL_UniqueID_eq(
                            &parsed.contentHash,
                            &existingDict->val->contentHash),
                    dict_corruption,
                    "trying to load a dict with an ID already in use",
                    i);
            bundle->dicts[i] = existingDict->val;
            p += parsed.packedSize;
            remaining -= parsed.packedSize;
            totalConsumed += parsed.packedSize;
            continue;
        }
        // Materialize
        ZL_RESULT_OF(ZL_VoidPtr)
        obj = ZL_DictLoader_materialize(
                loader->baseLoader,
                parsed.materializingCodec,
                parsed.isCustomCodec,
                parsed.dictContent,
                parsed.contentSize);
        ZL_ERR_IF_ERR(obj);

        // Create ZL_Dict
        ZL_Dict* dict = (ZL_Dict*)ALLOC_Arena_calloc(
                loader->persistentArena, sizeof(ZL_Dict));
        if (dict == NULL) {
            ZL_DictLoader_dematerialize(
                    loader->baseLoader,
                    parsed.materializingCodec,
                    parsed.isCustomCodec,
                    ZL_RES_value(obj));
            ZL_ERR(allocation);
        }
        dict->dictID             = parsed.dictID;
        dict->contentHash        = parsed.contentHash;
        dict->materializingCodec = parsed.materializingCodec;
        dict->isCustomCodec      = parsed.isCustomCodec;
        dict->packedSize         = parsed.packedSize;
        dict->dictObj            = ZL_RES_value(obj);

        bundle->dicts[i] = dict;

        // Insert into dedup cache
        FBDLDictMap_Entry dictEntry = {
            .key = parsed.dictID.id,
            .val = dict,
        };
        FBDLDictMap_Insert ins =
                FBDLDictMap_insert(&loader->dictMap, &dictEntry);
        if (ins.badAlloc) {
            ZL_DictLoader_dematerialize(
                    loader->baseLoader,
                    parsed.materializingCodec,
                    parsed.isCustomCodec,
                    ZL_RES_value(obj));
            ZL_ERR(allocation);
        }

        p += parsed.packedSize;
        remaining -= parsed.packedSize;
        totalConsumed += parsed.packedSize;
    }
    bundle->packedSize = totalConsumed;

    // Cache bundle by ID
    FBDLBundleMap_Entry bundleEntry = {
        .key = info.bundleID.id,
        .val = bundle,
    };
    FBDLBundleMap_Insert ins =
            FBDLBundleMap_insert(&loader->bundleMap, &bundleEntry);
    ZL_ERR_IF(ins.badAlloc, allocation);

    return ZL_returnSuccess();
}

ZL_DictLoader* ZL_FatBundleDictLoader_getDictLoader(
        ZL_FatBundleDictLoader* loader)
{
    ZL_ASSERT_NN(loader);
    return loader->baseLoader;
}
