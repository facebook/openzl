// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/arch/encode_pivco_arch.h"

#include <assert.h>

#include "openzl/codecs/common/bitstream/ff_bitstream.h"

// Portable reference implementation of the PivCo-Huffman encode kernels.
//
// This file is written for clarity, not speed. It is used for correctness
// testing and as the fallback on hardware without a specialized kernel, which
// is not expected to run this code in practice. The architecture-specific
// kernels (x86, arm, avx512) are the fast paths and MUST produce byte-identical
// bitmaps to the code here.
//
// Every bitmap is packed little-endian, least-significant-bit first -- exactly
// the layout ZS_BitCStreamFF writes -- so we lean on that primitive instead of
// hand-rolling the bit math. To keep the loops trivially correct we flush the
// 64-bit window after every element. That is wasteful but easy to follow, which
// is the point of this file.

const ZL_PivCoHuffmanEncode* ZL_PivCoHuffmanEncode_select(
        const ZL_cpuid_t* cpuid)
{
    (void)cpuid;
    return &ZL_PivCoHuffmanEncode_generic;
}

static size_t bitmapBytes(size_t bits)
{
    return (bits + 7) / 8;
}

// Partitions @p ranks into a left group (rank < rightRank) and a right group
// (rank >= rightRank). Emits one bit per rank into @p bitmap (0 = left,
// 1 = right) and, when the corresponding output is non-NULL, copies each rank
// into @p lhs / @p rhs in order. Returns the number of right (1) bits.
static size_t partitionGeneric(
        uint8_t* bitmap,
        uint8_t* lhs,
        uint8_t* rhs,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    // The bitmap holds numRanks bits; the +SLOP matches the over-write budget
    // the kernel contract guarantees, leaving room for the bitstream's 8-byte
    // stores.
    ZS_BitCStreamFF out = ZS_BitCStreamFF_init(
            bitmap, bitmapBytes(numRanks) + ZL_PIVCO_HUFFMAN_SLOP);

    size_t ones  = 0;
    size_t zeros = 0;
    for (size_t i = 0; i < numRanks; ++i) {
        const uint8_t rank = ranks[i];
        const bool isRight = rank >= rightRank;

        ZS_BitCStreamFF_write(&out, isRight ? 1 : 0, 1);
        // Flush every bit so the accumulator never holds more than 8 bits.
        ZS_BitCStreamFF_flush(&out);

        if (isRight) {
            if (rhs != NULL) {
                rhs[ones] = rank;
            }
            ++ones;
        } else {
            if (lhs != NULL) {
                lhs[zeros] = rank;
            }
            ++zeros;
        }
    }
    (void)ZS_BitCStreamFF_finish(&out);
    return ones;
}

static size_t partitionLeft(
        uint8_t* bitmap,
        uint8_t* lhs,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    return partitionGeneric(bitmap, lhs, NULL, ranks, numRanks, rightRank);
}

static size_t partitionRight(
        uint8_t* bitmap,
        uint8_t* rhs,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    return partitionGeneric(bitmap, NULL, rhs, ranks, numRanks, rightRank);
}

static void partitionNone(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    (void)partitionGeneric(bitmap, NULL, NULL, ranks, numRanks, rightRank);
}

// Packs one @p depth-bit index per rank into @p bitmap. The index is the rank's
// offset from @p rankBegin (its position within a flat leaf) and must fit in
// @p depth bits.
static void packFlatDepth(
        uint8_t* bitmap,
        size_t depth,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rankBegin)
{
    assert(depth >= 1);
    assert(depth <= 8);

    ZS_BitCStreamFF out = ZS_BitCStreamFF_init(
            bitmap, bitmapBytes(numRanks * depth) + ZL_PIVCO_HUFFMAN_SLOP);

    for (size_t i = 0; i < numRanks; ++i) {
        const size_t index = (size_t)(ranks[i] - rankBegin);
        assert(index < ((size_t)1 << depth));

        ZS_BitCStreamFF_write(&out, index, depth);
        ZS_BitCStreamFF_flush(&out);
    }
    (void)ZS_BitCStreamFF_finish(&out);
}

static bool supported(const ZL_cpuid_t* cpuid)
{
    (void)cpuid;
    return true;
}

const ZL_PivCoHuffmanEncode ZL_PivCoHuffmanEncode_generic = {
    .supported      = supported,
    .partitionFull  = partitionGeneric,
    .partitionLeft  = partitionLeft,
    .partitionRight = partitionRight,
    .partitionNone  = partitionNone,
    .packFlatDepth  = packFlatDepth,
};
