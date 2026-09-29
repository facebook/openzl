// Copyright (c) Meta Platforms, Inc. and affiliates.

// The ARM decoding code is heavily inspired by upstream's implementation,
// see ./README.md for credits.

#include "openzl/codecs/pivco_huffman/arch/decode_pivco_arch.h"

#if ZL_ARCH_ARM64

#    include <arm_neon.h>

#    include "common_pivco_neon_tables.h"
#    include "openzl/shared/mem.h"

static bool supported(const ZL_cpuid_t* cpuid)
{
    (void)cpuid;
    return true;
}

static size_t bitmapBytes(size_t bits)
{
    return (bits + 7) / 8;
}

/**
 * Computes the rank prefix for one 64-output merge block from that block's 64
 * partition bits, passed as the 8 bitmap bytes in @p maskBytes.
 *
 * `vcnt_u8` replaces each bitmap byte with its popcount -- how many of that
 * byte's 8 outputs come from the right side. Moved back into a 64-bit word,
 * that is eight independent counts, one per byte lane. Multiplying by
 * 0x0101010101010101 adds every lane into all the lanes above it, so byte k of
 * the product holds `count[0] + ... + count[k]`: an inclusive prefix sum, for
 * the price of one multiply. No lane can carry into the next, because the
 * running sum tops out at 64 (8 bytes x 8 bits), far below 256.
 *
 * @returns Byte k = the number of one bits in bitmap bytes 0..k, i.e. how many
 * right-side bytes the block consumes before output `8 * (k + 1)`. Callers want
 * the ranks at 16-byte boundaries, so they read the odd bytes: shifts 8, 24 and
 * 40 give the rank at outputs 16, 32 and 48, and shift 56 gives the block
 * total.
 */
ZL_FORCE_INLINE uint64_t mergeRankPrefix(uint8x8_t maskBytes)
{
    const uint64_t counts =
            vget_lane_u64(vreinterpret_u64_u8(vcnt_u8(maskBytes)), 0);
    return counts * UINT64_C(0x0101010101010101);
}

ZL_FORCE_INLINE void mergeVectorVectorPair(
        uint8_t* restrict out,
        const uint8_t* restrict lhs,
        const uint8_t* restrict rhs,
        size_t lhsOffset,
        size_t rhsOffset,
        uint8_t maskLo,
        uint8_t maskHi)
{
    const int8x16_t shuf0 = vreinterpretq_s8_u8(
            vld1q_u8(ZL_kPivCoHuffmanNeonMergeShuf0[maskLo]));
    const int8x16_t shuf1 = vreinterpretq_s8_u8(
            vld1q_u8(ZL_kPivCoHuffmanNeonMergeShuf1[maskHi]));
    const uint8x16_t shuf = vreinterpretq_u8_s8(vabdq_s8(shuf0, shuf1));
    uint8x16x2_t src;
    src.val[0] = vld1q_u8(rhs + rhsOffset);
    src.val[1] = vld1q_u8(lhs + lhsOffset);
    vst1q_u8(out, vqtbl2q_u8(src, shuf));
}

