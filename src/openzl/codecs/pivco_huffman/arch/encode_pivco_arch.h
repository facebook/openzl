// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_PIVCO_HUFFMAN_ARCH_ENCODE_PIVCO_ARCH_H
#define OPENZL_CODECS_PIVCO_HUFFMAN_ARCH_ENCODE_PIVCO_ARCH_H

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
     * Partitions @p ranks into @p lhs and @p rhs based on @p rightRank.
     * If the rank is below @p rightRank, then it goes to @p lhs, otherwise it
     * goes to @p rhs. The partition decision is for each rank is written
     * as a bit in @p bitmap, 0 for left and 1 for right.
     *
     * @param bitmap Must be `(numRanks + 7) / 8 + SLOP` bytes
     * @param lhs    Must be `numRanks + SLOP` elements large
     * @param rhs    Must be `numRanks + SLOP` elements large
     * @param ranks  Must be `numRanks + SLOP` elements large,
     *               where the first @p numRanks elements are valid.
     *
     * @returns the number of ones in the bitmap
     *
     * @note @p lhs or @p rhs may alias @p ranks
     */
    size_t (*partitionFull)(
            uint8_t* bitmap,
            uint8_t* lhs,
            uint8_t* rhs,
            const uint8_t* ranks,
            size_t numRanks,
            uint8_t rightRank);

    /**
     * Partitions @p ranks into @p lhs based on @p rightRank. If the rank is
     * below @p rightRank, then it goes to @p lhs. The partition decision is for
     * each rank is written as a bit in @p bitmap, 0 for left and 1 for
     * right.
     *
     * @param bitmap Must be `(numRanks + 7) / 8 + SLOP` bytes
     * @param lhs    Must be `numRanks + SLOP` elements large
     * @param ranks  Must be `numRanks + SLOP` elements large,
     *               where the first @p numRanks elements are valid.
     *
     * @returns the number of ones in the bitmap
     */
    size_t (*partitionLeft)(
            uint8_t* bitmap,
            uint8_t* lhs,
            const uint8_t* ranks,
            size_t numRanks,
            uint8_t rightRank);

    /**
     * Partitions @p ranks into @p rhs based on @p rightRank. If the rank is
     * at least @p rightRank, then it goes to @p rhs. The partition decision is
     * for each rank is written as a bit in @p bitmap, 0 for left and 1 for
     * right.
     *
     * @param bitmap Must be `(numRanks + 7) / 8 + SLOP` bytes
     * @param rhs    Must be `numRanks + SLOP` elements large
     * @param ranks  Must be `numRanks + SLOP` elements large,
     *               where the first @p numRanks elements are valid.
     *
     * @returns the number of ones in the bitmap
     */
    size_t (*partitionRight)(
            uint8_t* bitmap,
            uint8_t* rhs,
            const uint8_t* ranks,
            size_t numRanks,
            uint8_t rightRank);

    /**
     * Partitions @p ranks based on @p rightRank. If the rank is below
     * @p rightRank, then it goes to the left, otherwise it goes to the right.
     * That decision is written as a bit in @p bitmap, 0 for left and 1 for
     * right.
     *
     * @param bitmap Must be `(numRanks + 7) / 8 + SLOP` bytes
     * @param ranks  Must be `numRanks + SLOP` elements large,
     *               where the first @p numRanks elements are valid.
     */
    void (*partitionNone)(
            uint8_t* bitmap,
            const uint8_t* ranks,
            size_t numRanks,
            uint8_t rightRank);

    /**
     * Subtracts @p rankBegin from each rank in @p ranks, the result of which
     * must be at most @p depth bits, and stores it into @p bitmap.
     *
     * @param bitmap Must be `(numRanks * depth + 7) / 8 + SLOP` bytes
     * @param ranks  Must be `numRanks + SLOP` elements large,
     *               where the first @p numRanks elements are valid.
     */
    void (*packFlatDepth)(
            uint8_t* bitmap,
            size_t depth,
            const uint8_t* ranks,
            size_t numRanks,
            uint8_t rankBegin);
} ZL_PivCoHuffmanEncode;

/**
 * @returns The best kernel for the CPU.
 */
const ZL_PivCoHuffmanEncode* ZL_PivCoHuffmanEncode_select(
        const ZL_cpuid_t* cpuid);

extern const ZL_PivCoHuffmanEncode ZL_PivCoHuffmanEncode_generic;

ZL_END_C_DECLS

#endif
