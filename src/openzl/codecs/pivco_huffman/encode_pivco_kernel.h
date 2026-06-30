// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_PIVCO_HUFFMAN_ENCODE_PIVCO_KERNEL_H
#define OPENZL_CODECS_PIVCO_HUFFMAN_ENCODE_PIVCO_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/codecs/pivco_huffman/arch/encode_pivco_arch.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/**
 * @returns the number of bytes required by the encoder scratch.
 */
size_t ZL_PivCoHuffmanEncode_scratchElements(size_t srcSize, size_t blockSize);

/**
 * @returns the output capacity including slop bytes to guarantee success
 * (except for integer overflow cases, where SIZE_MAX is returned).
 *
 * Smaller destination sizes can be passed to the encoder safely, but then it
 * may fail to compress and return an error.
 *
 * @pre blockSize > 0 && blockSize <= ZL_PIVCO_MAX_BLOCK_SIZE.
 */
size_t ZL_PivCoHuffmanEncode_bound(size_t srcSize, size_t blockSize);

/**
 * Encodes @p src using zstd-style Huffman weights and returns the number of
 * bytes written to @p dst. The weights are not emitted into @p dst; the decoder
 * must receive the same weights separately.
 *
 * @param kernels The kernel to use for encoding or NULL to use the default.
 *
 * @param blockSize The number of input bytes coded per pivco block. Must
 * match the block size the decoder uses. srcSize > blockSize produces multiple
 * blocks.
 *
 * @pre blockSize > 0 && blockSize <= ZL_PIVCO_MAX_BLOCK_SIZE.
 * @pre scratchElements >=
 *      ZL_PivCoHuffmanEncode_scratchElements(srcSize, blockSize).
 * @pre weights describes a valid zstd-style Huffman weight table.
 * @pre tableLog == ZL_PivCoHuffman_computeTableLog(weights, weightsSize).
 * @pre Every symbol in src is less than weightsSize and has non-zero weight.
 *
 * @returns The encoded size in bytes or SIZE_MAX upon failure.
 */
size_t ZL_PivCoHuffman_encode(
        uint8_t* dst,
        size_t dstCapacity,
        uint8_t* scratch,
        size_t scratchElements,
        const uint8_t* weights,
        size_t weightsSize,
        int tableLog,
        const uint8_t* src,
        size_t srcSize,
        size_t blockSize,
        const ZL_PivCoHuffmanEncode* kernels);

ZL_END_C_DECLS

#endif
