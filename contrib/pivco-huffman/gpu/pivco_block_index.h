// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Structurally walks a PivCo-Huffman bitstream and returns byte offsets for
 * each independently encoded block.
 *
 * The weights are supplied out-of-band, matching the PivCo-Huffman kernel API.
 * Offsets are byte positions in @p bitstream and the output shape is
 * `numBlocks + 1`, where `numBlocks = ceil(decodedSize / blockSize)`.
 *
 * Constant blocks legitimately consume no bytes, so adjacent offsets may be
 * equal.
 */
ZL_Report pivcoFindBlockOffsets(
        uint64_t* offsets,
        size_t offsetsCapacity,
        const uint8_t* weights,
        size_t weightsSize,
        const uint8_t* bitstream,
        size_t bitstreamSize,
        size_t decodedSize,
        size_t blockSize);

#ifdef __cplusplus
}
#endif
