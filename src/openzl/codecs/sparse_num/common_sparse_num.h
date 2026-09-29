// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_SPARSE_NUM_COMMON_SPARSE_NUM_H
#define OPENZL_CODECS_SPARSE_NUM_COMMON_SPARSE_NUM_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Supported numeric element widths, in bytes: 1, 2, 4, or 8. */
static inline bool ZL_sparseNumValidValueWidth(size_t width)
{
    return width == 1 || width == 2 || width == 4 || width == 8;
}

/** Supported distance stream widths, in bytes: 1, 2, or 4. */
static inline bool ZL_sparseNumValidDistanceWidth(size_t width)
{
    return width == 1 || width == 2 || width == 4;
}

/** Returns whether ptr is aligned for width-byte numeric elements. */
static inline bool ZL_sparseNumIsAlignedForWidth(const void* ptr, size_t width)
{
    assert(width > 0);
    return (((uintptr_t)ptr) & (width - 1)) == 0;
}

/**
 * Copies one aligned sparse_num literal value.
 *
 * Preconditions:
 * - dst and src must be non-null.
 * - dst and src must be aligned for width-byte numeric elements.
 * - width must be a supported numeric element width.
 */
static inline void
ZL_sparseNumCopyAlignedValue(void* dst, const void* src, size_t width)
{
    assert(dst != NULL);
    assert(src != NULL);
    assert(ZL_sparseNumIsAlignedForWidth(dst, width));
    assert(ZL_sparseNumIsAlignedForWidth(src, width));
    switch (width) {
        case 1:
            *(uint8_t*)dst = *(const uint8_t*)src;
            return;
        case 2:
            *(uint16_t*)dst = *(const uint16_t*)src;
            return;
        case 4:
            *(uint32_t*)dst = *(const uint32_t*)src;
            return;
        case 8:
            *(uint64_t*)dst = *(const uint64_t*)src;
            return;
        default:
            assert(false);
            return;
    }
}

#endif
