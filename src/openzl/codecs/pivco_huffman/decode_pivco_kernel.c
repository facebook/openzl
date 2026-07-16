// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/decode_pivco_kernel.h"

#include <assert.h>
#include <string.h>

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/codecs/pivco_huffman/common_pivco_kernel.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/utils.h"

/**
 * The decoded result of a tree node. A constant node covers a single-symbol
 * leaf: the whole rank range is `symbol` and `output` is unused (the caller
 * fills the run). Otherwise the node's decoded bytes have been written to
 * `output`.
 */
typedef struct {
    bool isConstant;
    uint8_t symbol;
    uint8_t* output;
} ZL_PivCoHuffmanDecodeResult;

size_t ZL_PivCoHuffmanDecode_scratchBytes(size_t dstSize, size_t blockSize)
{
    // The recursion ping-pongs between two slop-padded buffers (see decodeNode)
    // so dst is never used as scratch -- only each block's root merge writes
    // into dst, honoring its exact (slop-free) capacity. The largest block is
    // min(dstSize, blockSize) bytes long, and each buffer holds one block plus
    // the kernels' over-write slop.
    blockSize = ZL_MIN(dstSize, blockSize);
    return 2 * (blockSize + ZL_PIVCO_HUFFMAN_SLOP);
}

/**
 * Reads a value that the encoder wrote with the fixed width needed to represent
 * @p cap (the width is derived from @p cap, not stored). @returns the value;
 * the caller must validate it against @p cap, since a corrupt stream can yield
 * a larger value.
 */
static size_t bitReaderDecodeCappedValue(ZS_BitDStreamFF* reader, size_t cap)
{
    size_t const numBits = (size_t)ZL_nextPow2(cap + 1);
    return ZS_BitDStreamFF_read(reader, numBits);
}

/**
 * Interleaves the two decoded children @p lhs and @p rhs into @p output (of
 * @p outputSize bytes, @p outputCapacity available) per @p bitmap: bit i picks
 * rhs (1) or lhs (0) for output position i. Each child is either a constant
 * symbol or a decoded vector, dispatched to the matching merge kernel; when
 * both are constant the bitmap alone reconstructs the output (no count was
 * sent).
 *
 * @returns true on success; false when a vector merge's one-count disagrees
 * with
 * @p rhsSize, which indicates a corrupt bitstream.
 */
static bool mergeDecodeResults(
        const ZL_PivCoHuffmanDecode* kernels,
        uint8_t* output,
        size_t outputSize,
        size_t outputCapacity,
        const uint8_t* bitmap,
        size_t bitmapBytes,
        const ZL_PivCoHuffmanDecodeResult* lhs,
        size_t lhsSize,
        const ZL_PivCoHuffmanDecodeResult* rhs,
        size_t rhsSize)
{
    if (lhs->isConstant && rhs->isConstant) {
        uint8_t symbols[2] = { lhs->symbol, rhs->symbol };
        kernels->mergeFlatDepth(
                output,
                outputSize,
                outputCapacity,
                bitmap,
                bitmapBytes,
                1,
                symbols);
        return true;
    }

    size_t ones;
    if (lhs->isConstant) {
        ones = kernels->mergeConstantVector(
                output,
                outputCapacity,
                bitmap,
                bitmapBytes,
                lhs->symbol,
                lhsSize,
                rhs->output,
                rhsSize);
    } else if (rhs->isConstant) {
        ones = kernels->mergeVectorConstant(
                output,
                outputCapacity,
                bitmap,
                bitmapBytes,
                lhs->output,
                lhsSize,
                rhs->symbol,
                rhsSize);
    } else {
        ones = kernels->mergeVectorVector(
                output,
                outputCapacity,
                bitmap,
                bitmapBytes,
                lhs->output,
                lhsSize,
                rhs->output,
                rhsSize);
    }

    return ones == rhsSize;
}