ZL_FORCE_INLINE void mergeVectorVectorBlock64(
        uint8_t* restrict out,
        const uint8_t* restrict lhs,
        const uint8_t* restrict rhs,
        uint64_t masks,
        uint64_t prefix,
        size_t* lhsIdx,
        size_t* rhsIdx)
{
    const size_t p2      = (size_t)((prefix >> 8) & 0xFF);
    const size_t p4      = (size_t)((prefix >> 24) & 0xFF);
    const size_t p6      = (size_t)((prefix >> 40) & 0xFF);
    const size_t total   = (size_t)(prefix >> 56);
    const size_t lhsBase = *lhsIdx;
    const size_t rhsBase = *rhsIdx;

    mergeVectorVectorPair(
            out,
            lhs,
            rhs,
            lhsBase,
            rhsBase,
            (uint8_t)masks,
            (uint8_t)(masks >> 8));
    mergeVectorVectorPair(
            out + 16,
            lhs,
            rhs,
            lhsBase + 16 - p2,
            rhsBase + p2,
            (uint8_t)(masks >> 16),
            (uint8_t)(masks >> 24));
    mergeVectorVectorPair(
            out + 32,
            lhs,
            rhs,
            lhsBase + 32 - p4,
            rhsBase + p4,
            (uint8_t)(masks >> 32),
            (uint8_t)(masks >> 40));
    mergeVectorVectorPair(
            out + 48,
            lhs,
            rhs,
            lhsBase + 48 - p6,
            rhsBase + p6,
            (uint8_t)(masks >> 48),
            (uint8_t)(masks >> 56));

    *rhsIdx += total;
    *lhsIdx += 64 - total;
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
    size_t const outSize = lhsSize + rhsSize;

    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= bitmapBytes(outSize));
    (void)outCapacity, (void)bitmapCapacity;

    size_t outIdx = 0;
    size_t lhsIdx = 0;
    size_t rhsIdx = 0;

    if (outIdx + 64 <= outSize && lhsIdx <= lhsSize && rhsIdx <= rhsSize) {
        uint64_t masks  = ZL_readLE64(bitmap + outIdx / 8);
        uint64_t prefix = mergeRankPrefix(vld1_u8(bitmap + outIdx / 8));
        for (;;) {
            const bool hasNext  = outIdx + 128 <= outSize;
            uint64_t nextMasks  = 0;
            uint64_t nextPrefix = 0;
            if (ZL_LIKELY(hasNext)) {
                const uint8_t* const nextBitmap = bitmap + (outIdx + 64) / 8;
                nextMasks                       = ZL_readLE64(nextBitmap);
                nextPrefix = mergeRankPrefix(vld1_u8(nextBitmap));
            }
            mergeVectorVectorBlock64(
                    out + outIdx, lhs, rhs, masks, prefix, &lhsIdx, &rhsIdx);
            outIdx += 64;
            if (!hasNext || lhsIdx > lhsSize || rhsIdx > rhsSize) {
                break;
            }
            masks  = nextMasks;
            prefix = nextPrefix;
        }
    }

    for (; outIdx + 16 <= outSize && lhsIdx <= lhsSize && rhsIdx <= rhsSize;
         outIdx += 16) {
        const uint8_t mask0 = bitmap[outIdx >> 3];
        const uint8_t mask1 = bitmap[(outIdx >> 3) + 1];
        mergeVectorVectorPair(
                out + outIdx, lhs, rhs, lhsIdx, rhsIdx, mask0, mask1);
        const size_t ones0 = ZL_kPivCoHuffmanNeonPopcount[mask0];
        const size_t ones1 = ZL_kPivCoHuffmanNeonPopcount[mask1];
        const size_t ones  = ones0 + ones1;
        rhsIdx += ones;
        lhsIdx += 16 - ones;
    }

    // The last < 16 outputs go through a temp buffer so the slop-free bitmap
    // and `out` are not touched past their ends.
    if (outIdx < outSize && lhsIdx <= lhsSize && rhsIdx <= rhsSize) {
        const size_t remaining = outSize - outIdx;
        assert(remaining < 16);
        uint16_t mask = bitmap[outIdx / 8];
        if (outIdx + 8 < outSize) {
            mask |= (uint16_t)((uint16_t)bitmap[(outIdx + 8) / 8] << 8);
        }
        // Drop bits past the valid tail: the source bitmap may hold unrelated
        // set bits there, and counting them would over-advance rhsIdx.
        mask &= (uint16_t)((1u << remaining) - 1u);
        uint8_t tmp[16];
        mergeVectorVectorPair(
                tmp,
                lhs,
                rhs,
                lhsIdx,
                rhsIdx,
                (uint8_t)mask,
                (uint8_t)(mask >> 8));
        ZL_memcpy(out + outIdx, tmp, remaining);
        rhsIdx += ZL_kPivCoHuffmanNeonPopcount[(uint8_t)mask]
                + ZL_kPivCoHuffmanNeonPopcount[(uint8_t)(mask >> 8)];
    }
    return rhsIdx;
}

ZL_FORCE_INLINE void mergeConstantVectorPair(
        uint8_t* restrict out,
        const uint8_t* restrict vector,
        uint8x16_t constant,
        size_t vectorOffset,
        uint8_t maskLo,
        uint8_t maskHi)
{
    const int8x16_t shuf0 = vreinterpretq_s8_u8(
            vld1q_u8(ZL_kPivCoHuffmanNeonMergeShuf0[maskLo]));
    const int8x16_t shuf1 = vreinterpretq_s8_u8(
            vld1q_u8(ZL_kPivCoHuffmanNeonMergeShuf1[maskHi]));
    const uint8x16_t shuf = vreinterpretq_u8_s8(vabdq_s8(shuf0, shuf1));
    uint8x16x2_t src;
    src.val[0] = vld1q_u8(vector + vectorOffset);
    src.val[1] = constant;
    vst1q_u8(out, vqtbl2q_u8(src, shuf));
}

