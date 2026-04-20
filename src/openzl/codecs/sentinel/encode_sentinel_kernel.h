// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_SENTINEL_ENCODE_SENTINEL_KERNEL_H
#define OPENZL_CODECS_SENTINEL_ENCODE_SENTINEL_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Sentinel-byte encode: values < 255 are stored as 1 byte in @p values,
 * values >= 255 are stored at full width in @p exceptions with sentinel 255
 * placed into @p values.
 *
 * @param values           Output: 1 byte per element (sentinel or narrowed
 *                         value)
 * @param exceptions       Output: full-width exceptions (eltWidth bytes each)
 * @param input            Input elements (eltWidth bytes each)
 * @param numElts          Number of input elements
 * @param eltWidth         Width of each input element in bytes (2, 4, or 8).
 *                         Must be > 1 (1-byte inputs make no sense for
 *                         byte-narrowing).
 * @returns Number of exceptions written
 */
size_t ZL_sentinelByteEncode(
        uint8_t* values,
        void* exceptions,
        const void* input,
        size_t numElts,
        size_t eltWidth);

/**
 * General sentinel encode: places sentinel at exception positions and copies
 * non-exception values to the values stream at the same width. Exception
 * values are copied to the exceptions stream.
 *
 * @param values           Output values (eltWidth bytes per element, numElts)
 * @param exceptions       Output exceptions (eltWidth bytes per element)
 * @param input            Input elements (eltWidth bytes per element)
 * @param numElts          Number of input elements
 * @param eltWidth         Width of each element in bytes (1, 2, 4, or 8)
 * @param exceptionIndices Sorted array of exception indices
 * @param numExceptions    Number of exception indices
 * @param sentinel         The sentinel value (must fit in eltWidth)
 * @returns 0 on success, non-zero on validation failure
 */
int ZL_sentinelEncode(
        void* values,
        void* exceptions,
        const void* input,
        size_t numElts,
        size_t eltWidth,
        const size_t* exceptionIndices,
        size_t numExceptions,
        uint64_t sentinel);

#if defined(__cplusplus)
}
#endif

#endif