/**
 * Decodes the tree node covering ranks [firstRank, rankEnd) at @p level,
 * producing @p numCodewords output symbols, and writes the node's result to
 * @p out (capacity @p outCapacity).
 *
 * @p out is write-only: only this node's final merge touches it, so it may be
 * the exactly-sized destination buffer. The merge honors @p outCapacity, only
 * over-writing past the logical size when the capacity leaves room -- which it
 * never does for the last block written to dst.
 *
 * The subtree recurses entirely within the two slop-padded ping-pong buffers
 * @p scratch1 and @p scratch2 (capacities @p scratch1Capacity and
 * @p scratch2Capacity, each >= numCodewords + ZL_PIVCO_HUFFMAN_SLOP). This
 * node decodes its two children packed into @p scratch1 (lhs then rhs) using
 * @p scratch2 as their recursion buffer, then merges @p scratch1 into @p out.
 * The children recurse with scratch1/scratch2 rotated, so @p out is never
 * handed down as scratch -- the destination never doubles as recursion scratch,
 * and every recursive kernel always has slop in scratch1/scratch2 to over-write
 * into.
 *
 * @note that @p out and @p scratch2 may alias.
 */
static bool decodeNode(
        const ZL_PivCoHuffmanTree* tree,
        const ZL_PivCoHuffmanDecode* kernels,
        ZS_BitDStreamFF* reader,
        size_t level,
        size_t firstRank,
        size_t rankEnd,
        size_t numCodewords,
        uint8_t* out,
        size_t outCapacity,
        uint8_t* scratch1,
        size_t scratch1Capacity,
        uint8_t* scratch2,
        size_t scratch2Capacity,
        ZL_PivCoHuffmanDecodeResult* result)
{
    if (ZL_PivCoHuffmanTree_rangeIsLeaf(tree, firstRank, rankEnd)) {
        const uint8_t* const symbols =
                ZL_PivCoHuffmanTree_leafSymbols(tree, firstRank);
        const size_t depth = ZL_PivCoHuffmanTree_leafFlatDepth(tree, firstRank);
        // A depth-0 leaf is a single symbol: the whole range is constant and
        // nothing is read from the bitstream.
        if (depth == 0) {
            result->isConstant = true;
            result->symbol     = symbols[0];
            result->output     = NULL;
            return true;
        }

        // A flat leaf packs `depth` bits per output, indexing into `symbols`.
        // numCodewords is bounded by the block length (<= dstSize), so the
        // bit-count product never overflows for any realizable output.
        assert(numCodewords <= SIZE_MAX / depth);
        const size_t bitmapBits = numCodewords * depth;
        const uint8_t* const bitmap =
                ZS_BitDStreamFF_popAlignedBits(reader, bitmapBits);
        if (bitmap == NULL) {
            return false;
        }
        const size_t bitmapBytes = (bitmapBits + 7) / 8;
        kernels->mergeFlatDepth(
                out,
                numCodewords,
                outCapacity,
                bitmap,
                bitmapBytes,
                depth,
                symbols);
        result->isConstant = false;
        result->symbol     = 0;
        result->output     = out;
        return true;
    }
    // A non-leaf range deeper than the tree is corrupt (no split exists).
    if (level >= tree->numLevels) {
        return false;
    }

    // Internal node: it splits [firstRank, rankEnd) at splitRank into a left
    // (0-bit) and right (1-bit) child, recombined via the partition bitmap.
    const size_t splitRank =
            ZL_PivCoHuffmanTree_splitRank(tree, level, firstRank, rankEnd);
    bool const lhsIsConstant =
            ZL_PivCoHuffmanTree_rangeIsConstantLeaf(tree, firstRank, splitRank);
    bool const rhsIsConstant =
            ZL_PivCoHuffmanTree_rangeIsConstantLeaf(tree, splitRank, rankEnd);

    const uint8_t* const bitmap =
            ZS_BitDStreamFF_popAlignedBits(reader, numCodewords);
    if (bitmap == NULL) {
        return false;
    }
    const size_t bitmapBytes = (numCodewords + 7) / 8;

    // The encoder omits numOnes when both children are constant (the bitmap
    // alone reconstructs the output); otherwise it is read from the stream.
    size_t numOnes = 0;
    if (!(lhsIsConstant && rhsIsConstant)) {
        numOnes = bitReaderDecodeCappedValue(reader, numCodewords);
        if (numOnes > numCodewords) {
            return false;
        }
    }

    assert(numCodewords + ZL_PIVCO_HUFFMAN_SLOP <= scratch1Capacity);
    const size_t numZeros = numCodewords - numOnes;
    ZL_PivCoHuffmanDecodeResult lhs;
    ZL_PivCoHuffmanDecodeResult rhs;

    // The children are packed into `scratch1` (lhs at [0, numZeros), rhs at
    // [numZeros, numCodewords)). Each child recurses with scratch1/scratch2
    // rotated: it packs its own children into `scratch2` and uses its slice of
    // `scratch1` as its recursion buffer. `out` is never passed down, so the
    // destination is never used as recursion scratch.
    if (!decodeNode(
                tree,
                kernels,
                reader,
                level + 1,
                firstRank,
                splitRank,
                numZeros,
                scratch1,
                scratch1Capacity,
                scratch2,
                scratch2Capacity,
                scratch1,
                scratch1Capacity,
                &lhs)) {
        return false;
    }
    if (!decodeNode(
                tree,
                kernels,
                reader,
                level + 1,
                splitRank,
                rankEnd,
                numOnes,
                scratch1 + numZeros,
                scratch1Capacity - numZeros,
                scratch2,
                scratch2Capacity,
                scratch1 + numZeros,
                scratch1Capacity - numZeros,
                &rhs)) {
        return false;
    }

    if (!mergeDecodeResults(
                kernels,
                out,
                numCodewords,
                outCapacity,
                bitmap,
                bitmapBytes,
                &lhs,
                numZeros,
                &rhs,
                numOnes)) {
        return false;
    }

    result->isConstant = false;
    result->symbol     = 0;
    result->output     = out;
    return true;
}

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
        const ZL_PivCoHuffmanDecode* kernels)
{
    if (dstSize == 0) {
        return bitstreamSize == 0;
    }

    // For a non-empty output the block size bounds the decode loop (a zero
    // block size would never make progress) and the scratch sizing, so its
    // bounds must be validated -- it comes from the untrusted codec header.
    if (blockSize == 0 || blockSize > ZL_PIVCO_MAX_BLOCK_SIZE) {
        return false;
    }

    if (scratchBytes < ZL_PivCoHuffmanDecode_scratchBytes(dstSize, blockSize)) {
        return false;
    }

    if (kernels == NULL) {
        kernels = ZL_PivCoHuffmanDecode_select(NULL);
    }

    ZL_PivCoHuffmanTree tree;
    int const tableLog = ZL_PivCoHuffman_computeTableLog(weights, weightsSize);
    if (tableLog < 0) {
        return false;
    }
    ZL_PivCoHuffmanTree_build(&tree, weights, weightsSize, tableLog);
    assert(tree.numRanks != 0);

    ZS_BitDStreamFF reader = ZS_BitDStreamFF_init(bitstream, bitstreamSize);

    // Two slop-padded ping-pong buffers carved from scratch for writing the
    // intermediate outputs, while guaranteeing SLOP bytes are available for
    // over-read when merging them.
    const size_t bufCapacity =
            ZL_MIN(dstSize, blockSize) + ZL_PIVCO_HUFFMAN_SLOP;
    uint8_t* const scratch1 = scratch;
    uint8_t* const scratch2 = scratch + bufCapacity;

    for (size_t off = 0; off < dstSize; off += blockSize) {
        const size_t blockLen = ZL_MIN(blockSize, dstSize - off);
        ZL_PivCoHuffmanDecodeResult result;
        if (!decodeNode(
                    &tree,
                    kernels,
                    &reader,
                    0,
                    0,
                    tree.numRanks,
                    blockLen,
                    dst + off,
                    dstSize - off,
                    scratch1,
                    bufCapacity,
                    scratch2,
                    bufCapacity,
                    &result)) {
            return false;
        }

        // An internal or flat-leaf root merged its result straight into dst; a
        // constant root writes nothing, so fill it here.
        if (result.isConstant) {
            memset(dst + off, result.symbol, blockLen);
        }
    }

    // A well-formed bitstream is consumed exactly: any leftover bytes mean the
    // input was corrupt.
    const ZL_Report ret = ZS_BitDStreamFF_finish(&reader);
    if (ZL_isError(ret)) {
        return false;
    }
    if (ZL_validResult(ret) != bitstreamSize) {
        return false;
    }

    return true;
}
