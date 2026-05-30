// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_ZSTD_COMMON_ZSTD_H
#define ZSTRONG_CODECS_ZSTD_COMMON_ZSTD_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/shared/portability.h"
#include "openzl/zl_materializer.h"

ZL_BEGIN_C_DECLS

/**
 * Serialized dict content format for zstd dictionaries.
 *
 * This format wraps raw zstd dict bytes with compression level metadata.
 * It is stored as the "dict content" inside the generic ZL_Dict wire format
 * and is opaque to the generic dict packing layer. Only the zstd CDict/DDict
 * materializers interpret it.
 *
 * Binary layout (little-endian):
 *   [4 bytes] version = format version (uint32 LE)
 *   [4 bytes] clevel  = compression level (int32 LE)
 *   [N bytes] raw zstd dictionary content
 */

#define ZL_TRAINED_ZSTD_CONTENT_VERSION 1
#define ZL_TRAINED_ZSTD_CONTENT_HEADER_SIZE 8

typedef struct {
    int32_t clevel;
    const void* rawDict;
    size_t rawDictSize;
} ZL_TrainedZstdContentParsed;

size_t ZL_TrainedZstdContent_packedSize(size_t rawDictSize);

/**
 * Pack a trained zstd dict content envelope.
 * @returns packed size on success, 0 on failure (insufficient capacity).
 */
size_t ZL_TrainedZstdContent_pack(
        void* dst,
        size_t dstCapacity,
        int32_t clevel,
        const void* rawDict,
        size_t rawDictSize);

/**
 * Parse a trained zstd dict content envelope.
 * @returns true on success, false on failure (insufficient size or bad
 * version).
 */
bool ZL_TrainedZstdContent_parse(
        const void* src,
        size_t srcSize,
        ZL_TrainedZstdContentParsed* out);

// TODO(T271643692): This is a convenient way of injecting arena allocation
// without disturbing the public API. Work needs to be done to allow the MC
// allocator to be used here.
/**
 * Custom allocator hooks for plugging a @ref ZL_Materializer's arena into
 * zstd's `ZSTD_customMem` callbacks. Pass the `ZL_Materializer*` as the
 * `opaque` field of `ZSTD_customMem`. The free function is a no-op since
 * arena memory is reclaimed at compressor teardown.
 */
void* ZL_Zstd_materializerAlloc(void* opaque, size_t size);
void ZL_Zstd_materializerFree(void* opaque, void* address);

/// DDict materializer for decompression-side zstd dict support.
/// Defined in common_zstd.c, registered in DictLoader.
extern const ZL_MaterializerDesc2 ZL_Zstd_ddict_materializer;

ZL_END_C_DECLS

#endif // ZSTRONG_CODECS_ZSTD_COMMON_ZSTD_H
