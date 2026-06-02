// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMMON_BITS_H
#define ZSTRONG_COMMON_BITS_H

#include <math.h>

#ifdef _MSC_VER
#    include <intrin.h> // For _byteswap_* intrinsics
#endif

#include "openzl/common/debug.h"
#include "openzl/shared/portability.h"

#if ZL_HAS_BMI2
#    include <immintrin.h>
#endif

#if ZL_HAS_SVE2_BITPERM
#    include <arm_sve.h>
#endif

ZL_BEGIN_C_DECLS

ZL_INLINE bool ZL_32bits(void)
{
    // Note (@cyan) : note that x32 systems are 64-bit,
    // yet they use pointers and by extension size_t of 32-bit.
    return sizeof(size_t) == 4;
}

ZL_INLINE bool ZL_64bits(void)
{
    return sizeof(size_t) == 8;
}

ZL_INLINE bool ZL_isLittleEndian(void)
{
    const union {
        uint32_t u;
        uint8_t c[4];
    } one = { 1 }; // don't use static: performance detrimental
    return one.c[0];
}

/**
 * This enum is used to tag serialized streams of numeric data with its
 * endianness.
 */
typedef enum {
    ZL_Endianness_little = 0,
    ZL_Endianness_big    = 1,
} ZL_Endianness;

/**
 * Identifies a Stream Representation.
 *
 * Integer streams can be interacted with in either a canonical serialized byte
 * representation, or in a host-endian native integer representation. This enum
 * selects between the two.
 *
 * Note that this selection is only meaningful for integers! All other types are
 * only available in their serialized format.
 */
typedef enum {
    ZL_StreamRep_serialized = 0,
    ZL_StreamRep_native     = 1,
} ZL_StreamRep;

/**
 * The canonical endianness of internal serialized integers (by default) is
 * little-endian.
 */
#ifndef ZL_CANONICAL_ENDIANNESS_IS_LITTLE
#    define ZL_CANONICAL_ENDIANNESS_IS_LITTLE 1
#endif

/**
 * This aliases the canonical endianness to the appropriate enum value.
 */
#if ZL_CANONICAL_ENDIANNESS_IS_LITTLE
#    define ZL_Endianness_canonical ZL_Endianness_little
#else
#    define ZL_Endianness_canonical ZL_Endianness_big
#endif

ZL_INLINE ZL_Endianness ZL_Endianness_host(void)
{
    return ZL_isLittleEndian() ? ZL_Endianness_little : ZL_Endianness_big;
}

/**
 * This aliases the stream representation to the correct concrete endianness.
 */
ZL_INLINE ZL_Endianness ZL_StreamRep_resolve(ZL_StreamRep hostEndian)
{
    return hostEndian == ZL_StreamRep_serialized ? ZL_Endianness_canonical
                                                 : ZL_Endianness_host();
}

ZL_INLINE int ZL_popcount64_fallback(uint64_t x)
{
    int popcount = 0;
    for (int bit = 0; bit < 64; ++bit) {
        popcount += (x & ((uint64_t)1 << bit)) != 0;
    }
    return popcount;
}

ZL_INLINE int ZL_popcount64(uint64_t x)
{
#if ZL_HAS_BUILTIN(__builtin_popcountll)
    return __builtin_popcountll(x);
#else
    return ZL_popcount64_fallback(x);
#endif
}

ZL_INLINE int ZL_clz32_fallback(uint32_t x)
{
    int leadingZeros = 0;
    for (int bit = 31; bit >= 0; --bit) {
        if (x & (1u << bit))
            break;
        ++leadingZeros;
    }
    return leadingZeros;
}

ZL_INLINE int ZL_clz32(uint32_t x)
{
#if ZL_HAS_BUILTIN(__builtin_clz)
    return __builtin_clz(x);
#elif defined(_MSC_VER)
    if (x == 0)
        return 32;
    unsigned long index;
    _BitScanReverse(&index, x);
    return 31 - (int)index;
#else
    return ZL_clz32_fallback(x);
#endif
}
ZL_INLINE int ZL_clz64_fallback(uint64_t x)
{
    int leadingZeros = 0;
    for (int bit = 63; bit >= 0; --bit) {
        if (x & ((uint64_t)1 << bit))
            break;
        ++leadingZeros;
    }
    return leadingZeros;
}

ZL_INLINE int ZL_clz64(uint64_t x)
{
#if ZL_HAS_BUILTIN(__builtin_clzll)
    ZL_STATIC_ASSERT(sizeof(unsigned long long) == sizeof(uint64_t), "");
    return __builtin_clzll(x);
#elif defined(_MSC_VER)
    if (x == 0)
        return 64;
    unsigned long index;
    _BitScanReverse64(&index, x);
    return 63 - (int)index;
#else
    return ZL_clz64_fallback(x);
#endif
}