ZL_FORCE_INLINE void mergeConstantVectorBlock64(
        uint8_t* restrict out,
        const uint8_t* restrict vector,
        uint8x16_t constant,
        uint64_t masks,
        uint64_t prefix,
        size_t* vectorIdx)
{
    const size_t p2    = (size_t)((prefix >> 8) & 0xFF);
    const size_t p4    = (size_t)((prefix >> 24) & 0xFF);
    const size_t p6    = (size_t)((prefix >> 40) & 0xFF);
    const size_t total = (size_t)(prefix >> 56);
    const size_t idx   = *vectorIdx;

    mergeConstantVectorPair(
            out, vector, constant, idx, (uint8_t)masks, (uint8_t)(masks >> 8));
    mergeConstantVectorPair(
            out + 16,
            vector,
            constant,
            idx + p2,
            (uint8_t)(masks >> 16),
            (uint8_t)(masks >> 24));
    mergeConstantVectorPair(
            out + 32,
            vector,
            constant,
            idx + p4,
            (uint8_t)(masks >> 32),
            (uint8_t)(masks >> 40));
    mergeConstantVectorPair(
            out + 48,
            vector,
            constant,
            idx + p6,
            (uint8_t)(masks >> 48),
            (uint8_t)(masks >> 56));

    *vectorIdx += total;
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
    size_t const outSize = lhsSize + rhsSize;

    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= bitmapBytes(outSize));
    (void)outCapacity, (void)bitmapCapacity;

    size_t outIdx               = 0;
    size_t rhsIdx               = 0;
    const uint8x16_t constant16 = vdupq_n_u8(lhs);

    if (outIdx + 64 <= outSize && rhsIdx <= rhsSize) {
        uint64_t masks  = ZL_readLE64(bitmap + outIdx / 8);
        uint64_t prefix = mergeRankPrefix(vld1_u8(bitmap + outIdx / 8));
        for (;;) {
            const bool hasNext  = outIdx + 128 <= outSize;
            uint64_t nextMasks  = 0;
            uint64_t nextPrefix = 0;
            if (ZL_LIKELY(hasNext)) {
                const uint8_t* const nextBitmap = bitmap + (outIdx + 64) / 8;
                nextMasks                       = ZL_readLE64(nextBitmap);
                nextPrefix = mergeRankPrefix(vld1_u8(nextBitmap));
            }
            mergeConstantVectorBlock64(
                    out + outIdx, rhs, constant16, masks, prefix, &rhsIdx);
            outIdx += 64;
            if (!hasNext || rhsIdx > rhsSize) {
                break;
            }
            masks  = nextMasks;
            prefix = nextPrefix;
        }
    }

    for (; outIdx + 16 <= outSize && rhsIdx <= rhsSize; outIdx += 16) {
        const uint8_t mask0 = bitmap[outIdx >> 3];
        const uint8_t mask1 = bitmap[(outIdx >> 3) + 1];
        mergeConstantVectorPair(
                out + outIdx, rhs, constant16, rhsIdx, mask0, mask1);
        rhsIdx += ZL_kPivCoHuffmanNeonPopcount[mask0]
                + ZL_kPivCoHuffmanNeonPopcount[mask1];
    }

    // The last < 16 outputs go through a temp buffer so the slop-free bitmap
    // and `out` are not touched past their ends.
    if (outIdx < outSize && rhsIdx <= rhsSize) {
        const size_t remaining = outSize - outIdx;
        assert(remaining < 16);
        uint16_t mask = bitmap[outIdx / 8];
        if (outIdx + 8 < outSize) {
            mask |= (uint16_t)((uint16_t)bitmap[(outIdx + 8) / 8] << 8);
        }
        // Drop bits past the valid tail: those lanes must take the constant,
        // and counting them would over-advance rhsIdx.
        mask &= (uint16_t)((1u << remaining) - 1u);
        uint8_t tmp[16];
        mergeConstantVectorPair(
                tmp,
                rhs,
                constant16,
                rhsIdx,
                (uint8_t)mask,
                (uint8_t)(mask >> 8));
        ZL_memcpy(out + outIdx, tmp, remaining);
        rhsIdx += ZL_kPivCoHuffmanNeonPopcount[(uint8_t)mask]
                + ZL_kPivCoHuffmanNeonPopcount[(uint8_t)(mask >> 8)];
    }
    return rhsIdx;
}

