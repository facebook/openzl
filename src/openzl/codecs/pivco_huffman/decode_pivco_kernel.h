// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_PIVCO_HUFFMAN_DECODE_PIVCO_KERNEL_H
#define OPENZL_CODECS_PIVCO_HUFFMAN_DECODE_PIVCO_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/codecs/pivco_huffman/arch/decode_pivco_arch.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/**
 * @returns The number of bytes required by the decoder scratch.
 */
size_t ZL_PivCoHuffmanDecode_scratchBytes(size_t dstSize, size_t blockSize);

/**
 * Decodes @p bitstream into @p dst using the same zstd-style Huffman weights
 * that were provided to the encoder. The weights are not carried in
 * @p bitstream; they must be supplied separately.
 *
 * @param scratch Working buffer of at least
 *      ZL_PivCoHuffmanDecode_scratchBytes(dstSize, blockSize) bytes.
 * @param blockSize The number of decoded bytes per pivco block; must equal
 * the block size the encoder used.
 * @param kernels The kernel to use for decoding or NULL to use the default.
 *
 * @returns true on success; false if @p scratchBytes is too small, @p blockSize
 *      is out of range, or the bitstream is corrupt.
 */
bool ZL_PivCoHuffman_decode(
        uint8_t* dst,
        size_t dstSize,
        uint8_t* scratch,
        size_t scratchBytes,
        const uint8_t* weights,
        size_t weightsSize,
        const uint8_t* bitstream,
        size_t bitstreamSize,
        size_t blockSize,
        const ZL_PivCoHuffmanDecode* kernels);

ZL_END_C_DECLS

#endif
