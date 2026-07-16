// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/common_pivco_kernel.h"

#include <assert.h>
#include <string.h>

#include "openzl/shared/bits.h"
#include "openzl/shared/utils.h"

int ZL_PivCoHuffman_computeTableLog(const uint8_t* weights, size_t weightsSize)
{
    if (weightsSize == 0 || weightsSize > ZL_PIVCO_MAX_SYMBOLS) {
        return -1;
    }
    assert(weights != NULL);

    // A complete prefix code satisfies sum(2^(weight-1)) == 2^tableLog over the
    // non-zero weights. Accumulate that sum, masking the shift to stay defined
    // for the (rejected) out-of-range weights, and branchlessly track validity.
    bool invalid = false;
    uint32_t sum = 0;
    for (size_t i = 0; i < weightsSize; ++i) {
        const uint8_t w = weights[i];
        invalid |= w > ZL_PIVCO_MAX_TABLE_LOG;
        sum += (1u << (w & 31)) >> 1;
    }

    if (invalid) {
        return -1;
    }

    if (sum == 0 || !ZL_isPow2(sum)) {
        return -1;
    }

    const int tableLog = ZL_highbit32(sum);
    if (tableLog > ZL_PIVCO_MAX_TABLE_LOG) {
        return -1;
    }

    return tableLog;
}

void ZL_PivCoHuffman_countWeights(
        uint16_t weightCounts[16],
        const uint8_t* weights,
        size_t numWeights)
{
    assert(numWeights <= 256);
    ZL_STATIC_ASSERT(ZL_PIVCO_MAX_TABLE_LOG < 16, "Assumption");
#if ZL_HAS_SSSE3
    // A weight histogram has at most 16 buckets and typically heavy collisions,
    // so a scalar histogram serializes on a few hot counters. Instead keep all
    // 16 counts in one SIMD register: for each weight, compare it against the
    // lane indices 0..15 and subtract the (0/-1) match mask, incrementing the
    // matching lane.
    __m128i const iota =
            _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    __m128i count = _mm_setzero_si128();
    for (size_t i = 0; i < numWeights; ++i) {
        __m128i const inc =
                _mm_cmpeq_epi8(_mm_set1_epi8((char)weights[i]), iota);
        count = _mm_sub_epi8(count, inc);
    }
    // Lanes are 8-bit, so a count of exactly 256 wraps to 0. The only way every
    // lane reads 0 after 256 weights is a single bucket that wrapped: recover
    // it explicitly.
    bool const everyCountIsZero =
            _mm_movemask_epi8(_mm_cmpeq_epi8(count, _mm_setzero_si128()))
            == 0xFFFF;
    if (numWeights == 256 && everyCountIsZero) {
        memset(weightCounts, 0, sizeof(*weightCounts) * 16);
        weightCounts[weights[0]] = 256;
        return;
    }

    // Widen the 8-bit lane counts to the 16-bit output.
    const __m128i lo = _mm_unpacklo_epi8(count, _mm_setzero_si128());
    const __m128i hi = _mm_unpackhi_epi8(count, _mm_setzero_si128());
    _mm_storeu_si128((__m128i_u*)&weightCounts[0], lo);
    _mm_storeu_si128((__m128i_u*)&weightCounts[8], hi);
#else
    // Scalar fallback: four independent histograms unrolled over the input to
    // hide the load-update-store latency, then summed. uint8 counters cannot
    // overflow because each accumulates at most numWeights/4 < 256 increments.
    uint8_t weightCounts0[16] = { 0 };
    uint8_t weightCounts1[16] = { 0 };
    uint8_t weightCounts2[16] = { 0 };
    uint8_t weightCounts3[16] = { 0 };

    size_t const prefix = numWeights % 4;
    for (size_t i = 0; i < prefix; ++i) {
        weightCounts0[weights[i]] = (uint8_t)(weightCounts0[weights[i]] + 1);
    }
    for (size_t i = prefix; i < numWeights; i += 4) {
        weightCounts0[weights[i + 0]] =
                (uint8_t)(weightCounts0[weights[i + 0]] + 1);
        weightCounts1[weights[i + 1]] =
                (uint8_t)(weightCounts1[weights[i + 1]] + 1);
        weightCounts2[weights[i + 2]] =
                (uint8_t)(weightCounts2[weights[i + 2]] + 1);
        weightCounts3[weights[i + 3]] =
                (uint8_t)(weightCounts3[weights[i + 3]] + 1);
    }
    for (size_t i = 0; i < 16; ++i) {
        weightCounts[i] = (uint16_t)(weightCounts0[i] + weightCounts1[i]
                                     + weightCounts2[i] + weightCounts3[i]);
    }
#endif
}

