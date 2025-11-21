// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZS_COMMON_UTILS_H
#define ZS_COMMON_UTILS_H

#include "openzl/common/debug.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

#if defined(__GNUC__) || defined(__clang__)
#    define ZL_MAYBE_UNUSED_FUNCTION __attribute__((__unused__))
#elif defined(_MSC_VER)
// MSVC does not have a direct equivalent for marking an entire function as
// "unused" to suppress a warning if it is defined but not called.
// Usually, this warning is handled by linker settings, or by ensuring
// the function has internal linkage (static).
#    define ZL_MAYBE_UNUSED_FUNCTION
#else
#    define ZL_MAYBE_UNUSED_FUNCTION
#endif

/**
 * This should only be for small helper functions that don't belong in any
 * grouping.
 */

#define ZL_MIN(a, b) ((a) < (b) ? (a) : (b))
#define ZL_MAX(a, b) ((a) > (b) ? (a) : (b))
#define ZL_CLAMP(x, lo, hi) ZL_MIN(ZL_MAX((x), (lo)), (hi))

#define ZL_CONTAINER_OF(ptr, type, member) \
    (type*)(ptr == NULL ? NULL : (void*)((char*)(ptr) - offsetof(type, member)))
#define ZL_CONST_CONTAINER_OF(ptr, type, member)                 \
    (const type*)(ptr == NULL ? NULL                             \
                              : (const void*)((const char*)(ptr) \
                                              - offsetof(type, member)))

ZL_INLINE bool ZL_isPow2(uint64_t val)
{
    return (val & (val - 1)) == 0;
}

ZL_INLINE bool ZL_uintFits(uint64_t val, int bytes)
{
    ZL_ASSERT_GT(bytes, 0);
    ZL_ASSERT_LE(bytes, 8);
    uint64_t const pow2 = bytes >= 8 ? 0 : (uint64_t)1 << (8 * bytes);
    uint64_t const mask = pow2 - 1;
    return (val & ~mask) == 0;
}

#define ZL_ASSERT_UINT_FITS(val, type) \
    ZL_ASSERT(ZL_uintFits((val), sizeof(type)))
#define ZL_REQUIRE_UINT_FITS(val, type) \
    ZL_REQUIRE(ZL_uintFits((val), sizeof(type)))

#define ZL_ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

#define ZL_ARRAY_SHIFT_LEFT(array, n) \
    memmove((array), (array) + n, sizeof((array)) - n * sizeof((array)[0]))

#define ZL_ARRAY_SHIFT_RIGHT(array, n) \
    memmove((array) + n, (array), sizeof((array)) - n * sizeof((array)[0]))

/**
 * @returns true iff @p width is a legal integer width .
 */
ZL_INLINE size_t ZL_isLegalIntegerWidth(size_t width)
{
    return width == 1 || width == 2 || width == 4 || width == 8;
}

ZL_END_C_DECLS

#endif // ZS_COMMON_UTILS_H