ZL_INLINE int ZL_ctz32_fallback(uint32_t x)
{
    int trailingZeros = 0;
    for (int bit = 0; bit < 32; ++bit) {
        if (x & (1u << bit))
            break;
        ++trailingZeros;
    }
    return trailingZeros;
}

ZL_INLINE int ZL_ctz32(uint32_t x)
{
#if ZL_HAS_BUILTIN(__builtin_ctz)
    return __builtin_ctz(x);
#elif defined(_MSC_VER)
    if (x == 0)
        return 32;
    unsigned long index;
    _BitScanForward(&index, x);
    return (int)index;
#else
    return ZL_ctz32_fallback(x);
#endif
}

ZL_INLINE int ZL_ctz64_fallback(uint64_t x)
{
    int trailingZeros = 0;
    for (int bit = 0; bit < 64; ++bit) {
        if (x & ((uint64_t)1 << bit))
            break;
        ++trailingZeros;
    }
    return trailingZeros;
}

ZL_INLINE int ZL_ctz64(uint64_t x)
{
#if ZL_HAS_BUILTIN(__builtin_ctzll)
    ZL_STATIC_ASSERT(sizeof(unsigned long long) == sizeof(uint64_t), "");
    return __builtin_ctzll(x);
#elif defined(_MSC_VER)
    if (x == 0)
        return 64;
    unsigned long index;
    _BitScanForward64(&index, x);
    return (int)index;
#else
    return ZL_ctz64_fallback(x);
#endif
}

ZL_INLINE int ZL_nextPow2_fallback(uint64_t upperBound)
{
    int i        = 0;
    uint64_t val = 1;
    while ((val < upperBound) && (i < 64)) {
        i++;
        val <<= 1;
    }
    return i;
}

// ZL_nextPow2():
// @return n so that (1 << n) >= upperBound
// thus allowing to represent all values up to (upperBound-1) using n bits.
ZL_INLINE int ZL_nextPow2(uint64_t upperBound)
{
#if ZL_HAS_BUILTIN(__builtin_clzll)
    if (upperBound <= 1)
        return 0;
    return 64 - (int)ZL_clz64(upperBound - 1);
#else
    return ZL_nextPow2_fallback(upperBound);
#endif
}

ZL_INLINE int ZL_highbit32(uint32_t value)
{
    ZL_ASSERT(value != 0);
    return ZL_clz32(value) ^ 31;
}

ZL_INLINE int ZL_highbit64(uint64_t value)
{
    ZL_ASSERT(value != 0);
    return ZL_clz64(value) ^ 63;
}

ZL_INLINE uint16_t ZL_swap16(uint16_t in)
{
#if defined(_MSC_VER)
    return _byteswap_ushort(in);
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap16(in);
#else
    return (uint16_t)((in << 8) | (in >> 8));
#endif
}

ZL_INLINE uint32_t ZL_swap32(uint32_t in)
{
#if defined(_MSC_VER)
    return _byteswap_ulong(in);
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(in);
#else
    return ((in << 24) & 0xff000000) | ((in << 8) & 0x00ff0000)
            | ((in >> 8) & 0x0000ff00) | ((in >> 24) & 0x000000ff);
#endif
}

ZL_INLINE uint64_t ZL_swap64(uint64_t in)
{
#if defined(_MSC_VER)
    return _byteswap_uint64(in);
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(in);
#else
    return ((in << 56) & 0xff00000000000000ULL)
            | ((in << 40) & 0x00ff000000000000ULL)
            | ((in << 24) & 0x0000ff00000000ULL)
            | ((in << 8) & 0x000000ff000000ULL)
            | ((in >> 8) & 0x00000000ff0000ULL)
            | ((in >> 24) & 0x0000000000ff00ULL)
            | ((in >> 40) & 0x000000000000ffULL)
            | ((in >> 56) & 0x00000000000000ffULL);
#endif
}

ZL_INLINE size_t ZL_swapST(size_t in)
{
    if (ZL_32bits()) {
        return (size_t)ZL_swap32((uint32_t)in);
    } else {
        return (size_t)ZL_swap64((uint64_t)in);
    }
}

typedef struct {
    uint64_t value;
} ZL_IEEEDouble;

/**
 * @returns true if @p value can be represented exactly as an IEEE double.
 * May return false for values that are representable as an IEEE double, e.g.
 * for large integers.
 */
ZL_INLINE bool ZL_canConvertIntToDouble(int64_t value);

