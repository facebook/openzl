// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/zstd/common_zstd.h"

#include <string.h>

#include "openzl/shared/mem.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_materializer.h"

#ifndef ZSTD_STATIC_LINKING_ONLY
#    define ZSTD_STATIC_LINKING_ONLY
#endif
#include <zstd.h> // ZSTD_customMem, ZSTD_createDDict_advanced

size_t ZL_TrainedZstdContent_packedSize(size_t rawDictSize)
{
    return ZL_TRAINED_ZSTD_CONTENT_HEADER_SIZE + rawDictSize;
}

size_t ZL_TrainedZstdContent_pack(
        void* dst,
        size_t dstCapacity,
        int32_t clevel,
        const void* rawDict,
        size_t rawDictSize)
{
    size_t const packedSize = ZL_TrainedZstdContent_packedSize(rawDictSize);
    if (dstCapacity < packedSize)
        return 0;
    uint8_t* const p = (uint8_t*)dst;
    ZL_writeLE32(p + 0, ZL_TRAINED_ZSTD_CONTENT_VERSION);
    ZL_writeLE32(p + 4, (uint32_t)clevel);
    if (rawDictSize > 0) {
        memcpy(p + ZL_TRAINED_ZSTD_CONTENT_HEADER_SIZE, rawDict, rawDictSize);
    }
    return packedSize;
}

bool ZL_TrainedZstdContent_parse(
        const void* src,
        size_t srcSize,
        ZL_TrainedZstdContentParsed* out)
{
    if (srcSize < ZL_TRAINED_ZSTD_CONTENT_HEADER_SIZE)
        return false;
    const uint8_t* const p = (const uint8_t*)src;
    uint32_t const version = ZL_readLE32(p + 0);
    if (version != ZL_TRAINED_ZSTD_CONTENT_VERSION)
        return false;
    out->clevel      = (int32_t)ZL_readLE32(p + 4);
    out->rawDict     = p + ZL_TRAINED_ZSTD_CONTENT_HEADER_SIZE;
    out->rawDictSize = srcSize - ZL_TRAINED_ZSTD_CONTENT_HEADER_SIZE;
    return true;
}

void* ZL_Zstd_materializerAlloc(void* opaque, size_t size)
{
    return ZL_Materializer_allocate((ZL_Materializer*)opaque, size);
}

void ZL_Zstd_materializerFree(void* opaque, void* address)
{
    // Arena-managed memory: freed when the owning compressor/dict store is
    // destroyed. Individual frees are no-ops.
    (void)opaque;
    (void)address;
}

static ZL_RESULT_OF(ZL_VoidPtr) DIZSTD_materializeDDict(
        ZL_Materializer* matCtx,
        const void* src,
        size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, NULL);
    ZL_TrainedZstdContentParsed parsed;
    ZL_ERR_IF_NOT(
            ZL_TrainedZstdContent_parse(src, srcSize, &parsed),
            dict_materialization,
            "Failed to parse trained zstd dict content");
    ZSTD_customMem const customMem = {
        .customAlloc = ZL_Zstd_materializerAlloc,
        .customFree  = ZL_Zstd_materializerFree,
        .opaque      = matCtx,
    };
    ZSTD_DDict* ddict = ZSTD_createDDict_advanced(
            parsed.rawDict,
            parsed.rawDictSize,
            ZSTD_dlm_byCopy,
            ZSTD_dct_auto,
            customMem);
    ZL_ERR_IF_NULL(ddict, dict_materialization, "ZSTD_createDDict failed");
    return ZL_RESULT_WRAP_VALUE(ZL_VoidPtr, (ZL_VoidPtr)ddict);
}

static void DIZSTD_dematerializeDDict(
        ZL_Materializer* matCtx,
        void* materialized)
{
    (void)matCtx;
    if (materialized != NULL) {
        ZSTD_freeDDict((ZSTD_DDict*)materialized);
    }
}

const ZL_MaterializerDesc2 ZL_Zstd_ddict_materializer = {
    .materializeFn   = DIZSTD_materializeDDict,
    .dematerializeFn = DIZSTD_dematerializeDDict,
};