// mergeFlatDepthTail re-enters the dispatch below to decode one whole block.
// Going back through the dispatch rather than through a function pointer to the
// individual kernel keeps every kernel's address untaken, so they all stay
// inlinable into the switch.
static void mergeFlatDepth(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        size_t depth,
        const uint8_t* symbols);

// Number of elements the flat-depth tail decodes in one shot. It is a multiple
// of every kernel's block (64, 32 or 16), so the call below always runs to
// completion and never needs a tail of its own -- that is what keeps the
// recursion one level deep. It also exceeds the largest remainder any kernel
// can leave, which is one short of its block.
//
// It doubles as the scratch size. Decoding a whole block, a kernel touches
// exactly its own packed bitmap (64 * depth / 8, at most 64 bytes at depth 8)
// and its own output (64 bytes): the wide loads and stores that over-run a
// partial block all land inside the block here, so neither scratch needs slack.
#    define FLAT_DEPTH_TAIL_ELEMS 64

// How much padding the caller left past the ends of `bitmap` and `out`, which
// decides how far a kernel's block loop may run. Both are padded by
// ZL_PIVCO_HUFFMAN_SLOP on the decode path, so FlatSlopBoth is the common case.
typedef enum {
    // Neither buffer is padded. The fast paths use 16-byte loads to fetch 12
    // bytes or fewer, so they must stop one load short of the end.
    FlatSlopNone = 0,
    // Only the bitmap is padded, so a block may over-read the codes but must
    // still land inside `out`: the loop stops after the last whole block.
    FlatSlopBitmap,
    // Both are padded, so the block holding the last element may run past
    // outSize into the slop. The loop then covers every element and the tail
    // never runs -- the fast path for real callers.
    FlatSlopBoth,
} FlatSlop;

// Offset from outSize at which a block loop must stop: it runs while
// `i <= outSize - guard`. @p block is the loop's stride, @p readMargin the
// bytes its loads fetch beyond the codes that block decodes.
ZL_FORCE_INLINE ptrdiff_t
flatDepthEnd(size_t outSize, FlatSlop slop, size_t block, size_t readMargin)
{
    size_t guard;
    if (slop == FlatSlopBoth) {
        guard = 1;
    } else if (slop == FlatSlopBitmap) {
        guard = block;
    } else {
        guard = block + readMargin;
    }
    return (ptrdiff_t)outSize - (ptrdiff_t)guard;
}

// Kept out of line: this is the cold path, and letting its scratch buffers and
// memset/memcpy inline into all eight switch arms costs the hot loops
// registers.
//
// Decode the final `outSize - i` (< FLAT_DEPTH_TAIL_ELEMS) flat-depth elements
// without a ladder of narrowing loops or a scalar fallback: copy the packed
// bitmap tail into a zero-padded temporary, run one full
// FLAT_DEPTH_TAIL_ELEMS-element block of `kernel` into a temporary output, then
// copy out the valid bytes. Mirrors the temp-buffer tail the merge kernels use.
// Padding the remainder up to a whole block is what lets a single block loop
// serve as its own tail; the temporaries keep the reads and writes that block
// performs off the (slop-free) bitmap and `out`.
ZL_FORCE_NOINLINE void mergeFlatDepthTail(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        size_t depth,
        const uint8_t* symbols,
        size_t i)
{
    const size_t remaining = outSize - i;
    assert(remaining < FLAT_DEPTH_TAIL_ELEMS);
    // Element i starts on a byte boundary: every block loop advances by a
    // multiple of 8 elements, so i * depth is a multiple of 8. That also makes
    // the copy below end exactly at bitmapBytes(outSize * depth), inside the
    // bitmap the caller validated.
    assert((i * depth) % 8 == 0);

    ZL_ALIGNED(16)
    uint8_t tmpBitmap[FLAT_DEPTH_TAIL_ELEMS + ZL_PIVCO_HUFFMAN_SLOP];
    ZL_memset(tmpBitmap, 0, sizeof(tmpBitmap));
    ZL_memcpy(
            tmpBitmap,
            bitmap + (i * depth) / 8,
            bitmapBytes(remaining * depth));

    ZL_ALIGNED(16)
    uint8_t tmpOut[FLAT_DEPTH_TAIL_ELEMS + ZL_PIVCO_HUFFMAN_SLOP];
    mergeFlatDepth(
            tmpOut,
            FLAT_DEPTH_TAIL_ELEMS,
            sizeof(tmpOut),
            tmpBitmap,
            sizeof(tmpBitmap),
            depth,
            symbols);
    ZL_memcpy(out + i, tmpOut, remaining);
}

