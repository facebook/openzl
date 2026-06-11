// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_DICT_DICTLOADER_H
#define OPENZL_DICT_DICTLOADER_H

#include "openzl/common/allocation.h"  // Arena
#include "openzl/common/map.h"         // ZL_DECLARE_CUSTOM_MAP_TYPE
#include "openzl/common/wire_format.h" // PublicTransformInfo
#include "openzl/zl_dictloader.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Map: PublicTransformInfo -> ZL_MaterializerDesc2
size_t DL_MatMap_hash(const PublicTransformInfo* key);
bool DL_MatMap_eq(
        const PublicTransformInfo* lhs,
        const PublicTransformInfo* rhs);
ZL_DECLARE_CUSTOM_MAP_TYPE(
        DL_MatMap,
        PublicTransformInfo,
        ZL_MaterializerDesc2);

struct ZL_DictLoader_s {
    ZL_DictLoaderDesc desc;
    DL_MatMap materializers;
    Arena* persistentArena;
    Arena* scratchArena;
};

/**
 * Registers materializers for all standard (built-in) codecs that support
 * dictionaries (e.g. zstd). ZL_DictLoader_create calls this once after creating
 * a ZL_DictLoader.
 */
ZL_Report DictLoader_registerStandardMaterializers(ZL_DictLoader* loader);

/**
 * @returns the ZL_MaterializerDesc2 registered with @p codecID or NULL if no
 * materializer has been registered with that ID.
 */
const ZL_MaterializerDesc2* DictLoader_getMaterializer(
        const ZL_DictLoader* loader,
        ZL_IDType codecID,
        bool isCustomCodec);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_DICT_DICTLOADER_H
