// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITSPLIT_DECODE_BITSPLIT_KERNEL_H
#define ZSTRONG_TRANSFORMS_BITSPLIT_DECODE_BITSPLIT_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_portability.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Decodes bit-split streams back into original values.
 *
 * @param bitWidths Array of bit widths (LSB to MSB order)
 * @param nbWidths Number of bit widths
 * @param inputs Array of input stream pointers (one per bit range)
 * @param inputWidths Array of input element widths in bytes
 * @param idx Element index to decode
 * @return Reconstructed value
 */
uint64_t ZS_bitSplitDecode64(
        const uint8_t* bitWidths,
        size_t nbWidths,
        const void* const inputs[],
        const size_t* inputWidths,
        size_t idx);

#if defined(__cplusplus)
}
#endif

#endif // ZSTRONG_TRANSFORMS_BITSPLIT_DECODE_BITSPLIT_KERNEL_H
