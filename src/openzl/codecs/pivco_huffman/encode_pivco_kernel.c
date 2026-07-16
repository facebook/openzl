// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/encode_pivco_kernel.h"

#include <assert.h>
#include <string.h>

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/codecs/pivco_huffman/common_pivco_kernel.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/utils.h"

size_t ZL_PivCoHuffmanEncode_scratchElements(size_t srcSize, size_t blockSize)
{
    blockSize = ZL_MIN(srcSize, blockSize);
    return 2 * (blockSize + ZL_PIVCO_HUFFMAN_SLOP);
}

size_t ZL_PivCoHuffmanEncode_bound(size_t srcSize, size_t blockSize)
{
    if (srcSize == 0) {
        return 0;
    }

    assert(blockSize > 0);
    assert(blockSize <= ZL_PIVCO_MAX_BLOCK_SIZE);

    // Worst-case size: the code data (at most ZL_PIVCO_MAX_TABLE_LOG bits per
    // symbol) plus fixed per-node overhead for every node in every block, plus
    // the kernels' over-write slop.
    blockSize              = ZL_MIN(srcSize, blockSize);
    const size_t blocks    = srcSize / blockSize + (srcSize % blockSize != 0);
    const size_t countBits = (size_t)ZL_nextPow2(blockSize + 1);
    const size_t overheadBitsPerNode = 7 + countBits;
    const size_t overheadBitsPerBlock =
            overheadBitsPerNode * ZL_PIVCO_MAX_TREE_NODES;
    const size_t overheadBits  = blocks * overheadBitsPerBlock;
    const size_t overheadBytes = (overheadBits + 7) / 8;

    // Avoid a multiply by 8, so that if the bound overflows a size_t, we know
    // the true-bound is >= SIZE_MAX.
    assert(ZL_PIVCO_MAX_TABLE_LOG == 8 + 4);
    const size_t dataBytes = srcSize + srcSize / 2;

    const size_t bound = dataBytes + overheadBytes + ZL_PIVCO_HUFFMAN_SLOP;

    if (bound < srcSize) {
        return SIZE_MAX;
    } else {
        return bound;
    }
}

/**
 * Writes @p value (which must be in [0, cap]) to @p writer using the fixed
 * number of bits needed to represent @p cap, then flushes. The width depends
 * only on @p cap, so the decoder recovers it without it being stored.
 */
static void
bitWriterEncodeCappedValue(ZS_BitCStreamFF* writer, size_t value, size_t cap)
{
    assert(value <= cap);
    const size_t numBits = (size_t)ZL_nextPow2(cap + 1);
    assert(numBits <= ZS_BITSTREAM_WRITE_MAX_BITS);
    ZS_BitCStreamFF_write(writer, value, numBits);
    ZS_BitCStreamFF_flush(writer);
}

/**
 * Reserves a byte-aligned region of @p numBits bits in @p writer for a bitmap
 * the caller writes directly. @returns A pointer to the region, or NULL if it
 * would not leave at least ZL_PIVCO_HUFFMAN_SLOP trailing bytes (which the
 * kernels may over-write) -- i.e. the output buffer is exhausted.
 */
static uint8_t* bitWriterReserveBitmap(ZS_BitCStreamFF* writer, size_t numBits)
{
    uint8_t* const bitmap = ZS_BitCStreamFF_reserveAlignedBits(writer, numBits);
    if (bitmap == NULL) {
        return NULL;
    }

    size_t const numBytes  = (numBits + 7) / 8;
    size_t const available = (size_t)(writer->end - bitmap);
    if (available - numBytes < ZL_PIVCO_HUFFMAN_SLOP) {
        return NULL;
    }

    return bitmap;
}

/**
 * Translates @p numSymbols source @p symbols into ranks via @p symbolToRank,
 * writing the result to @p ranks. This is the per-symbol mapping the rest of
 * the encoder operates on.
 * @pre Every source symbol is present in the tree (has a non-zero weight).
 */