static void mergeFlatDepth1(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        const uint8_t* symbols,
        FlatSlop slop)
{
    const uint16_t lrWord =
            (uint16_t)((uint16_t)symbols[0] | ((uint16_t)symbols[1] << 8));
    const uint8x16_t c2s   = vreinterpretq_u8_u16(vdupq_n_u16(lrWord));
    const uint8_t kDup[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 };
    const int8_t kShift[16] = { 0, -1, -2, -3, -4, -5, -6, -7,
                                0, -1, -2, -3, -4, -5, -6, -7 };
    const uint8x16_t dup    = vld1q_u8(kDup);
    const uint8x16_t dup2   = vaddq_u8(dup, vdupq_n_u8(2));
    const uint8x16_t dup4   = vaddq_u8(dup, vdupq_n_u8(4));
    const uint8x16_t dup6   = vaddq_u8(dup, vdupq_n_u8(6));
    const int8x16_t shift   = vld1q_s8(kShift);
    const uint8x16_t one    = vdupq_n_u8(1);

    const ptrdiff_t end = flatDepthEnd(outSize, slop, 64, 0);

    ptrdiff_t i = 0;
    for (; i <= end; i += 64) {
        const uint64_t bits = ZL_readLE64(bitmap + (i >> 3));
        const uint8x16_t bm =
                vreinterpretq_u8_u64(vsetq_lane_u64(bits, vdupq_n_u64(0), 0));
        const uint8x16_t idx0 =
                vandq_u8(vshlq_u8(vqtbl1q_u8(bm, dup), shift), one);
        const uint8x16_t idx1 =
                vandq_u8(vshlq_u8(vqtbl1q_u8(bm, dup2), shift), one);
        const uint8x16_t idx2 =
                vandq_u8(vshlq_u8(vqtbl1q_u8(bm, dup4), shift), one);
        const uint8x16_t idx3 =
                vandq_u8(vshlq_u8(vqtbl1q_u8(bm, dup6), shift), one);
        vst1q_u8(out + i, vqtbl1q_u8(c2s, idx0));
        vst1q_u8(out + i + 16, vqtbl1q_u8(c2s, idx1));
        vst1q_u8(out + i + 32, vqtbl1q_u8(c2s, idx2));
        vst1q_u8(out + i + 48, vqtbl1q_u8(c2s, idx3));
    }
    if ((size_t)i < outSize) {
        mergeFlatDepthTail(out, outSize, bitmap, 1, symbols, (size_t)i);
    }
}

static uint8x16_t flatD2Unpack(const uint8_t* bm)
{
    const uint8_t kDup[16] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3 };
    const int8_t kShift[16] = { 0, -2, -4, -6, 0, -2, -4, -6,
                                0, -2, -4, -6, 0, -2, -4, -6 };
    uint32_t const packed   = ZL_readLE32(bm);
    uint8x16_t bmLo =
            vreinterpretq_u8_u32(vsetq_lane_u32(packed, vdupq_n_u32(0), 0));
    uint8x16_t const dup = vqtbl1q_u8(bmLo, vld1q_u8(kDup));
    return vandq_u8(vshlq_u8(dup, vld1q_s8(kShift)), vdupq_n_u8(0x03));
}

static void mergeFlatDepth2(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        const uint8_t* symbols,
        FlatSlop slop)
{
    ZL_ALIGNED(16) uint8_t lut[16] = { 0 };
    ZL_memcpy(lut, symbols, 4);
    uint8x16_t const c2s = vld1q_u8(lut);
    const ptrdiff_t end  = flatDepthEnd(outSize, slop, 64, 0);

    ptrdiff_t i = 0;
    for (; i <= end; i += 64) {
        const uint8x16_t codes0 = flatD2Unpack(bitmap + (i >> 2));
        const uint8x16_t codes1 = flatD2Unpack(bitmap + (i >> 2) + 4);
        const uint8x16_t codes2 = flatD2Unpack(bitmap + (i >> 2) + 8);
        const uint8x16_t codes3 = flatD2Unpack(bitmap + (i >> 2) + 12);
        vst1q_u8(out + i, vqtbl1q_u8(c2s, codes0));
        vst1q_u8(out + i + 16, vqtbl1q_u8(c2s, codes1));
        vst1q_u8(out + i + 32, vqtbl1q_u8(c2s, codes2));
        vst1q_u8(out + i + 48, vqtbl1q_u8(c2s, codes3));
    }
    if ((size_t)i < outSize) {
        mergeFlatDepthTail(out, outSize, bitmap, 2, symbols, (size_t)i);
    }
}