/**
 * Worst-case leaf count: one single-symbol leaf per symbol, plus the flat
 * leaves. Flattening a weight bucket can add at most one flat leaf to each of
 * the weight levels above it, bounding the extra leaves by the triangular
 * number 1 + 2 + ... + ZL_PIVCO_MAX_WEIGHT.
 */
#define PIVCO_LEAF_STORAGE_SIZE \
    (ZL_PIVCO_MAX_SYMBOLS       \
     + ((ZL_PIVCO_MAX_WEIGHT * (ZL_PIVCO_MAX_WEIGHT + 1)) / 2))
/**
 * Each symbol is stored once for its single-symbol leaf and at most once more
 * inside a flat leaf.
 */
#define PIVCO_SYMBOL_STORAGE_SIZE (2 * ZL_PIVCO_MAX_SYMBOLS)

/**
 * A leaf under construction. It owns `numSymbols` symbols stored contiguously
 * at `symbolStorage + symbolOffset`. A single-symbol leaf has numSymbols == 1;
 * a flat leaf has a power-of-two numSymbols. Leaves exist only while building;
 * the finished tree is purely rank-indexed.
 *
 * A flat leaf copies its symbols into symbolStorage instead of referencing its
 * constituent single-symbol leaves in place. That is required: flattening a
 * lower weight can append a flat leaf onto those leaves' slots (see appendLeaf
 * reusing positions vacated by peeling), so the in-place symbols are not stable
 * once a leaf has been flattened.
 */
typedef struct {
    uint16_t symbolOffset;
    uint16_t numSymbols;
} ZL_PivCoLeaf;

/**
 * Mutable scratch for ZL_PivCoHuffmanTree_build. `leaves` is grouped by
 * weight: weight `w`'s leaves occupy [weightOffsets[w], weightOffsets[w + 1]),
 * and weightCounts[w] tracks how many of those slots are used. weightCounts
 * needs 16 lanes so countWeights can fill it with a single SIMD store.
 * `symbolStorage` backs the leaves' symbols and is bump-allocated by
 * `nextSymbol`.
 */
typedef struct {
    ZL_PivCoLeaf leaves[PIVCO_LEAF_STORAGE_SIZE];
    uint8_t symbolStorage[PIVCO_SYMBOL_STORAGE_SIZE];
    uint16_t weightCounts[16];
    uint16_t weightOffsets[ZL_PIVCO_MAX_WEIGHT + 2];
    uint16_t nextSymbol;
} ZL_PivCoBuilder;

/** @returns log2(numSymbols): the leaf's flat depth. */
static size_t leafFlatDepth(const ZL_PivCoLeaf* leaf)
{
    return (size_t)ZL_highbit32((uint32_t)leaf->numSymbols);
}

/**
 * Reserves @p count contiguous bytes in the builder's symbolStorage and
 * advances past them. @returns A pointer to the reserved bytes.
 */
static uint8_t* appendSymbols(ZL_PivCoBuilder* builder, size_t count)
{
    assert((int)count <= PIVCO_SYMBOL_STORAGE_SIZE - builder->nextSymbol);
    uint8_t* const symbols = builder->symbolStorage + builder->nextSymbol;
    builder->nextSymbol += (uint16_t)count;
    return symbols;
}

/**
 * Appends an (uninitialized) leaf to weight bucket @p weight, consuming one of
 * the slots reserved for that bucket by weightOffsets. @returns The new leaf,
 * for the caller to fill in.
 */
static ZL_PivCoLeaf* appendLeaf(ZL_PivCoBuilder* builder, int weight)
{
    assert(weight >= 0);
    assert(weight <= ZL_PIVCO_MAX_WEIGHT);

    size_t const pos = (size_t)builder->weightOffsets[weight]
            + builder->weightCounts[weight];
    assert(pos < builder->weightOffsets[weight + 1]);

    ZL_PivCoLeaf* const leaf = &builder->leaves[pos];
    ++builder->weightCounts[weight];
    return leaf;
}

