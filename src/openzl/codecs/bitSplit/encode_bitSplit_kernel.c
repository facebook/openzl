// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitSplit/encode_bitSplit_kernel.h"

#include <string.h>

int ZS_bitSplit_validateParams(
        const uint8_t* bitWidths,
        size_t nbWidths,
        size_t inputEltWidthBits,
        size_t* sumWidths)
{
    *sumWidths = 0;
    for (size_t i = 0; i < nbWidths; i++) {
        if (bitWidths[i] == 0) {
            return -1; // width == 0 is invalid
        }
        *sumWidths += bitWidths[i];
    }
    if (*sumWidths > inputEltWidthBits) {
        return -2; // sum of widths exceeds input element width
    }
    return 0;
}

int ZS_bitSplit_topBitsAreZero(
        const void* src,
        size_t srcEltWidth,
        size_t nbElts,
        size_t sumWidths)
{
    if (sumWidths >= 64) {
        return 1;
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
            return 0;
        }
    }
    return 1;
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