/**
 * Converts @p value to an IEEE double, possibly with precision loss.
 * If ZL_canConvertIntToDouble(value), then the result is guaranteed to be
 * exact. Otherwise, the result is an unspecified but valid IEEE double.
 *
 * WARNING: We only provide guarantees about the output when @p value can
 * be represented exactly as an IEEE double.
 */
ZL_INLINE ZL_IEEEDouble ZL_convertIntToDoubleUnchecked(int64_t value);

/**
 * Converts @p value to an IEEE double representing the same value if possible,
 * otherwise returns false and does no conversion.
 *
 * @returns true iff @p value was converted losslessly to an IEEE double, which
 * is exactly when ZL_canConvertIntToDouble(value) returns true.
 */
ZL_INLINE bool ZL_convertIntToDouble(ZL_IEEEDouble* dbl, int64_t value);

/**
 * Converts @p dbl to an int64_t, possibly with precision loss.
 * If @p dbl represents an integer that fits in an int64_t, it will be converted
 * losslessly to an int64_t. Otherwise, the result is a valid but undefined
 * int64_t.
 *
 * WARNING: We do not guarantee any specific rounding for non-integers.
 * This function only guarantees correct output when @p dbl is an integer.
 */
ZL_INLINE int64_t ZL_convertDoubleToIntUnchecked(ZL_IEEEDouble dbl);

/**
 * Converts @p dbl to an int64_t if it can be converted without precision loss,
 * otherwise it returns false and no conversion is performed.
 */
ZL_INLINE bool ZL_convertDoubleToInt(int64_t* value, ZL_IEEEDouble dbl);

#if ZL_HAS_IEEE_754
// These functions only work when the representation of a double follows the
// IEEE 754 standard.

ZL_INLINE bool ZL_canConvertIntToDouble(int64_t value)
{
    // Every integer in the range [-2^53, 2^53] can be represented exactly as a
    // double. Some integers outside of this range can also be represented
    // exactly as a double, but for simplicity just refuse to convert them.
    int64_t const kMaxInt = (int64_t)1 << 53;
    if (value >= -kMaxInt && value <= kMaxInt) {
        return true;
    } else {
        return false;
    }
}

/**
 * @returns true if @p dbl can be converted to an int64_t without
 * UB. It also filters out integers with magnitude > 2^53. There
 * may still be precision loss on that conversion that must be
 * checked. E.g. the values -0.0 and 0.5 are not filtered out by this check.
 */
ZL_INLINE bool ZL_shouldAttemptDoubleToInt(ZL_IEEEDouble dbl)
{
    double val;
    memcpy(&val, &dbl, sizeof(dbl));
    double const kMaxVal = (double)((int64_t)1 << 53);
    // NaNs are filtered out by this comparison because every comparison with
    // a NaN value returns false.
    ZL_ASSERT(!(NAN >= -kMaxVal));
    ZL_ASSERT(!(NAN <= kMaxVal));
    return val >= -kMaxVal && val <= kMaxVal;
}

ZL_INLINE int64_t ZL_convertDoubleToIntUnchecked(ZL_IEEEDouble dbl)
{
    // Only check if conversion will trigger UB.
    if (ZL_shouldAttemptDoubleToInt(dbl)) {
        double val;
        memcpy(&val, &dbl, sizeof(dbl));
        int64_t const converted = (int64_t)val;
        return converted;
    } else {
        return 0;
    }
}

ZL_INLINE bool ZL_convertDoubleToInt(int64_t* valuePtr, ZL_IEEEDouble dbl)
{
    // First check if conversion would trigger UB.
    if (ZL_shouldAttemptDoubleToInt(dbl)) {
        int64_t const value = ZL_convertDoubleToIntUnchecked(dbl);
        if (!ZL_canConvertIntToDouble(value)) {
            return false;
        }
        // After conversion, convert back to a double and check that we have a
        // bit-identical representation.
        double const check = (double)value;
        bool const success = memcmp(&dbl, &check, sizeof(dbl)) == 0;
        if (success) {
            *valuePtr = value;
        }
        return success;
    } else {
        return false;
    }
}

ZL_INLINE ZL_IEEEDouble ZL_convertIntToDoubleUnchecked(int64_t value)
{
    double const converted = (double)value;
    ZL_IEEEDouble dbl;
    memcpy(&dbl, &converted, sizeof(dbl));

    return dbl;
}

ZL_INLINE bool ZL_convertIntToDouble(ZL_IEEEDouble* dbl, int64_t value)
{
    // Filter out integers whose conversion may be lossy
    if (!ZL_canConvertIntToDouble(value)) {
        return false;
    }
    *dbl = ZL_convertIntToDoubleUnchecked(value);

#    ifndef NDEBUG
    // We checked that conversion is lossless before converting.
    // Double check in debug mode.
    int64_t check;
    ZL_ASSERT(ZL_convertDoubleToInt(&check, *dbl));
    ZL_ASSERT_EQ(value, check);
#    endif

    return true;
}

