// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_ZL_DICT_H
#define OPENZL_ZL_DICT_H

#include <stdbool.h>
#include <stddef.h>

#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

// ============================================================================
// ZL_Dict - A single dictionary
// ============================================================================

typedef struct {
    ZL_DictID dictID;
    ZL_UniqueID contentHash; // SHA-256 of raw dict content
    ZL_IDType materializingCodec;
    bool isCustomCodec;
    void* dictObj;
    size_t packedSize;
} ZL_Dict;

typedef const ZL_Dict* ZL_DictConstPtr;
ZL_RESULT_DECLARE_TYPE(ZL_DictConstPtr);

// ============================================================================
// ZL_ParsedDict - Parsed (non-owning) view of a serialized dict
// ============================================================================

typedef struct {
    ZL_DictID dictID;
    ZL_UniqueID contentHash; // SHA-256 of raw dict content
    ZL_IDType materializingCodec;
    bool isCustomCodec;
    const void* dictContent;
    size_t contentSize;
    size_t packedSize;
} ZL_ParsedDict;

ZL_RESULT_DECLARE_TYPE(ZL_ParsedDict);

/// Warning: The produced ZL_ParsedDict is non-owning. The dictContent field is
/// just a pointer to somewhere in the @p buf .
ZL_RESULT_OF(ZL_ParsedDict) ZL_Dict_parse(const void* buf, size_t size);

// ============================================================================
// ZL_BundleInfo - Metadata about the bundle and associated dicts
// ============================================================================

typedef struct {
    ZL_BundleID bundleID;
    bool isFatBundle;
    size_t numDicts;
    const ZL_DictID* dictIDs;
    size_t packedSize;
} ZL_BundleInfo;

typedef const ZL_BundleInfo* ZL_BundleInfoConstPtr;
ZL_RESULT_DECLARE_TYPE(ZL_BundleInfoConstPtr);
ZL_RESULT_DECLARE_TYPE(ZL_BundleInfo);

/// Warning: The produced ZL_BundleInfo is non-owning. The dictIDs field is
/// just a pointer to somewhere in the @p buf . The caller must ensure that
/// @p buf outlives any use of the returned struct's dictIDs, or otherwise copy
/// the buffer.
ZL_RESULT_OF(ZL_BundleInfo) ZL_BundleInfo_parse(const void* buf, size_t size);

// ============================================================================
// ZL_DictBundle - A bundle of dictionaries
// ============================================================================

/**
 * A non-owning "view" of a bundle. The backing memory must be managed by an
 * external allocator.
 */
typedef struct {
    ZL_BundleInfo info;
    size_t packedSize;
    const ZL_Dict** dicts;
} ZL_DictBundle;

typedef const ZL_DictBundle* ZL_DictBundleConstPtr;
ZL_RESULT_DECLARE_TYPE(ZL_DictBundleConstPtr);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_ZL_DICT_H