static void mergeFlatDepth3(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        const uint8_t* symbols,
        FlatSlop slop)
{
    const uint8_t kPairShuf[16] = { 0, 1, 1, 2, 3, 4,  4,  5,
                                    6, 7, 7, 8, 9, 10, 10, 11 };
    const int16_t kHalfShift[8] = { 2, -2, 2, -2, 2, -2, 2, -2 };
    const int8_t kByteShift[16] = { -2, 0, -2, 0, -2, 0, -2, 0,
                                    -2, 0, -2, 0, -2, 0, -2, 0 };
    const uint8x8_t c2s8        = vld1_u8(symbols);
    const uint8x16_t c2s        = vcombine_u8(c2s8, c2s8);
    const uint8x16x2_t c2s2     = { { c2s, c2s } };
    const uint8x16_t pairShuf   = vld1q_u8(kPairShuf);
    const int16x8_t halfShift   = vld1q_s16(kHalfShift);
    const int8x16_t byteShift   = vld1q_s8(kByteShift);
    const uint8x16_t mask       = vdupq_n_u8(0x07);
    const ptrdiff_t end         = flatDepthEnd(outSize, slop, 32, 16);

    ptrdiff_t i = 0;
    for (; i <= end; i += 32) {
        const uint8x16_t packed = vld1q_u8(bitmap + ((i * 3) >> 3));
        uint16x8_t pairs = vreinterpretq_u16_u8(vqtbl1q_u8(packed, pairShuf));
        pairs            = vshlq_u16(pairs, halfShift);
        const uint8x16_t pairCodes =
                vshlq_u8(vreinterpretq_u8_u16(pairs), byteShift);
        uint8x16x2_t decoded;
        decoded.val[0] = vqtbl1q_u8(c2s, vandq_u8(pairCodes, mask));
        decoded.val[1] = vqtbl2q_u8(c2s2, vshrq_n_u8(pairCodes, 3));
        vst2q_u8(out + i, decoded);
    }
    if ((size_t)i < outSize) {
        mergeFlatDepthTail(out, outSize, bitmap, 3, symbols, (size_t)i);
    }
}

static void mergeFlatDepth4(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        const uint8_t* symbols,
        FlatSlop slop)
{
    const uint8x16_t c2s  = vld1q_u8(symbols);
    const uint8x16_t mask = vdupq_n_u8(0x0f);
    const ptrdiff_t end   = flatDepthEnd(outSize, slop, 64, 0);

    ptrdiff_t i = 0;
    for (; i <= end; i += 64) {
        const uint8x16_t packed0 = vld1q_u8(bitmap + (i >> 1));
        const uint8x16_t packed1 = vld1q_u8(bitmap + (i >> 1) + 16);
        const uint8x16_t lo0     = vqtbl1q_u8(c2s, vandq_u8(packed0, mask));
        const uint8x16_t lo1     = vqtbl1q_u8(c2s, vandq_u8(packed1, mask));
        const uint8x16_t hi0     = vqtbl1q_u8(c2s, vshrq_n_u8(packed0, 4));
        const uint8x16_t hi1     = vqtbl1q_u8(c2s, vshrq_n_u8(packed1, 4));
        vst1q_u8(out + i, vzip1q_u8(lo0, hi0));
        vst1q_u8(out + i + 16, vzip2q_u8(lo0, hi0));
        vst1q_u8(out + i + 32, vzip1q_u8(lo1, hi1));
        vst1q_u8(out + i + 48, vzip2q_u8(lo1, hi1));
    }
    if ((size_t)i < outSize) {
        mergeFlatDepthTail(out, outSize, bitmap, 4, symbols, (size_t)i);
    }
}