#else
// When we have a non-standard double representation require fail.
// We could implement a slow fallback if needed.

ZL_INLINE bool ZL_canConvertIntToDouble(int64_t value)
{
    (void)value;
    ZL_REQUIRE_FAIL("Unsupported");
}

ZL_INLINE int64_t ZL_convertDoubleToIntUnchecked(ZL_IEEEDouble dbl)
{
    (void)dbl;
    ZL_REQUIRE_FAIL("Unsupported");
}

ZL_INLINE bool ZL_convertDoubleToInt(int64_t* value, ZL_IEEEDouble dbl)
{
    (void)value;
    (void)dbl;
    ZL_REQUIRE_FAIL("Unsupported");
}

ZL_INLINE ZL_IEEEDouble ZL_convertIntToDoubleUnchecked(int64_t value)
{
    (void)value;
    ZL_REQUIRE_FAIL("Unsupported");
}

ZL_INLINE bool ZL_convertIntToDouble(ZL_IEEEDouble* dbl, int64_t value)
{
    (void)dbl;
    (void)value;
    ZL_REQUIRE_FAIL("Unsupported");
}

#endif

// ---------------------------------------------------------------------------
// bitDeposit: scatter contiguous source bits into positions given by mask
// (PDEP)
// ---------------------------------------------------------------------------

ZL_INLINE uint64_t ZL_bitDeposit64_fallback(uint64_t src, uint64_t mask)
{
    uint64_t result = 0;
    uint64_t srcBit = 1;
    while (mask != 0) {
        uint64_t const lowestBit = mask & (~mask + 1);
        if (src & srcBit) {
            result |= lowestBit;
        }
        mask &= ~lowestBit;
        srcBit <<= 1;
    }
    return result;
}

ZL_INLINE uint64_t ZL_bitDeposit64(uint64_t src, uint64_t mask)
{
#if ZL_HAS_BMI2
    return _pdep_u64(src, mask);
#elif ZL_HAS_SVE2_BITPERM
    // This is not as terse as the BMI2 instruction because SVE2 BDEP is a
    // vector instruction
    return svlastb_u64(
            svptrue_b64(), svbdep_u64(svdup_n_u64(src), svdup_n_u64(mask)));
#else
    return ZL_bitDeposit64_fallback(src, mask);
#endif
}

ZL_INLINE uint32_t ZL_bitDeposit32_fallback(uint32_t src, uint32_t mask)
{
    return (uint32_t)ZL_bitDeposit64_fallback((uint64_t)src, (uint64_t)mask);
}

ZL_INLINE uint32_t ZL_bitDeposit32(uint32_t src, uint32_t mask)
{
#if ZL_HAS_BMI2
    return _pdep_u32(src, mask);
#elif ZL_HAS_SVE2_BITPERM
    return (uint32_t)svlastb_u32(
            svptrue_b32(), svbdep_u32(svdup_n_u32(src), svdup_n_u32(mask)));
#else
    return ZL_bitDeposit32_fallback(src, mask);
#endif
}

// ---------------------------------------------------------------------------
// bitExtract: collect bits from positions given by mask into contiguous result
// (PEXT)
// ---------------------------------------------------------------------------

ZL_INLINE uint64_t ZL_bitExtract64_fallback(uint64_t src, uint64_t mask)
{
    uint64_t result = 0;
    uint64_t dstBit = 1;
    while (mask != 0) {
        uint64_t const lowestBit = mask & (~mask + 1);
        if (src & lowestBit) {
            result |= dstBit;
        }
        mask &= ~lowestBit;
        dstBit <<= 1;
    }
    return result;
}

ZL_INLINE uint64_t ZL_bitExtract64(uint64_t src, uint64_t mask)
{
#if ZL_HAS_BMI2
    return _pext_u64(src, mask);
#else
    return ZL_bitExtract64_fallback(src, mask);
#endif
}

ZL_INLINE uint32_t ZL_bitExtract32_fallback(uint32_t src, uint32_t mask)
{
    return (uint32_t)ZL_bitExtract64_fallback((uint64_t)src, (uint64_t)mask);
}

ZL_INLINE uint32_t ZL_bitExtract32(uint32_t src, uint32_t mask)
{
#if ZL_HAS_BMI2
    return _pext_u32(src, mask);
#else
    return ZL_bitExtract32_fallback(src, mask);
#endif
}

ZL_END_C_DECLS

#endif // ZSTRONG_COMMON_BITS_H
