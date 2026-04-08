// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_CDICTMGR_H
#define OPENZL_COMPRESS_CDICTMGR_H

#include "openzl/common/allocation.h" // Arena
#include "openzl/common/map.h"        // ZL_DECLARE_CUSTOM_MAP_TYPE
#include "openzl/common/vector.h"     // VECTOR, DECLARE_VECTOR_TYPE
#include "openzl/compress/nodemgr.h"  // Nodes_manager
#include "openzl/dict/bundle.h" // ZL_DictBundleConstPtr, ZL_Dict, ZL_UniqueID
#include "openzl/dict/dict.h"   // ZL_Dict
#include "openzl/shared/portability.h" // ZL_BEGIN_C_DECLS
#include "openzl/zl_errors.h"          // ZL_Report
#include "openzl/zl_materializer.h"    // ZL_MaterializerDesc
#include "openzl/zl_opaque_types.h"    // ZL_DictID, ZL_BundleID

ZL_BEGIN_C_DECLS

typedef struct GraphsMgr_s GraphsMgr; // forward declaration

/* =========================================================================
 * CDictMgr — Compressor-internal dictionary manager
 * =========================================================================
 *
 * Stores materialized dicts, provides O(1) lookup by ZL_DictID, and owns the
 * compressor's bundleID.
 *
 * Dict caching: dicts are keyed by (ZL_DictID, ZL_MaterializerDesc). The
 * composite key ensures that the same raw dict loaded with different
 * materializers produces distinct cache entries. All memory is arena-backed
 * and freed in one shot on destroy.
 */

typedef const ZL_Dict* CDictMgr_DictPtr;

typedef struct {
    ZL_UniqueID id;
    ZL_MaterializerDesc2 matDesc;
} CDictMgr_DictKey;

size_t CDictMgr_DictMap_hash(const CDictMgr_DictKey* key);
bool CDictMgr_DictMap_eq(
        const CDictMgr_DictKey* lhs,
        const CDictMgr_DictKey* rhs);

ZL_DECLARE_CUSTOM_MAP_TYPE(
        CDictMgr_DictMap,
        CDictMgr_DictKey,
        CDictMgr_DictPtr);

typedef struct {
    ZL_BundleID bundleID;
    CDictMgr_DictMap dictsByID;
    ZL_DictBundle* bundle;

    // internal state
    Arena* arena;
    Arena* scratchArena; // scratch arena for materializations
    const Nodes_manager* nmgr;
    const GraphsMgr* gm;
    ZL_OperationContext* opCtx;
} CDictMgr;

/**
 * Initialize the CDictMgr. Creates a backing heap arena and an empty dict map.
 * Must be paired with CDictMgr_destroy().
 * @returns an error if the arena allocation fails.
 */
ZL_Report CDictMgr_init(
        CDictMgr* mgr,
        const Nodes_manager* nmgr,
        const GraphsMgr* gm,
        ZL_OperationContext* opCtx);

/**
 * Destroy the CDictMgr, freeing the dict map and all arena-backed memory.
 * Safe to call on a zero-initialized CDictMgr (no-op).
 */
void CDictMgr_destroy(CDictMgr* mgr);

/**
 * Parse a serialized fat bundle, cache each constituent dict, and set the
 * bundleID. This should be called *once* per CDictMgr, since the compressor can
 * only use one bundle at a time.
 *
 * TESTING ONLY -- If a dict with the same (ZL_DictID, materializer) was already
 * loaded (from a prior call to loadDict), the cached dict is reused.
 *
 * @returns the parsed ZL_DictBundle on success, or an error.
 */
ZL_RESULT_OF(ZL_DictBundleConstPtr)
CDictMgr_loadFatBundle(
        CDictMgr* mgr,
        const void* serializedFatBundle,
        size_t serializedFatBundleSize);

/**
 * Parse and cache a single serialized dict. If a dict with the same
 * (ZL_DictID, materializer) was already loaded, this is a no-op.
 *
 * @returns success, or an error if the blob is malformed or allocation fails.
 */
ZL_RESULT_OF(ZL_DictConstPtr)
CDictMgr_loadDict(
        CDictMgr* mgr,
        const void* serialBuffer,
        size_t bufferMaxSize);

/**
 * O(1) lookup of a previously loaded dict by its (ZL_DictID, materializer)
 * pair.
 * @param matDesc must match the materializer used when the dict was loaded.
 * Pass NULL if the dict was loaded without a materializer.
 * @returns the dict, or NULL if no matching entry has been loaded.
 */
const ZL_Dict* CDictMgr_findDict(
        const CDictMgr* mgr,
        const ZL_DictID* id,
        const ZL_MaterializerDesc2* matDesc);

/**
 * Should be called before CDictMgr_loadFatBundle() if the loaded bundle's ID is
 * expected to be validated against it.
 */
ZL_Report CDictMgr_setBundleID(CDictMgr* mgr, const ZL_BundleID* id);

/**
 * @returns the bundleID of the loaded fat bundle, or NULL if
 * no fat bundle has been loaded.
 */
const ZL_BundleID* CDictMgr_getBundleID(const CDictMgr* mgr);

ZL_END_C_DECLS

#endif // OPENZL_COMPRESS_CDICTMGR_H
