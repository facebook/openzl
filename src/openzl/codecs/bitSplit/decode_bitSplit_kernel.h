// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITSPLIT_DECODE_BITSPLIT_KERNEL_H
#define ZSTRONG_TRANSFORMS_BITSPLIT_DECODE_BITSPLIT_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/codecs/bitSplit/bitSplit_common.h"

/**
 * Decodes arrays of bit-split values back into original elements.
 * Owns the hot loop - processes all elements.
 *
 * @param srcPtrs Array of source pointers (one per bit range), NULL means zeros
 * @param srcEltWidths Array of source element widths in bytes
 * @param bitWidths Array of bit widths (LSB to MSB order)
 * @param nbWidths Number of bit widths / source streams
 * @param dst Destination array for reconstructed values
 * @param dstEltWidth Element width of destination (1, 2, 4, or 8 bytes)
 * @param nbElts Number of elements to decode
 */
void ZS_bitSplitDecode64(
        const void* const srcPtrs[],
        const size_t* srcEltWidths,
        const uint8_t* bitWidths,
        size_t nbWidths,
        void* dst,
        size_t dstEltWidth,
        size_t nbElts);

#endif // ZSTRONG_TRANSFORMS_BITSPLIT_DECODE_BITSPLIT_KERNEL_H
