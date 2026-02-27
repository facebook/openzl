// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_DICT_DICTSTORE_H
#define OPENZL_DICT_DICTSTORE_H

#include "openzl/common/allocation.h"
#include "openzl/common/map.h"
#include "openzl/dict/dict.h"
#include "openzl/dict/dictbundle.h"
#include "openzl/dict/sha256.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h" // ZL_NodeID

#if defined(__cplusplus)
extern "C" {
#endif

ZL_DECLARE_PREDEF_MAP_TYPE(ZL_BundleMap, ZL_SHA256, ZL_DictBundle*);
ZL_DECLARE_PREDEF_MAP_TYPE(ZL_DictMap, ZL_SHA256, ZL_Dict*);

typedef struct {
    Arena* arena;
    ZL_BundleMap bundles;
    ZL_DictMap dicts;
} ZL_DictStore;

/**
 * Create a new ZL_DictStore with an arena.
 */
ZL_DictStore* ZL_DictStore_create(void);

/**
 * Clears the arena and frees *all* dicts using the dematerialization functions.
 */
void ZL_DictStore_reset(ZL_DictStore* store);
// also kills the struct
void ZL_DictStore_free(ZL_DictStore* store);

// ==================== ZL_Dict manip ===================

/**
 * Add an already-materialized dict to the store.
 * Helper function for unit testing.
 */
ZL_RESULT_OF(ZL_SHA256)
DictStore_addDict(ZL_DictStore* store, ZL_Dict* dict);

/**
 * Lookup a dict by its hash.
 * @returns Pointer to the dict if found, NULL otherwise.
 */
ZL_Dict* DictStore_getDict(ZL_DictStore* store, ZL_SHA256 dictID);

bool ZL_DictStore_hasDict(ZL_DictStore* store, ZL_SHA256 dictID);

/**
 * Materialize and store a dict within the dict store. Requires either a
 * compressor or dctx to do the materialization
 */
ZL_RESULT_OF(ZL_SHA256)
ZL_DictStore_matererializeDict(
        ZL_DictStore* store,
        ZL_Compressor* compressor,
        const void* rawDict,
        size_t rawDictSize);

ZL_RESULT_OF(ZL_SHA256)
ZL_DictStore_materializeDict(
        ZL_DictStore* store,
        ZL_DCtx* dctx,
        const void* rawDict,
        size_t rawDictSize);

// ================= ZL_DictBundle manip ================

/**
 * Add an already-materialized bundle to the store.
 * Helper function for unit testing.
 */
ZL_RESULT_OF(ZL_SHA256)
DictStore_addBundle(ZL_DictStore* store, ZL_DictBundle* bundle);

/**
 * Materialize a bundle and store it in the dict store
 */
ZL_RESULT_OF(ZL_SHA256)
ZL_DictStore_materializeBundle(
        ZL_DictStore* store,
        void* rawBundle,
        size_t rawBundleSize);

// map lookup
bool ZL_DictStore_hasBundle(ZL_DictStore* store, ZL_SHA256 bundleID);

// return NULL if not there
ZL_DictBundle* ZL_DictStore_getBundle(ZL_DictStore* store, ZL_SHA256 bundleID);

/**
 * Whether the store contains all the dicts required for the bundle with ID @p
 * bundleID
 */
bool ZL_DictStore_bundleIsComplete(ZL_DictStore* store, ZL_SHA256 bundleID);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_DICT_DICTSTORE_H