static void mergeFlatDepth5(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        const uint8_t* symbols,
        FlatSlop slop)
{
    const uint8_t kPairShuf[16] = { 0, 1, 1, 2, 2, 3, 3, 4,
                                    5, 6, 6, 7, 7, 8, 8, 9 };
    const int16_t kHalfShift[8] = { 3, 1, -1, -3, 3, 1, -1, -3 };
    const int8_t kByteShift[16] = { -3, 0, -3, 0, -3, 0, -3, 0,
                                    -3, 0, -3, 0, -3, 0, -3, 0 };
    uint8x16x2_t c2s;
    c2s.val[0]                = vld1q_u8(symbols);
    c2s.val[1]                = vld1q_u8(symbols + 16);
    const uint8x16_t pairShuf = vld1q_u8(kPairShuf);
    const int16x8_t halfShift = vld1q_s16(kHalfShift);
    const int8x16_t byteShift = vld1q_s8(kByteShift);
    const uint8x16_t mask     = vdupq_n_u8(0x1f);
    const ptrdiff_t end       = flatDepthEnd(outSize, slop, 16, 9);

    ptrdiff_t i = 0;
    for (; i <= end; i += 16) {
        const uint8x16_t packed = vld1q_u8(bitmap + ((i * 5) >> 3));
        uint16x8_t pairs = vreinterpretq_u16_u8(vqtbl1q_u8(packed, pairShuf));
        pairs            = vshlq_u16(pairs, halfShift);
        const uint8x16_t codes = vandq_u8(
                vshlq_u8(vreinterpretq_u8_u16(pairs), byteShift), mask);
        vst1q_u8(out + i, vqtbl2q_u8(c2s, codes));
    }
    if ((size_t)i < outSize) {
        mergeFlatDepthTail(out, outSize, bitmap, 5, symbols, (size_t)i);
    }
}

static void mergeFlatDepth6(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        const uint8_t* symbols,
        FlatSlop slop)
{
    const uint8_t kPairShuf[16] = { 0, 1, 1, 2, 3, 4,  4,  5,
                                    6, 7, 7, 8, 9, 10, 10, 11 };
    const int16_t kHalfShift[8] = { 2, -2, 2, -2, 2, -2, 2, -2 };
    const int8_t kByteShift[16] = { -2, 0, -2, 0, -2, 0, -2, 0,
                                    -2, 0, -2, 0, -2, 0, -2, 0 };
    uint8x16x4_t c2s;
    c2s.val[0]                 = vld1q_u8(symbols);
    c2s.val[1]                 = vld1q_u8(symbols + 16);
    c2s.val[2]                 = vld1q_u8(symbols + 32);
    c2s.val[3]                 = vld1q_u8(symbols + 48);
    const uint8x16_t pairShuf  = vld1q_u8(kPairShuf);
    const uint8x16_t pairShuf2 = vaddq_u8(pairShuf, vdupq_n_u8(4));
    const int16x8_t halfShift  = vld1q_s16(kHalfShift);
    const int8x16_t byteShift  = vld1q_s8(kByteShift);
    const uint8x16_t mask      = vdupq_n_u8(0x3f);
    const ptrdiff_t end        = flatDepthEnd(outSize, slop, 32, 8);

    ptrdiff_t i = 0;
    for (; i <= end; i += 32) {
        const uint8_t* const input = bitmap + ((i * 6) >> 3);
        uint16x8_t pairs0 =
                vreinterpretq_u16_u8(vqtbl1q_u8(vld1q_u8(input), pairShuf));
        uint16x8_t pairs1 = vreinterpretq_u16_u8(
                vqtbl1q_u8(vld1q_u8(input + 8), pairShuf2));
        pairs0                  = vshlq_u16(pairs0, halfShift);
        pairs1                  = vshlq_u16(pairs1, halfShift);
        const uint8x16_t codes0 = vandq_u8(
                vshlq_u8(vreinterpretq_u8_u16(pairs0), byteShift), mask);
        const uint8x16_t codes1 = vandq_u8(
                vshlq_u8(vreinterpretq_u8_u16(pairs1), byteShift), mask);
        vst1q_u8(out + i, vqtbl4q_u8(c2s, codes0));
        vst1q_u8(out + i + 16, vqtbl4q_u8(c2s, codes1));
    }
    if ((size_t)i < outSize) {
        mergeFlatDepthTail(out, outSize, bitmap, 6, symbols, (size_t)i);
    }
}

static uint8x8_t flatD7Unpack(const uint8_t* bm)
{
    const uint8_t kShuf[16] = {
        0, 1, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6
    };
    const int16_t kShift[8] = { 0, -7, -6, -5, -4, -3, -2, -1 };
    uint16x8_t const words =
            vreinterpretq_u16_u8(vqtbl1q_u8(vld1q_u8(bm), vld1q_u8(kShuf)));
    return vmovn_u16(
            vandq_u16(vshlq_u16(words, vld1q_s16(kShift)), vdupq_n_u16(0x7f)));
}

