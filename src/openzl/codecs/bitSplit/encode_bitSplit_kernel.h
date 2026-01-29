// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITSPLIT_ENCODE_BITSPLIT_KERNEL_H
#define ZSTRONG_TRANSFORMS_BITSPLIT_ENCODE_BITSPLIT_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/codecs/bitSplit/bitSplit_common.h"

/**
 * Encodes a single element by extracting bit ranges.
 *
 * @param value The input value to split
 * @param bitWidths Array of bit widths (LSB to MSB order)
 * @param nbWidths Number of bit widths
 * @param outputs Array of output pointers (one per bit range)
 * @param outputWidths Array of output element widths in bytes
 */
void ZS_bitSplitEncode64(
        uint64_t value,
        const uint8_t* bitWidths,
        size_t nbWidths,
        void* const outputs[],
        const size_t* outputWidths);

/**
 * Validates bitSplit parameters.
 *
 * @param bitWidths Array of bit widths
 * @param nbWidths Number of bit widths (must be > 0)
 * @param inputEltWidthBits Input element width in bits
 * @param sumWidths Output: sum of all bit widths
 * @return 0 on success, non-zero on error
 */
int ZS_bitSplit_validateParams(
        const uint8_t* bitWidths,
        size_t nbWidths,
        size_t inputEltWidthBits,
        size_t* sumWidths);

/**
 * Checks if top bits (beyond sum of widths) are zero.
 *
 * @param value The value to check
 * @param sumWidths Sum of bit widths used
 * @return 1 if top bits are zero, 0 otherwise
 */
int ZS_bitSplit_topBitsAreZero(uint64_t value, size_t sumWidths);

#endif // ZSTRONG_TRANSFORMS_BITSPLIT_ENCODE_BITSPLIT_KERNEL_H
