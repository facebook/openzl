// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_PIVCO_COMMON_PIVCO_KERNEL_H
#define OPENZL_CODECS_PIVCO_COMMON_PIVCO_KERNEL_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/**
 * The PivCo-Huffman coding tree.
 *
 * The tree is the structure shared by the encoder and decoder. It is derived
 * deterministically from the zstd-style Huffman `weights` alone (it is never
 * serialized), so both sides build an identical tree from the same weights.
 *
 * Concepts used throughout this codec:
 *
 *  - weight: a zstd Huffman weight. For a symbol with code length `L` under a
 *    table of `tableLog` bits, weight = `tableLog + 1 - L`. A weight of 0 means
 *    the symbol is absent. Larger weight == shorter code == closer to the root.
 *
 *  - rank: symbols are ordered canonically (shortest codes first), and a
 *    symbol's rank is its index in that order. PivCo coding operates on
 *    contiguous rank ranges rather than on symbols directly, and the tree is
 *    stored entirely as rank-indexed arrays.
 *
 *  - leaf: each present symbol starts as a single-symbol leaf. A run of leaves
 *    that share the same code length may be collapsed into one multi-symbol
 *    "flat leaf" of `2^flatDepth` symbols, which the codec processes with a
 *    flat fixed-width bitmap instead of a chain of binary splits. A leaf
 *    occupies a contiguous rank range; flattening happens only while building
 *    (see the .c) and leaves no leaf objects in the tree.
 *
 *  - level: the depth at which pivco coding splits. At each level a rank
 *    range is partitioned into the codewords whose bit at that level is 0 vs 1;
 *    ZL_PivCoHuffmanTree_splitRank finds that partition point from the
 *    per-rank codewords (`rankToCodeword`).
 */

/**
 * Default bytes of output per pivco block. The block size is a parameter to
 * the encoder/decoder kernels; this is the value the binding uses and records
 * in the codec header when the input spans more than one block. It can be
 * changed at any time without breaking the wire format, and is just a
 * recommended default value.
 */
#define ZL_PIVCO_DEFAULT_BLOCK_SIZE ((size_t)(32 * 1024))
/**
 * Blocks larger than this are disallowed by the encoder and decoder.
 * The encoder asserts the contract is respected, and the decoder validates the
 * block size does not exceed this bound.
 *
 * The bound is chosen so that manipulating bits based on this number of bytes
 * is still comfortably below overlowing a U32.
 */
#define ZL_PIVCO_MAX_BLOCK_SIZE ((size_t)1 << 28)
#define ZL_PIVCO_MAX_SYMBOLS 256
#define ZL_PIVCO_MAX_TABLE_LOG 12
/**
 * The shallowest leaf (the flat root) sits one level above the longest code,
 * so weights run 1..tableLog+1.
 */
#define ZL_PIVCO_MAX_WEIGHT (ZL_PIVCO_MAX_TABLE_LOG + 1)
/**
 * Max nodes in the pivot tree: a binary tree with up to ZL_PIVCO_MAX_SYMBOLS
 * leaves has at most 2 * ZL_PIVCO_MAX_SYMBOLS - 1 nodes. The encoder uses
 * this to bound per-block overhead.
 */
#define ZL_PIVCO_MAX_TREE_NODES (2 * ZL_PIVCO_MAX_SYMBOLS - 1)

typedef struct ZL_PivCoHuffmanTree_s {
    /** symbol -> rank. Used by the encoder to map source bytes to ranks. */
    uint8_t symbolToRank[ZL_PIVCO_MAX_SYMBOLS];
    /**
     * rank -> the rank's symbol. Within a leaf, ranks are in flat order, so a
     * leaf's symbols are a contiguous slice of this array.
     */
    uint8_t rankToSymbol[ZL_PIVCO_MAX_SYMBOLS];
    /**
     * rank -> the flat depth (log2 of the symbol count) of the leaf containing
     * that rank. A leaf starting at `r` therefore spans `1 <<
     * rankToFlatDepth[r]` ranks, which is how a leaf is distinguished from an
     * internal node.
     */
    uint8_t rankToFlatDepth[ZL_PIVCO_MAX_SYMBOLS];
    /**
     * rank -> the rank's canonical codeword, left-justified (MSB-aligned) into
     * 16 bits. Sorted by rank, so splitRank can scan it for a level's 0/1 bit
     * boundary.
     */
    uint16_t rankToCodeword[ZL_PIVCO_MAX_SYMBOLS];
    /** Number of levels with at least one leaf (tree depth). */
    uint16_t numLevels;
    /** Number of ranks == number of present symbols. */
    uint16_t numRanks;
    /** Huffman table log: the longest code is `tableLog` bits (see weight). */
    int tableLog;
} ZL_PivCoHuffmanTree;

