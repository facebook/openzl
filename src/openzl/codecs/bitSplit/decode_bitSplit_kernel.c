// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitSplit/decode_bitSplit_kernel.h"

uint64_t ZS_bitSplitDecode64(
        const uint8_t* bitWidths,
        size_t nbWidths,
        const void* const inputs[],
        const size_t* inputWidths,
        size_t idx)
{
    uint64_t result = 0;
    size_t bitPos = 0;

    for (size_t i = 0; i < nbWidths; i++) {
        unsigned const width = bitWidths[i];
        uint64_t extracted = 0;

        switch (inputWidths[i]) {
            case 1:
                extracted = ((const uint8_t*)inputs[i])[idx];
                break;
            case 2:
                extracted = ((const uint16_t*)inputs[i])[idx];
                break;
            case 4:
                extracted = ((const uint32_t*)inputs[i])[idx];
                break;
            case 8:
                extracted = ((const uint64_t*)inputs[i])[idx];
                break;
        }

        result |= (extracted << bitPos);
        bitPos += width;
    }

    return result;
}
