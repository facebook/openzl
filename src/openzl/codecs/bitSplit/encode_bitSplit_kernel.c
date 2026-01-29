// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitSplit/encode_bitSplit_kernel.h"

#include <string.h>

size_t ZS_bitSplit_outputEltWidth(unsigned bitWidth)
{
    if (bitWidth <= 8) {
        return 1;
    }
    if (bitWidth <= 16) {
        return 2;
    }
    if (bitWidth <= 32) {
        return 4;
    }
    return 8;
}

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

int ZS_bitSplit_topBitsAreZero(uint64_t value, size_t sumWidths)
{
    if (sumWidths >= 64) {
        return 1;
    }
    uint64_t const mask = ~((1ULL << sumWidths) - 1);
    return (value & mask) == 0;
}

void ZS_bitSplitEncode64(
        uint64_t value,
        const uint8_t* bitWidths,
        size_t nbWidths,
        void* const outputs[],
        const size_t* outputWidths)
{
    size_t bitPos = 0;
    for (size_t i = 0; i < nbWidths; i++) {
        unsigned const width = bitWidths[i];
        uint64_t const mask  = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
        uint64_t const extracted = (value >> bitPos) & mask;

        switch (outputWidths[i]) {
            case 1:
                *(uint8_t*)outputs[i] = (uint8_t)extracted;
                break;
            case 2:
                *(uint16_t*)outputs[i] = (uint16_t)extracted;
                break;
            case 4:
                *(uint32_t*)outputs[i] = (uint32_t)extracted;
                break;
            case 8:
                *(uint64_t*)outputs[i] = extracted;
                break;
        }

        bitPos += width;
    }
}
