// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITSPLIT_ENCODE_BITSPLIT_KERNEL_H
#define ZSTRONG_TRANSFORMS_BITSPLIT_ENCODE_BITSPLIT_KERNEL_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint8_t */

/**
 * Encodes an array of elements by extracting bit ranges.
 *
 * @param dstPtrs Array of destination pointers (one per bit range)
 * @param dstEltWidths Array of destination element widths in bytes
 * @param nbElts Number of elements to encode
 * @param src Source array of input values
 * @param srcEltWidth Element width of source (1, 2, 4, or 8 bytes)
 * @param bitWidths Array of bit widths (LSB to MSB order)
 * @param nbWidths Number of bit widths
 */
void ZS_bitSplitEncode(
        void* const dstPtrs[],
        const size_t* dstEltWidths,
        size_t nbElts,
        const void* src,
        size_t srcEltWidth,
        const uint8_t* bitWidths,
        size_t nbWidths);

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
 * Checks if top bits (beyond sum of widths) are zero for all elements.
 *
 * @param src Source array
 * @param srcEltWidth Element width of source (1, 2, 4, or 8 bytes)
 * @param nbElts Number of elements to check
 * @param sumWidths Sum of bit widths used
 * @return 1 if all top bits are zero, 0 otherwise
 */
int ZS_bitSplit_topBitsAreZero(
        const void* src,
        size_t srcEltWidth,
        size_t nbElts,
        size_t sumWidths);

#endif // ZSTRONG_TRANSFORMS_BITSPLIT_ENCODE_BITSPLIT_KERNEL_H