/**
 * @returns Whether the rank range [firstRank, rankEnd) is exactly one leaf
 * (a base case for the recursive encode/decode), as opposed to an internal node
 * spanning multiple leaves.
 *
 * @pre The range is a valid tree-node range: 0 <= firstRank < rankEnd <=
 * numRanks, and firstRank is a leaf boundary. Every caller derives the range
 * from the tree itself (`splitRank` / `numRanks`), which guarantees this; the
 * range never comes from untrusted bitstream data.
 */
ZL_INLINE bool ZL_PivCoHuffmanTree_rangeIsLeaf(
        const ZL_PivCoHuffmanTree* tree,
        size_t firstRank,
        size_t rankEnd)
{
    // The leaf starting at firstRank spans 2^flatDepth ranks; the range is that
    // leaf iff it has exactly that length. A longer range spans into the next
    // leaf, and (per the precondition) sub-leaf ranges never occur.
    return ((size_t)1 << tree->rankToFlatDepth[firstRank])
            == rankEnd - firstRank;
}

/**
 * @returns The flat depth (log2 of the symbol count) of the leaf starting at
 * @p firstRank; 0 for a constant (single-symbol) leaf.
 * @pre firstRank is a leaf boundary.
 */
ZL_INLINE size_t ZL_PivCoHuffmanTree_leafFlatDepth(
        const ZL_PivCoHuffmanTree* tree,
        size_t firstRank)
{
    return tree->rankToFlatDepth[firstRank];
}

/**
 * @returns Whether the rank range [firstRank, rankEnd) is a single constant
 * (single-symbol) leaf, which contributes nothing to the bitstream.
 * @pre The range is a valid tree-node range (see rangeIsLeaf).
 */
ZL_INLINE bool ZL_PivCoHuffmanTree_rangeIsConstantLeaf(
        const ZL_PivCoHuffmanTree* tree,
        size_t firstRank,
        size_t rankEnd)
{
    return ZL_PivCoHuffmanTree_rangeIsLeaf(tree, firstRank, rankEnd)
            && ZL_PivCoHuffmanTree_leafFlatDepth(tree, firstRank) == 0;
}

/**
 * @returns The symbols of the leaf starting at @p firstRank, in flat order
 * (`1 << leafFlatDepth` of them, laid out contiguously in rank order).
 * @pre firstRank is a leaf boundary.
 */
ZL_INLINE const uint8_t* ZL_PivCoHuffmanTree_leafSymbols(
        const ZL_PivCoHuffmanTree* tree,
        size_t firstRank)
{
    return &tree->rankToSymbol[firstRank];
}

/**
 * @returns The split rank of the internal node covering [firstRank, rankEnd) at
 * @p level: the first rank whose codeword has bit @p level set, i.e. the
 * boundary between the node's 0-bit (left) and 1-bit (right) children.
 * @pre The node is an internal node (not a single leaf), so
 * firstRank + 1 < rankEnd <= numRanks.
 */
ZL_INLINE uint16_t ZL_PivCoHuffmanTree_splitRank(
        const ZL_PivCoHuffmanTree* tree,
        size_t level,
        size_t firstRank,
        size_t rankEnd)
{
    (void)rankEnd;
    assert(level < tree->numLevels);
    assert(firstRank + 1 < rankEnd);
    assert(rankEnd <= tree->numRanks);

    // Codewords are MSB-aligned, so level L's bit is the (L+1)-th from the top.
    // Ranks are in canonical (codeword) order, so within the range bit `level`
    // reads 0 over a prefix then 1 over the rest; scan for the first 1. The
    // scan is short -- shorter (higher-weight) codewords are visited first --
    // and the range is tiny, so a linear scan beats a binary search.
    uint16_t const mask = (uint16_t)(0x8000u >> level);
    assert((tree->rankToCodeword[firstRank] & mask) == 0);

    size_t splitRank = firstRank + 1;
    while ((tree->rankToCodeword[splitRank] & mask) == 0) {
        assert(splitRank < rankEnd);
        ++splitRank;
    }
    assert(firstRank < splitRank);
    assert(splitRank < rankEnd);
    return (uint16_t)splitRank;
}

/**
 * Builds a tree from validated weights.
 * @pre tableLog == ZL_PivCoHuffman_computeTableLog(weights, weightsSize)
 */
void ZL_PivCoHuffmanTree_build(
        ZL_PivCoHuffmanTree* tree,
        const uint8_t* weights,
        size_t weightsSize,
        int tableLog);

/**
 * @returns The table log of the Huffman tree described by @p weights or -1 if
 * the weights are invalid.
 */
int ZL_PivCoHuffman_computeTableLog(const uint8_t* weights, size_t weightsSize);

/**
 * Efficiently counts weight frequency and outputs to @p weightCounts.
 *
 * @pre numWeights <= 256
 */
void ZL_PivCoHuffman_countWeights(
        uint16_t weightCounts[16],
        const uint8_t* weights,
        size_t numWeights);

ZL_END_C_DECLS

#endif
