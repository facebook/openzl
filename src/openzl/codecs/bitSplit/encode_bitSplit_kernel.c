// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitSplit/encode_bitSplit_kernel.h"

#include <assert.h> /* assert */
#include <string.h>

bool ZS_bitSplit_paramsAreValid(
        const uint8_t* bitWidths,
        size_t nbWidths,
        size_t inputEltWidthBits,
        size_t* sumWidths)
{
    assert(bitWidths != NULL);
    assert(nbWidths > 0);

    size_t sum = 0;
    for (size_t i = 0; i < nbWidths; i++) {
        if (bitWidths[i] == 0) {
            return false;
        }
        sum += bitWidths[i];
    }
    if (sum > inputEltWidthBits) {
        return false;
    }
    if (sumWidths != NULL) {
        *sumWidths = sum;
    }
    return true;
}

bool ZS_bitSplit_topBitsAreZero(
        const void* src,
        size_t srcEltWidth,
        size_t nbElts,
        size_t sumWidths)
{
    if (nbElts == 0) return true;

    assert(src != NULL);
    assert(srcEltWidth == 1 || srcEltWidth == 2 || srcEltWidth == 4
           || srcEltWidth == 8);

    if (sumWidths >= 64) {
        return true;
    }
    uint64_t const mask = ~((1ULL << sumWidths) - 1);

    for (size_t e = 0; e < nbElts; e++) {
        uint64_t value = 0;
        switch (srcEltWidth) {
            case 1:
                value = ((const uint8_t*)src)[e];
                break;
            case 2:
                value = ((const uint16_t*)src)[e];
                break;
            case 4:
                value = ((const uint32_t*)src)[e];
                break;
            case 8:
                value = ((const uint64_t*)src)[e];
                break;
        }
        if ((value & mask) != 0) {
            return false;
        }
    }
    return true;
}

void ZS_bitSplitEncode(
        void* const dstPtrs[],
        const size_t* dstEltWidths,
        size_t nbElts,
        const void* src,
        size_t srcEltWidth,
        const uint8_t* bitWidths,
        size_t nbWidths)
{
    if (nbElts == 0) return;

    assert(dstPtrs != NULL);
    assert(dstEltWidths != NULL);
    assert(src != NULL);
    assert(srcEltWidth == 1 || srcEltWidth == 2 || srcEltWidth == 4
           || srcEltWidth == 8);
    assert(bitWidths != NULL);
    assert(nbWidths > 0);
    assert(nbWidths <= 64);

    /* Validate parameters and individual streams */
    {
        size_t sumBitWidths = 0;
        for (size_t i = 0; i < nbWidths; i++) {
            assert(dstPtrs[i] != NULL);
            assert(dstEltWidths[i] == 1 || dstEltWidths[i] == 2
                   || dstEltWidths[i] == 4 || dstEltWidths[i] == 8);
            assert(bitWidths[i] > 0);
            assert(bitWidths[i] <= dstEltWidths[i] * 8);
            sumBitWidths += bitWidths[i];
        }
        assert(sumBitWidths <= srcEltWidth * 8);
    }

    for (size_t e = 0; e < nbElts; e++) {
        // Read input value
        uint64_t value = 0;
        switch (srcEltWidth) {
            case 1:
                value = ((const uint8_t*)src)[e];
                break;
            case 2:
                value = ((const uint16_t*)src)[e];
                break;
            case 4:
                value = ((const uint32_t*)src)[e];
                break;
            case 8:
                value = ((const uint64_t*)src)[e];
                break;
        }

        // Split the value into bit ranges
        size_t bitPos = 0;
        for (size_t i = 0; i < nbWidths; i++) {
            unsigned const width = bitWidths[i];
            uint64_t const mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
            uint64_t const extracted = (value >> bitPos) & mask;

            switch (dstEltWidths[i]) {
                case 1:
                    ((uint8_t*)dstPtrs[i])[e] = (uint8_t)extracted;
                    break;
                case 2:
                    ((uint16_t*)dstPtrs[i])[e] = (uint16_t)extracted;
                    break;
                case 4:
                    ((uint32_t*)dstPtrs[i])[e] = (uint32_t)extracted;
                    break;
                case 8:
                    ((uint64_t*)dstPtrs[i])[e] = extracted;
                    break;
            }

            bitPos += width;
        }
    }
}
