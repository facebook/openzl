// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitSplit/decode_bitSplit_kernel.h"

void ZS_bitSplitDecode64(
        const void* const srcPtrs[],
        const size_t* srcEltWidths,
        const uint8_t* bitWidths,
        size_t nbWidths,
        void* dst,
        size_t dstEltWidth,
        size_t nbElts)
{
    for (size_t e = 0; e < nbElts; e++) {
        uint64_t value = 0;
        size_t bitPos  = 0;

        for (size_t i = 0; i < nbWidths; i++) {
            uint64_t part = 0;
            if (srcPtrs[i] != NULL) {
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
            }
            // else: srcPtrs[i] == NULL means implicit zeros, part stays 0

            // Mask to the bit width
            unsigned const width = bitWidths[i];
            uint64_t const mask  = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
            part &= mask;

            // Place in output value
            value |= (part << bitPos);
            bitPos += width;
        }

        // Write output
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
                ((uint64_t*)dst)[e] = value;
                break;
        }
    }
}
