// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/arch/decode_pivco_arch.h"

#include <assert.h>

#include "openzl/codecs/common/bitstream/ff_bitstream.h"

// Portable reference implementation of the PivCo-Huffman decode kernels.
//
// This file is written for clarity, not speed. It is used for correctness
// testing and as the fallback on hardware without a specialized kernel, which
// is not expected to run this code in practice. The architecture-specific
// kernels (x86, arm, avx512) are the fast paths and MUST decode the exact same
// bitmaps as the code here.
//
// Every bitmap is packed little-endian, least-significant-bit first -- exactly
// the layout ZS_BitDStreamFF reads -- so we lean on that primitive instead of
// hand-rolling the bit math. To keep the loops trivially correct we reload the
// 64-bit window after every element. That is wasteful but easy to follow, which
// is the point of this file.

const ZL_PivCoHuffmanDecode* ZL_PivCoHuffmanDecode_select(
        const ZL_cpuid_t* cpuid)
{
    (void)cpuid;
    return &ZL_PivCoHuffmanDecode_generic;
}

ZL_INLINE size_t bitmapBytes(size_t bits)
{
    return (bits + 7) / 8;
}

// One side of a merge: either a single byte repeated (a constant leaf) or a
// vector of decoded bytes consumed in order. `pos` counts how many have been
// taken so far.
typedef struct {
    const uint8_t* vec; // NULL for a constant source
    uint8_t constant;
    size_t size;
    size_t pos;
} MergeSource;

static MergeSource vectorSource(const uint8_t* vec, size_t size)
{
    return (MergeSource){ .vec = vec, .constant = 0, .size = size, .pos = 0 };
}

static MergeSource constantSource(uint8_t value, size_t size)
{
    return (MergeSource){
        .vec = NULL, .constant = value, .size = size, .pos = 0
    };
}

static uint8_t mergeSourceNext(MergeSource* source)
{
    uint8_t value;
    if (source->vec == NULL) {
        value = source->constant;
    } else if (source->pos < source->size) {
        value = source->vec[source->pos];
    } else {
        // More bits selected this source than it has bytes: the bitstream is
        // corrupt. Return a placeholder; the caller detects the corruption
        // because the returned one-count will not match the expected size.
        value = 0;
    }
    ++source->pos;
    return value;
}

// Reconstructs lhs.size + rhs.size bytes into @p out from a partition bitmap:
// bit i selects the right source (1) or the left source (0) for output position
// i, taking one byte from the chosen source. Returns the number of one bits.
static size_t mergeGeneric(
        uint8_t* out,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        MergeSource lhs,
        MergeSource rhs)
{
    const size_t outSize = lhs.size + rhs.size;
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= bitmapBytes(outSize));
    (void)outCapacity; // only read by the assert above

    ZS_BitDStreamFF bits = ZS_BitDStreamFF_init(bitmap, bitmapCapacity);

    size_t ones = 0;
    for (size_t i = 0; i < outSize; ++i) {
        const size_t bit = ZS_BitDStreamFF_read(&bits, 1);
        // Reload every bit so the 64-bit window never underflows.
        ZS_BitDStreamFF_reload(&bits);

        if (bit != 0) {
            out[i] = mergeSourceNext(&rhs);
            ++ones;
        } else {
            out[i] = mergeSourceNext(&lhs);
        }
    }
    return ones;
}

static size_t mergeVectorVector(
        uint8_t* out,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* lhs,
        size_t lhsSize,
        const uint8_t* rhs,
        size_t rhsSize)
{
    return mergeGeneric(
            out,
            outCapacity,
            bitmap,
            bitmapCapacity,
            vectorSource(lhs, lhsSize),
            vectorSource(rhs, rhsSize));
}

static size_t mergeConstantVector(
        uint8_t* out,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        uint8_t lhs,
        size_t lhsSize,
        const uint8_t* rhs,
        size_t rhsSize)
{
    return mergeGeneric(
            out,
            outCapacity,
            bitmap,
            bitmapCapacity,
            constantSource(lhs, lhsSize),
            vectorSource(rhs, rhsSize));
}

static size_t mergeVectorConstant(
        uint8_t* out,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* lhs,
        size_t lhsSize,
        uint8_t rhs,
        size_t rhsSize)
{
    return mergeGeneric(
            out,
            outCapacity,
            bitmap,
            bitmapCapacity,
            vectorSource(lhs, lhsSize),
            constantSource(rhs, rhsSize));
}

// Expands a flat leaf: reads one @p depth-bit index per output and looks it up
// in @p symbols (which has 2^depth entries).
static void mergeFlatDepth(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        size_t depth,
        const uint8_t* symbols)
{
    assert(depth >= 1);
    assert(depth <= 8);
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= bitmapBytes(outSize * depth));
    (void)outCapacity; // only read by the assert above

    ZS_BitDStreamFF bits = ZS_BitDStreamFF_init(bitmap, bitmapCapacity);

    for (size_t i = 0; i < outSize; ++i) {
        const size_t index = ZS_BitDStreamFF_read(&bits, depth);
        // Reload every index so the 64-bit window never underflows.
        ZS_BitDStreamFF_reload(&bits);
        out[i] = symbols[index];
    }
}

static bool supported(const ZL_cpuid_t* cpuid)
{
    (void)cpuid;
    return true;
}

const ZL_PivCoHuffmanDecode ZL_PivCoHuffmanDecode_generic = {
    .supported           = supported,
    .mergeVectorVector   = mergeVectorVector,
    .mergeConstantVector = mergeConstantVector,
    .mergeVectorConstant = mergeVectorConstant,
    .mergeFlatDepth      = mergeFlatDepth,
};
