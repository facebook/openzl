// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <assert.h>  /* assert */
#include <stdbool.h> /* bool */

#include "openzl/codecs/bitSplit/decode_bitSplit_kernel.h"

/*
 * Specialized decoder for bf16 pattern:
 * - 3 streams with bitWidths {7, 8, 1}
 * - All srcEltWidths are 1 (uint8_t)
 * - dstEltWidth is 2 (uint16_t)
 *
 * Layout: [mantissa:7][exponent:8][sign:1] = 16 bits
 */
static inline void
ZS_decodeBf16(void* dst, size_t nbElts, const void* const srcPtrs[])
{
    uint16_t* const dst16         = (uint16_t*)dst;
    const uint8_t* const mantissa = (const uint8_t*)srcPtrs[0];
    const uint8_t* const exponent = (const uint8_t*)srcPtrs[1];
    const uint8_t* const sign     = (const uint8_t*)srcPtrs[2];

    for (size_t e = 0; e < nbElts; e++) {
        uint16_t value = (uint16_t)mantissa[e]; /* bits 0-6 */
        value |= (uint16_t)exponent[e] << 7;    /* bits 7-14 */
        value |= (uint16_t)(sign[e] & 1) << 15; /* bit 15 */
        dst16[e] = value;
    }
}

/*
 * Check if parameters match the bf16 pattern.
 */
static inline bool ZS_isBf16Pattern(
        size_t dstEltWidth,
        const size_t* srcEltWidths,
        const uint8_t* bitWidths,
        size_t nbWidths)
{
    if (dstEltWidth != 2)
        return false;
    if (nbWidths != 3)
        return false;
    if (bitWidths[0] != 7 || bitWidths[1] != 8 || bitWidths[2] != 1)
        return false;
    if (srcEltWidths[0] != 1 || srcEltWidths[1] != 1 || srcEltWidths[2] != 1)
        return false;
    return true;
}

static inline void ZS_decodeElements(
        void* dst,
        const size_t dstEltWidth,
        size_t nbElts,
        const void* const srcPtrs[],
        const size_t* srcEltWidths,
        const uint8_t* bitWidths,
        size_t nbWidths)
{
    for (size_t e = 0; e < nbElts; e++) {
        uint64_t value = 0;
        size_t bitPos  = 0;

        for (size_t i = 0; i < nbWidths; i++) {
            uint64_t part = 0;
            switch (srcEltWidths[i]) {
                case 1:
                    part = ((const uint8_t*)srcPtrs[i])[e];
                    break;
                case 2:
                    part = ((const uint16_t*)srcPtrs[i])[e];
                    break;
                case 4:
                    part = ((const uint32_t*)srcPtrs[i])[e];
                    break;
                case 8:
                    part = ((const uint64_t*)srcPtrs[i])[e];
                    break;
            }

            value |= (part << bitPos);
            bitPos += bitWidths[i];
        }

        // If `dstEltWidth` is a constant and `ZS_decodeElements()` is inlined,
        // we expect the compiler to fold this switch() statement.
        switch (dstEltWidth) {
            case 1:
                ((uint8_t*)dst)[e] = (uint8_t)value;
                break;
            case 2:
                ((uint16_t*)dst)[e] = (uint16_t)value;
                break;
            case 4:
                ((uint32_t*)dst)[e] = (uint32_t)value;
                break;
            case 8:
                ((uint64_t*)dst)[e] = (uint64_t)value;
                break;
        }
    }
}

void ZS_bitSplitDecode(
        void* dst,
        size_t dstEltWidth,
        size_t nbElts,
        const void* const srcPtrs[],
        const size_t* srcEltWidths,
        const uint8_t* bitWidths,
        size_t nbWidths)
{
    if (nbElts == 0)
        return;

    assert(dst != NULL);
    assert(dstEltWidth == 1 || dstEltWidth == 2 || dstEltWidth == 4
           || dstEltWidth == 8);
    assert(srcPtrs != NULL);
    assert(srcEltWidths != NULL);
    assert(bitWidths != NULL);
    assert(nbWidths > 0);
    assert(nbWidths <= 64);

    /* Validate sum of bit widths fits in destination */
    {
        size_t sumBitWidths = 0;
        for (size_t i = 0; i < nbWidths; i++) {
            assert(srcPtrs[i] != NULL);
            assert(srcEltWidths[i] == 1 || srcEltWidths[i] == 2
                   || srcEltWidths[i] == 4 || srcEltWidths[i] == 8);
            assert(bitWidths[i] > 0);
            assert(bitWidths[i] <= srcEltWidths[i] * 8);
            sumBitWidths += bitWidths[i];
        }
        assert(sumBitWidths <= dstEltWidth * 8);
    }

    /* Check for specialized patterns and dispatch */
    if (ZS_isBf16Pattern(dstEltWidth, srcEltWidths, bitWidths, nbWidths)) {
        ZS_decodeBf16(dst, nbElts, srcPtrs);
        return;
    }

    /* Generic path */
    // We expect the compiler to optimize ZS_decodeElements() by propagating the
    // constant (thus resulting in several instances).
    switch (dstEltWidth) {
        case 1:
            ZS_decodeElements(
                    dst, 1, nbElts, srcPtrs, srcEltWidths, bitWidths, nbWidths);
            break;
        case 2:
            ZS_decodeElements(
                    dst, 2, nbElts, srcPtrs, srcEltWidths, bitWidths, nbWidths);
            break;
        case 4:
            ZS_decodeElements(
                    dst, 4, nbElts, srcPtrs, srcEltWidths, bitWidths, nbWidths);
            break;
        case 8:
            ZS_decodeElements(
                    dst, 8, nbElts, srcPtrs, srcEltWidths, bitWidths, nbWidths);
            break;
    }
}
