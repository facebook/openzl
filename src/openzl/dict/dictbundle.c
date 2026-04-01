// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dict/dictbundle.h"

#include <stdint.h>
#include <string.h>

#include "openzl/common/allocation.h"

const uint32_t ZL_DICTBUNDLE_COMP_MAGIC =
        0x5A4C4243; // "ZLBC" (compression-only)
const uint32_t ZL_DICTBUNDLE_BIDI_MAGIC = 0x5A4C4249; // "ZLBI" (bidirectional)

// ============================================================================
// ZL_DictBundle Implementation
// ============================================================================

ZL_RESULT_OF(ZL_DictBundlePtr)
DictBundle_create(
        ZL_OperationContext* opctx,
        Arena* arena,
        const void* dictBlob,
        const size_t blobSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_DictBundlePtr, opctx);
    ZL_ASSERT_NN(arena);
    ZL_ASSERT_NN(dictBlob);

    ZL_ERR_IF_LT(
            blobSize,
            sizeof(uint32_t) * 2 + sizeof(ZL_SHA256),
            dict_materialization,
            "DictBundle blob too small for header");

    const char* ptr = (const char*)dictBlob;

    uint32_t magic;
    memcpy(&magic, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    ZL_DictBundle* bundle = ALLOC_Arena_malloc(arena, sizeof(ZL_DictBundle));
    ZL_ERR_IF_NULL(bundle, allocation, "Failed to allocate ZL_DictBundle");
    if (magic == ZL_DICTBUNDLE_COMP_MAGIC) {
        bundle->compressionOnly = true;
    } else if (magic == ZL_DICTBUNDLE_BIDI_MAGIC) {
        bundle->compressionOnly = false;
    } else {
        ZL_ERR(dict_corruption, "Invalid dict bundle magic");
    }

    memcpy(&bundle->bundleId, ptr, sizeof(ZL_SHA256));
    ptr += sizeof(ZL_SHA256);

    memcpy(&bundle->numDicts, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    ZL_ERR_IF_NE(
            blobSize,
            sizeof(uint32_t) * 2 + sizeof(ZL_SHA256)
                    + bundle->numDicts * sizeof(ZL_SHA256),
            dict_corruption,
            "DictBundle blob size mismatch");

    bundle->dicts =
            ALLOC_Arena_malloc(arena, sizeof(ZL_SHA256) * bundle->numDicts);
    ZL_ERR_IF_NULL(
            bundle->dicts,
            allocation,
            "Failed to allocate ZL_DictBundle dicts");
    memcpy(bundle->dicts, ptr, sizeof(ZL_SHA256) * bundle->numDicts);

    return ZL_WRAP_VALUE(bundle);
}

void DictBundle_free(Arena* arena, ZL_DictBundle* bundle)
{
    if (bundle == NULL) {
        return;
    }
    ALLOC_Arena_free(arena, bundle->dicts);
    ALLOC_Arena_free(arena, bundle);
}