/**
 * Collapses runs of equal-weight single-symbol leaves into flat leaves.
 *
 * `2^k` leaves of the same weight `w` (same code length) form a complete
 * subtree rooted `k` levels up, i.e. a single leaf of weight `w + k` holding
 * those `2^k` symbols. Decoding that leaf is a flat `k`-bit lookup instead of a
 * chain of binary pivots, so it is both smaller and faster.
 *
 * For each weight from shallowest (largest weight) to deepest (smallest
 * weight), repeatedly peel the largest power-of-two block (taken from the end
 * of the bucket) while more than two leaves remain; a residual of one or two is
 * left for the ordinary binary pivot, which a flat node could not improve on.
 * New flat leaves land in higher weight (shallower) buckets that have already
 * been processed, so they are never re-flattened.
 */
static void optimizeLeaves(ZL_PivCoBuilder* builder, int tableLog)
{
    for (int weight = tableLog; weight > 0; --weight) {
        while (builder->weightCounts[weight] > 2) {
            uint16_t const numWeights = builder->weightCounts[weight];
            int const flatBits        = ZL_highbit32(numWeights);
            size_t const flatNum      = (size_t)1 << flatBits;
            size_t const flatOff      = (size_t)numWeights - flatNum;
            int const flatWeight      = weight + flatBits;

            ZL_PivCoLeaf* const leaf = appendLeaf(builder, flatWeight);
            uint8_t* const symbols   = appendSymbols(builder, flatNum);

            // Copy the peeled suffix's symbols into the flat leaf now: a later,
            // lower-weight flattening can append over these single leaves'
            // slots, so they must be captured before that can happen.
            for (size_t i = 0; i < flatNum; ++i) {
                const ZL_PivCoLeaf* child =
                        &builder->leaves
                                 [builder->weightOffsets[weight] + flatOff + i];
                assert(child->numSymbols == 1);
                symbols[i] = builder->symbolStorage[child->symbolOffset];
            }

            leaf->symbolOffset = (uint16_t)(symbols - builder->symbolStorage);
            leaf->numSymbols   = (uint16_t)flatNum;
            builder->weightCounts[weight] -= (uint16_t)flatNum;
        }
    }
}

/**
 * Walks the builder's leaves in canonical order and fills the tree's
 * rank-indexed arrays (symbolToRank, rankToSymbol, rankToFlatDepth,
 * rankToCodeword) and numRanks.
 *
 * Leaves are visited shallowest-first (level 0 == the largest weight). Within a
 * level, canonical Huffman codewords are consecutive integers, so we keep a
 * running `codeword` counter that increments per leaf and shifts left by one
 * when descending a level. A flat leaf of depth `flatBits` occupies
 * `2^flatBits` consecutive ranks/codewords, one per contained symbol.
 */
