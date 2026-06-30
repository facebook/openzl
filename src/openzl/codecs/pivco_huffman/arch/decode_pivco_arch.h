// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_PIVCO_HUFFMAN_ARCH_DECODE_PIVCO_ARCH_H
#define OPENZL_CODECS_PIVCO_HUFFMAN_ARCH_DECODE_PIVCO_ARCH_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/codecs/pivco_huffman/arch/common_pivco_arch.h"
#include "openzl/shared/cpu.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

typedef struct {
    /**
     * @returns true iff the CPU supports this implementation.
     * @note The rest of the function pointers may be NULL if not supported.
     */
    bool (*supported)(const ZL_cpuid_t* cpuid);

    /**
     * Merges @p lhs and @p rhs into @p out based on the bits in @p bitmap.
     *
     * @param outCapacity    Must be at least `lhsSize + rhsSize` bytes, but may
     *                       be larger to indicate that over-writes are okay.
     * @param bitmapCapacity Must be at least `(lhsSize + rhsSize + 7) / 8`
     *                       bytes, but may be larger to indicate that
     *                       over-reads are okay.
     * @param lhs            Must be at least `lhsSize + SLOP` bytes,
     *                       where the first @p lhsSize bytes are valid.
     * @param rhs            Must be at least `rhsSize + SLOP` bytes,
     *                       where the first @p rhsSize bytes are valid.
     *
     * @returns The number of ones in the bitmap. If this is not equal to
     * @p rhsSize then the data was corrupt, and the output is unspecified.
     */
    size_t (*mergeVectorVector)(
            uint8_t* out,
            size_t outCapacity,
            const uint8_t* bitmap,
            size_t bitmapCapacity,
            const uint8_t* lhs,
            size_t lhsSize,
            const uint8_t* rhs,
            size_t rhsSize);

    /**
     * Merges @p lhs and @p rhs into @p out based on the bits in @p bitmap,
     * where @p lhs is a constant.
     *
     * @param outCapacity    Must be at least `lhsSize + rhsSize` bytes, but may
     *                       be larger to indicate that over-writes are okay.
     * @param bitmapCapacity Must be at least `(lhsSize + rhsSize + 7) / 8`
     *                       bytes, but may be larger to indicate that
     *                       over-reads are okay.
     * @param rhs            Must be at least `rhsSize + SLOP` bytes,
     *                       where the first @p rhsSize bytes are valid.
     *
     * @returns The number of ones in the bitmap. If this is not equal to
     * @p rhsSize then the data was corrupt, and the output is unspecified.
     */
    size_t (*mergeConstantVector)(
            uint8_t* out,
            size_t outCapacity,
            const uint8_t* bitmap,
            size_t bitmapCapacity,
            uint8_t lhs,
            size_t lhsSize,
            const uint8_t* rhs,
            size_t rhsSize);

    /**
     * Merges @p lhs and @p rhs into @p out based on the bits in @p bitmap,
     * where @p rhs is a constant.
     *
     * @param outCapacity    Must be at least `lhsSize + rhsSize` bytes, but may
     *                       be larger to indicate that over-writes are okay.
     * @param bitmapCapacity Must be at least `(lhsSize + rhsSize + 7) / 8`
     *                       bytes, but may be larger to indicate that
     *                       over-reads are okay.
     * @param lhs            Must be at least `lhsSize + SLOP` bytes,
     *                       where the first @p lhsSize bytes are valid.
     *
     * @returns The number of ones in the bitmap. If this is not equal to
     * @p rhsSize then the data was corrupt, and the output is unspecified.
     */
    size_t (*mergeVectorConstant)(
            uint8_t* out,
            size_t outCapacity,
            const uint8_t* bitmap,
            size_t bitmapCapacity,
            const uint8_t* lhs,
            size_t lhsSize,
            uint8_t rhs,
            size_t rhsSize);

    /**
     * Reads @p outSize packed indices of @p depth bits each from @p bitmap and
     * fills @p out with `symbols[idx]` for each packed index.
     *
     * @param out            Buffer of size @p outCapacity
     * @param outSize        Number of output symbols
     * @param outCapacity    Must be at least @p outSize. It can be larger to
     *                       indicate that over-writing is okay
     * @param bitmapCapacity Must be at least `(outSize * depth + 7) / 8` bytes,
     *                       but may be larger to indicate that over-reads are
     *                       okay
     * @param depth          The number of bits each index in @p bitmap takes
     * @param symbols        Must be @p 2^depth bytes large
     */
    void (*mergeFlatDepth)(
            uint8_t* out,
            size_t outSize,
            size_t outCapacity,
            const uint8_t* bitmap,
            size_t bitmapCapacity,
            size_t depth,
            const uint8_t* symbols);
} ZL_PivCoHuffmanDecode;

/**
 * @returns The best kernel for the CPU.
 */
const ZL_PivCoHuffmanDecode* ZL_PivCoHuffmanDecode_select(
        const ZL_cpuid_t* cpuid);

extern const ZL_PivCoHuffmanDecode ZL_PivCoHuffmanDecode_generic;
extern const ZL_PivCoHuffmanDecode ZL_PivCoHuffmanDecode_avx512;

ZL_END_C_DECLS

#endif
