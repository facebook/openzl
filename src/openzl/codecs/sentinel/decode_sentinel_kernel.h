// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_SENTINEL_DECODE_SENTINEL_KERNEL_H
#define OPENZL_CODECS_SENTINEL_DECODE_SENTINEL_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Decode sentinel-encoded data.
 *
 * Values equal to @p sentinel are replaced by the next exception.
 * Non-sentinel values are zero-extended from @p valWidth to @p outWidth.
 *
 * @param output         Output buffer (outWidth * numElts bytes)
 * @param values         Values stream (valWidth bytes per element)
 * @param exceptions     Exceptions stream (outWidth bytes per element)
 * @param numElts        Number of elements in the values stream
 * @param numExceptions  Number of elements in the exceptions stream
 * @param valWidth       Element width of the values stream (bytes)
 * @param outWidth       Element width of the output/exceptions stream (bytes)
 * @param sentinel       The sentinel value (compared at valWidth)
 * @returns 0 on success, non-zero on error
 */
int ZL_sentinelDecode(
        void* output,
        const void* values,
        const void* exceptions,
        size_t numElts,
        size_t numExceptions,
        size_t valWidth,
        size_t outWidth,
        uint64_t sentinel);

#if defined(__cplusplus)
}
#endif

#endif
