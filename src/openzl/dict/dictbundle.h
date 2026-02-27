// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_DICT_DICTBUNDLE_H
#define OPENZL_DICT_DICTBUNDLE_H

#include <stddef.h> // size_t

#include "openzl/common/allocation.h"
#include "openzl/common/errors_internal.h"
#include "openzl/dict/sha256.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    ZL_SHA256 bundleId;
    bool compressionOnly;
    size_t numDicts;
    ZL_SHA256* dicts;
} ZL_DictBundle;

typedef ZL_DictBundle* ZL_DictBundlePtr;
ZL_RESULT_DECLARE_TYPE(ZL_DictBundlePtr);

size_t ZL_DictBundle_numDicts(const ZL_DictBundle* bundle);
ZL_SHA256* ZL_DictBundle_dictHashes(const ZL_DictBundle* bundle);

/**
 * Parse and allocate a bundle from the raw blob. Does safety checks.
 */
ZL_RESULT_OF(ZL_DictBundlePtr)
DictBundle_create(
        ZL_OperationContext* opctx,
        Arena* arena,
        const void* dictBlob,
        const size_t blobSize);

void DictBundle_free(Arena* arena, ZL_DictBundle* bundle);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_DICT_DICTBUNDLE_H