static void mergeFlatDepth7(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        const uint8_t* symbols,
        FlatSlop slop)
{
    uint8x16x4_t lo;
    uint8x16x4_t hi;
    lo.val[0]              = vld1q_u8(symbols);
    lo.val[1]              = vld1q_u8(symbols + 16);
    lo.val[2]              = vld1q_u8(symbols + 32);
    lo.val[3]              = vld1q_u8(symbols + 48);
    hi.val[0]              = vld1q_u8(symbols + 64);
    hi.val[1]              = vld1q_u8(symbols + 80);
    hi.val[2]              = vld1q_u8(symbols + 96);
    hi.val[3]              = vld1q_u8(symbols + 112);
    const uint8x16_t sub64 = vdupq_n_u8(64);
    const ptrdiff_t end    = flatDepthEnd(outSize, slop, 32, 24);

    ptrdiff_t i = 0;
    for (; i <= end; i += 32) {
        const uint8x16_t codes0 = vcombine_u8(
                flatD7Unpack(bitmap + ((i * 7) >> 3)),
                flatD7Unpack(bitmap + (((i + 8) * 7) >> 3)));
        const uint8x16_t codes1 = vcombine_u8(
                flatD7Unpack(bitmap + (((i + 16) * 7) >> 3)),
                flatD7Unpack(bitmap + (((i + 24) * 7) >> 3)));
        uint8x16_t values0 = vqtbl4q_u8(lo, codes0);
        uint8x16_t values1 = vqtbl4q_u8(lo, codes1);
        values0            = vqtbx4q_u8(values0, hi, vsubq_u8(codes0, sub64));
        values1            = vqtbx4q_u8(values1, hi, vsubq_u8(codes1, sub64));
        vst1q_u8(out + i, values0);
        vst1q_u8(out + i + 16, values1);
    }
    if ((size_t)i < outSize) {
        mergeFlatDepthTail(out, outSize, bitmap, 7, symbols, (size_t)i);
    }
}

static void mergeFlatDepth8(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        const uint8_t* symbols)
{
    // Depth 8: each output is a full-byte index into the 256-entry table, so
    // this is a direct table lookup -- left as a scalar loop for the compiler
    // to auto-vectorize.
    // This is not expected to be hot in real usage.
    for (size_t outIdx = 0; outIdx < outSize; ++outIdx) {
        out[outIdx] = symbols[bitmap[outIdx]];
    }
}

static void mergeFlatDepth(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        size_t depth,
        const uint8_t* symbols)
{
    assert(depth >= 1 && depth <= 8);
    assert(outCapacity >= outSize);
    assert(outSize <= (SIZE_MAX - 7) / depth);
    assert(bitmapCapacity >= bitmapBytes(outSize * depth));
    // Classify the caller's padding once: it decides how far each block loop
    // may run, and whether the tail is needed at all.
    const bool bitmapHasSlop = bitmapCapacity
            >= bitmapBytes(outSize * depth) + ZL_PIVCO_HUFFMAN_SLOP;
    const bool outHasSlop = outCapacity >= outSize + ZL_PIVCO_HUFFMAN_SLOP;
    const FlatSlop slop   = !bitmapHasSlop ? FlatSlopNone
              : outHasSlop                 ? FlatSlopBoth
                                           : FlatSlopBitmap;

    switch (depth) {
        case 1:
            mergeFlatDepth1(out, outSize, bitmap, symbols, slop);
            return;
        case 2:
            mergeFlatDepth2(out, outSize, bitmap, symbols, slop);
            return;
        case 3:
            mergeFlatDepth3(out, outSize, bitmap, symbols, slop);
            return;
        case 4:
            mergeFlatDepth4(out, outSize, bitmap, symbols, slop);
            return;
        case 5:
            mergeFlatDepth5(out, outSize, bitmap, symbols, slop);
            return;
        case 6:
            mergeFlatDepth6(out, outSize, bitmap, symbols, slop);
            return;
        case 7:
            mergeFlatDepth7(out, outSize, bitmap, symbols, slop);
            return;
        default:
            mergeFlatDepth8(out, outSize, bitmap, symbols);
            return;
    }
}

const ZL_PivCoHuffmanDecode ZL_PivCoHuffmanDecode_arm = {
    .supported           = supported,
    .mergeVectorVector   = mergeVectorVector,
    .mergeConstantVector = mergeConstantVector,
    .mergeFlatDepth      = mergeFlatDepth,
};

#else

static bool supported(const ZL_cpuid_t* cpuid)
{
    (void)cpuid;
    return false;
}

const ZL_PivCoHuffmanDecode ZL_PivCoHuffmanDecode_arm = {
    .supported = supported,
};

#endif