static void assignRanksAndCodewords(
        ZL_PivCoHuffmanTree* tree,
        const ZL_PivCoBuilder* builder)
{
    uint32_t codeword = 0;
    uint16_t rank     = 0;
    for (size_t level = 0; level < tree->numLevels; ++level) {
        size_t const weight = (size_t)tree->tableLog + 1 - level;

        for (size_t idx = 0; idx < builder->weightCounts[weight]; ++idx) {
            size_t const leafIndex =
                    (size_t)builder->weightOffsets[weight] + idx;
            const ZL_PivCoLeaf* leaf = &builder->leaves[leafIndex];
            size_t const flatBits    = leafFlatDepth(leaf);
            size_t const totalBits   = level + flatBits;
            assert(totalBits <= 16);
            assert(rank <= ZL_PIVCO_MAX_SYMBOLS - leaf->numSymbols);

            const uint8_t* symbols =
                    builder->symbolStorage + leaf->symbolOffset;

            for (size_t flatIdx = 0; flatIdx < leaf->numSymbols; ++flatIdx) {
                uint8_t const symbol      = symbols[flatIdx];
                uint16_t const symbolRank = (uint16_t)(rank + flatIdx);
                // The symbol's codeword is the leaf's prefix followed by its
                // flat index, left-justified into 16 bits. totalBits == 0 only
                // for the single-symbol (constant) tree, which has no codeword
                // bits; special-cased to avoid a 16-bit shift by 16.
                uint32_t const bits =
                        (uint32_t)((codeword << flatBits) + flatIdx);
                uint16_t const symbolCodeword     = totalBits == 0
                            ? 0
                            : (uint16_t)(bits << (16 - totalBits));
                tree->symbolToRank[symbol]        = (uint8_t)symbolRank;
                tree->rankToSymbol[symbolRank]    = symbol;
                tree->rankToFlatDepth[symbolRank] = (uint8_t)flatBits;
                tree->rankToCodeword[symbolRank]  = symbolCodeword;
            }
            rank += leaf->numSymbols;

            ++codeword;
        }

        if (level + 1 < tree->numLevels) {
            codeword <<= 1;
        }
    }

    // Sanity check: a complete canonical code leaves the codeword counter at
    // exactly 2^(numLevels - 1) (one full code space at the deepest level).
    assert(codeword == ((uint32_t)1 << (tree->numLevels - 1)));
    assert(rank <= ZL_PIVCO_MAX_SYMBOLS);
    tree->numRanks = rank;
}

void ZL_PivCoHuffmanTree_build(
        ZL_PivCoHuffmanTree* tree,
        const uint8_t* weights,
        size_t weightsSize,
        int tableLog)
{
    assert(tree != NULL);
    assert(tableLog >= 0);
    assert(tableLog <= ZL_PIVCO_MAX_TABLE_LOG);
    assert(ZL_PivCoHuffman_computeTableLog(weights, weightsSize) == tableLog);

    memset(tree, 0, sizeof(*tree));
    tree->tableLog = tableLog;

    ZL_PivCoBuilder builder;
    builder.nextSymbol = 0;
    ZL_PivCoHuffman_countWeights(builder.weightCounts, weights, weightsSize);

    // Lay out `leaves` grouped by weight (a prefix sum over weightCounts).
    // Beyond its single-symbol leaves, a weight-w bucket reserves a gap of w
    // slots for flat leaves that flattening lower weights deposits here: each
    // lower weight w' < w contributes at most one (its peels have strictly
    // decreasing depth), so w slots always suffice.
    builder.weightOffsets[0] = 0;
    builder.weightOffsets[1] = 0;
    for (size_t weight = 2; weight < ZL_PIVCO_MAX_WEIGHT + 2; ++weight) {
        builder.weightOffsets[weight] =
                (uint16_t)(builder.weightOffsets[weight - 1]
                           + builder.weightCounts[weight - 1] + (weight - 1));
    }

    // Seed each present symbol as a single-symbol leaf in its weight bucket.
    // weightPos tracks the next free slot per bucket as we fill it.
    {
        uint16_t weightPos[ZL_PIVCO_MAX_WEIGHT + 2];
        memcpy(weightPos, builder.weightOffsets, sizeof(weightPos));
        for (size_t symbol = 0; symbol < weightsSize; ++symbol) {
            uint8_t const weight = weights[symbol];
            if (weight == 0) {
                continue;
            }

            ZL_PivCoLeaf* leaf     = &builder.leaves[weightPos[weight]++];
            uint8_t* const symbols = appendSymbols(&builder, 1);
            symbols[0]             = (uint8_t)symbol;
            leaf->symbolOffset = (uint16_t)(symbols - builder.symbolStorage);
            leaf->numSymbols   = 1;
        }
    }

    optimizeLeaves(&builder, tableLog);

    // The number of levels is fixed by the deepest non-empty level, which is
    // the smallest weight bucket that still has leaves (flattening can empty
    // out the deepest buckets). level == tableLog + 1 - weight, so the deepest
    // level + 1 == tableLog + 2 - minWeight.
    size_t minWeight = 1;
    while (builder.weightCounts[minWeight] == 0) {
        ++minWeight;
    }
    assert(minWeight <= (size_t)tableLog + 1);
    tree->numLevels = (uint16_t)((size_t)tableLog + 2 - minWeight);

    assignRanksAndCodewords(tree, &builder);
    assert(tree->numRanks != 0);
}