static void buildRankStream(
        uint8_t* restrict ranks,
        const uint8_t* restrict symbols,
        size_t numSymbols,
        const uint8_t* restrict symbolToRank)
{
    for (size_t i = 0; i < numSymbols; ++i) {
        ranks[i] = symbolToRank[symbols[i]];
    }
}

/**
 * Recursively encodes the pivco-tree node covering ranks [firstRank, rankEnd)
 * at @p level into @p writer. @p nodeRanks holds the node's @p numRanks ranks
 * (in input order); @p nodeScratch is working space for partitioning.
 *
 * A leaf node emits a flat bitmap (or nothing, for a constant leaf). An
 * internal node partitions its ranks at the split rank into a 0-bit (left) and
 * 1-bit (right) group, writes the partition bitmap and -- unless both children
 * are constant -- the 1-bit count, then recurses into each non-constant child.
 * The children reuse @p nodeRanks and @p nodeScratch as their own rank/scratch
 * buffers without overlapping.
 *
 * @returns true on success, false if the output buffer is exhausted.
 */
static bool encodeNode(
        const ZL_PivCoHuffmanTree* tree,
        const ZL_PivCoHuffmanEncode* kernels,
        ZS_BitCStreamFF* writer,
        uint8_t* nodeRanks,
        uint8_t* nodeScratch,
        size_t numRanks,
        size_t level,
        size_t firstRank,
        size_t rankEnd)
{
    if (ZL_PivCoHuffmanTree_rangeIsLeaf(tree, firstRank, rankEnd)) {
        const size_t depth = ZL_PivCoHuffmanTree_leafFlatDepth(tree, firstRank);
        // A constant (depth-0) leaf emits nothing -- the weights alone identify
        // the symbol. A flat leaf packs `depth` bits per rank into a bitmap.
        if (depth != 0) {
            uint8_t* const bitmap =
                    bitWriterReserveBitmap(writer, numRanks * depth);
            if (bitmap == NULL) {
                return false;
            }
            kernels->packFlatDepth(
                    bitmap, depth, nodeRanks, numRanks, (uint8_t)firstRank);
            ZS_BitCStreamFF_commitReservedBits(writer);
        }
        return true;
    }

    // Internal node: split [firstRank, rankEnd) at splitRank into a left
    // (0-bit) and right (1-bit) group, partition the ranks accordingly, and
    // emit the partition bitmap (a constant side needs no separate rank
    // buffer).
    size_t const splitRank =
            ZL_PivCoHuffmanTree_splitRank(tree, level, firstRank, rankEnd);
    bool const lhsIsConstant =
            ZL_PivCoHuffmanTree_rangeIsConstantLeaf(tree, firstRank, splitRank);
    bool const rhsIsConstant =
            ZL_PivCoHuffmanTree_rangeIsConstantLeaf(tree, splitRank, rankEnd);

    uint8_t* const lhsRanks = nodeScratch;
    uint8_t* const rhsRanks = lhsIsConstant ? nodeScratch : nodeRanks;

    uint8_t* const bitmap = bitWriterReserveBitmap(writer, numRanks);
    if (bitmap == NULL) {
        return false;
    }

    size_t numOnes = 0;
    if (lhsIsConstant && rhsIsConstant) {
        // Both children are constant leaves; the decoder recovers the partition
        // straight from the bitmap, so numOnes is neither computed nor written.
        kernels->partitionNone(bitmap, nodeRanks, numRanks, (uint8_t)splitRank);
    } else if (lhsIsConstant) {
        numOnes = kernels->partitionRight(
                bitmap, rhsRanks, nodeRanks, numRanks, (uint8_t)splitRank);
    } else if (rhsIsConstant) {
        numOnes = kernels->partitionLeft(
                bitmap, lhsRanks, nodeRanks, numRanks, (uint8_t)splitRank);
    } else {
        numOnes = kernels->partitionFull(
                bitmap,
                lhsRanks,
                rhsRanks,
                nodeRanks,
                numRanks,
                (uint8_t)splitRank);
    }
    ZS_BitCStreamFF_commitReservedBits(writer);

    if (!(lhsIsConstant && rhsIsConstant)) {
        bitWriterEncodeCappedValue(writer, numOnes, numRanks);
    }

    // Hand each child a working buffer carved from this node's two buffers: the
    // partitioned ranks live in one, so each child's scratch is the unused tail
    // of the other (or all of nodeRanks when its sibling is constant).
    const size_t numZeros     = numRanks - numOnes;
    uint8_t* const lhsScratch = rhsIsConstant ? nodeRanks : rhsRanks + numOnes;
    uint8_t* const rhsScratch = lhsIsConstant ? nodeRanks : lhsRanks + numZeros;

    bool success = true;
    if (!lhsIsConstant) {
        success &= encodeNode(
                tree,
                kernels,
                writer,
                lhsRanks,
                lhsScratch,
                numZeros,
                level + 1,
                firstRank,
                splitRank);
    }
    if (!rhsIsConstant) {
        success &= encodeNode(
                tree,
                kernels,
                writer,
                rhsRanks,
                rhsScratch,
                numOnes,
                level + 1,
                splitRank,
                rankEnd);
    }
    return success;
}

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
        const ZL_PivCoHuffmanEncode* kernels)
{
    if (srcSize == 0 || tableLog == 0) {
        // Empty or constant input ==> empty output
        return 0;
    }

    assert(srcSize != 0);
    assert(blockSize != 0);
    assert(blockSize <= ZL_PIVCO_MAX_BLOCK_SIZE);
    assert(weightsSize != 0);
    assert(weightsSize <= ZL_PIVCO_MAX_SYMBOLS);
    assert(tableLog > 0 && tableLog <= ZL_PIVCO_MAX_TABLE_LOG);
    assert(tableLog == ZL_PivCoHuffman_computeTableLog(weights, weightsSize));

    if (scratchElements
        < ZL_PivCoHuffmanEncode_scratchElements(srcSize, blockSize)) {
        return SIZE_MAX;
    }

    if (kernels == NULL) {
        kernels = ZL_PivCoHuffmanEncode_select(NULL);
    }

    ZL_PivCoHuffmanTree tree;
    ZL_PivCoHuffmanTree_build(&tree, weights, weightsSize, tableLog);
    assert(tree.numRanks != 0);

    // The largest block is min(srcSize, blockSize) bytes long.
    blockSize = ZL_MIN(srcSize, blockSize);

    // Scratch is split into the per-block rank stream and the recursion's
    // partitioning workspace, each cappedBlock + SLOP bytes.
    uint8_t* const ranks       = scratch;
    uint8_t* const nodeScratch = scratch + blockSize + ZL_PIVCO_HUFFMAN_SLOP;

    ZS_BitCStreamFF writer = ZS_BitCStreamFF_init(dst, dstCapacity);

    for (size_t off = 0; off < srcSize; off += blockSize) {
        const size_t blockLen = ZL_MIN(blockSize, srcSize - off);
        buildRankStream(ranks, src + off, blockLen, tree.symbolToRank);
        const bool success = encodeNode(
                &tree,
                kernels,
                &writer,
                ranks,
                nodeScratch,
                blockLen,
                0,
                0,
                tree.numRanks);
        if (!success) {
            return SIZE_MAX;
        }
    }

    const ZL_Report ret = ZS_BitCStreamFF_finish(&writer);
    if (ZL_isError(ret)) {
        return SIZE_MAX;
    }
    const size_t dstSize = ZL_validResult(ret);
    assert(dstSize != 0);
    assert(dstSize + ZL_PIVCO_HUFFMAN_SLOP <= dstCapacity);
    return dstSize;
}
