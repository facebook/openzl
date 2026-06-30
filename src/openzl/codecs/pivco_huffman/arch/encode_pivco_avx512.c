// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/arch/encode_pivco_arch.h"

#if ZL_ARCH_X86_64 && ZL_HAS_ATTRIBUTE(__target__)

#    include <assert.h>
#    include <immintrin.h>

#    include "openzl/shared/bits.h"
#    include "openzl/shared/mem.h"

#    define ZL_AVX512_ATTR   \
        ZL_TARGET_ATTRIBUTE( \
                "avx512vbmi,avx512vbmi2,avx512f,avx512vl,avx512bw,bmi2")
#    define ZL_AVX512_INLINE ZL_FORCE_INLINE ZL_AVX512_ATTR

static bool supported(const ZL_cpuid_t* cpuid)
{
    return cpuid != NULL && ZL_cpuid_avx512vbmi(*cpuid)
            && ZL_cpuid_avx512vbmi2(*cpuid) && ZL_cpuid_avx512f(*cpuid)
            && ZL_cpuid_avx512vl(*cpuid) && ZL_cpuid_avx512bw(*cpuid)
            && ZL_cpuid_bmi2(*cpuid);
}

/// @returns a 64-bit lane mask with the low @p lanes bits set (all 64 when
/// @p lanes >= 64). Used to select the valid byte lanes of a partial 64-element
/// tail block.
ZL_AVX512_INLINE __mmask64 tailMask64(size_t lanes)
{
    return lanes >= 64 ? ~(__mmask64)0 : (((__mmask64)1 << lanes) - 1);
}

/// @returns the number of whole bytes needed to hold @p lanes packed
/// @p depth-bit indices (ceil(lanes*depth / 8)).
ZL_AVX512_INLINE size_t packedBytes(size_t lanes, size_t depth)
{
    return (lanes * depth + 7) / 8;
}

/**
 * @param kPartitionLhs/kPartitionRhs Compile-time flags (always passed as
 * literals so the branches fold away) selecting which child streams to produce.
 * They are used instead of testing `lhs == NULL` / `rhs == NULL` because
 * partitionFull may legitimately be called with a NULL child, and a runtime
 * NULL test would add a branch to the hot loop.
 *
 * @note Writes whole 64-bit words to @p bitmap, so the final word may spill up
 * to 7 bytes past `(numRanks + 7) / 8`; covered by SLOP.
 */
ZL_AVX512_INLINE size_t partitionImpl(
        uint8_t* bitmap,
        uint8_t* lhs,
        uint8_t* rhs,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank,
        const bool kPartitionLhs,
        const bool kPartitionRhs)
{
    const __m512i threshold = _mm512_set1_epi8((char)rightRank);
    size_t zeros            = 0;
    size_t ones             = 0;
    size_t i                = 0;

    for (; i + 64 <= numRanks; i += 64) {
        const __m512i rankVec = _mm512_loadu_si512((const void*)(ranks + i));
        // One mask bit per lane: set where rank >= rightRank (the right child).
        // The 64-bit mask is the partition bitmap for these 64 ranks.
        const __mmask64 bits =
                _mm512_cmp_epu8_mask(rankVec, threshold, _MM_CMPINT_GE);
        ZL_writeLE64(bitmap + i / 8, (uint64_t)bits);

        const size_t blockOnes = (size_t)ZL_popcount64((uint64_t)bits);
        if (kPartitionRhs) {
            // Compress-store gathers the masked lanes into a contiguous run,
            // appending this block's right-child ranks after the previous ones.
            _mm512_mask_compressstoreu_epi8(rhs + ones, bits, rankVec);
            ones += blockOnes;
        }
        if (kPartitionLhs) {
            // ~bits selects the left child; in a full 64-lane block every
            // inverted bit is a valid lane, so no extra masking is needed.
            _mm512_mask_compressstoreu_epi8(lhs + zeros, ~bits, rankVec);
            zeros += 64 - blockOnes;
        }
    }

    if (i < numRanks) {
        const size_t lanes    = numRanks - i;
        const __mmask64 valid = tailMask64(lanes);
        const __m512i rankVec =
                _mm512_maskz_loadu_epi8(valid, (const void*)(ranks + i));
        // Force out-of-range tail lanes to 0 so they never look like a 1-bit.
        const __mmask64 bits =
                _mm512_cmp_epu8_mask(rankVec, threshold, _MM_CMPINT_GE) & valid;
        // Writes up to 7 bytes beyond the end of bitmap. Ok because of SLOP.
        ZL_writeLE64(bitmap + i / 8, (uint64_t)bits);

        const size_t blockOnes = (size_t)ZL_popcount64((uint64_t)bits);
        if (kPartitionRhs) {
            _mm512_mask_compressstoreu_epi8(rhs + ones, bits, rankVec);
            ones += blockOnes;
        }
        if (kPartitionLhs) {
            // For the tail, restrict the left child to valid lanes as well,
            // otherwise the padding lanes (which are 0-bits) would be stored.
            _mm512_mask_compressstoreu_epi8(
                    lhs + zeros, ~bits & valid, rankVec);
            zeros += lanes - blockOnes;
        }
    }

    if (kPartitionRhs) {
        return ones;
    } else if (kPartitionLhs) {
        return numRanks - zeros;
    } else {
        return 0;
    }
}

static ZL_AVX512_ATTR size_t partitionFull(
        uint8_t* bitmap,
        uint8_t* lhs,
        uint8_t* rhs,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    return partitionImpl(
            bitmap, lhs, rhs, ranks, numRanks, rightRank, true, true);
}

static ZL_AVX512_ATTR size_t partitionLeft(
        uint8_t* bitmap,
        uint8_t* lhs,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    return partitionImpl(
            bitmap, lhs, NULL, ranks, numRanks, rightRank, true, false);
}

static ZL_AVX512_ATTR size_t partitionRight(
        uint8_t* bitmap,
        uint8_t* rhs,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    return partitionImpl(
            bitmap, NULL, rhs, ranks, numRanks, rightRank, false, true);
}

static ZL_AVX512_ATTR void partitionNone(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    (void)partitionImpl(
            bitmap, NULL, NULL, ranks, numRanks, rightRank, false, false);
}

/// Loads the @p valid lanes of 64 ranks and subtracts @p rankBegin from each,
/// yielding one flat-leaf index per byte lane (the rank's offset within its
/// flat leaf, < 2^depth). Lanes outside @p valid are forced to 0 so padding
/// never contributes spurious set bits to the packed output. Pass @p valid ==
/// -1 for a full block: the all-ones mask folds away to a plain load with no
/// zeroing.
ZL_AVX512_INLINE __m512i
loadRankIndexBytes64(const uint8_t* ranks, uint8_t rankBegin, __mmask64 valid)
{
    const __m512i rankVec = _mm512_maskz_loadu_epi8(valid, (const void*)ranks);
    const __m512i indices =
            _mm512_sub_epi8(rankVec, _mm512_set1_epi8((char)rankBegin));
    return _mm512_maskz_mov_epi8(valid, indices);
}

/**
 * Fuses each adjacent (even, odd) pair of byte indices into one 16-bit lane,
 * concatenating their @p depth-bit fields: result lane = even | (odd << depth).
 * This is the first packing step for depths 2..7 (a 64-byte vector becomes 32
 * 16-bit lanes each holding two indices).
 *
 * Each index is < 2^depth, so even and odd never overlap and the "sum" the
 * intrinsics compute is exactly a bitwise concatenation.
 *
 * For depth < 7 this is a single `maddubs`: it multiplies each even byte by 1
 * and each odd byte by `1 << depth`, summing the adjacent pair into a 16-bit
 * lane. depth == 7 cannot use that path: the odd-lane multiplier would be
 * `1 << 7 == 0x80`, which `maddubs` treats as a *signed* -128. So depth 7 falls
 * back to mask-and-shift: keep the even byte's low 7 bits, shift the odd byte's
 * 7 bits down by one (from bit 8 to bit 7), and OR them together.
 */
ZL_AVX512_INLINE __m512i packBytePairs16(__m512i indices, const size_t kDepth)
{
    if (kDepth == 7) {
        const __m512i lo =
                _mm512_and_si512(indices, _mm512_set1_epi16((short)0x007f));
        const __m512i hi = _mm512_srli_epi16(
                _mm512_and_si512(indices, _mm512_set1_epi16((short)0x7f00)), 1);
        return _mm512_or_si512(lo, hi);
    }

    const int pairMultiplierValue = (int)(((1u << kDepth) << 8) | 1u);
    return _mm512_maddubs_epi16(
            indices, _mm512_set1_epi16((short)pairMultiplierValue));
}

/**
 * Packs one 64-index block at depth 2 (output: 16 bytes, 4 indices/byte).
 * `packBytePairs16` first fuses pairs into 4-bit fields (two 2-bit indices per
 * 16-bit lane); then `madd_epi16` with {1, 1<<4} fuses two adjacent lanes into
 * an 8-bit field (four 2-bit indices) in the low byte of each 32-bit lane;
 * finally `cvtepi32_storeu_epi8` narrows each 32-bit lane to that low byte.
 */
ZL_AVX512_INLINE void
packFlatDepth2Block(uint8_t* bitmap, __m512i indices, __mmask16 storeMask)
{
    const __m512i pairs16 = packBytePairs16(indices, 2);
    const __m512i quads32 =
            _mm512_madd_epi16(pairs16, _mm512_set1_epi32(0x00100001));
    _mm512_mask_cvtepi32_storeu_epi8(bitmap, storeMask, quads32);
}

/// Packs depth-2 indices (2 bits each, 16 bytes per 64-index block).
static ZL_AVX512_ATTR void packFlatDepth2(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rankBegin)
{
    size_t idx    = 0;
    size_t outIdx = 0;
    for (; idx + 64 <= numRanks; idx += 64) {
        packFlatDepth2Block(
                bitmap + outIdx,
                loadRankIndexBytes64(ranks + idx, rankBegin, (__mmask64)-1),
                (__mmask16)0xffff);
        outIdx += 16;
    }
    if (idx < numRanks) {
        const size_t lanes    = numRanks - idx;
        const __mmask64 valid = tailMask64(lanes);
        packFlatDepth2Block(
                bitmap + outIdx,
                loadRankIndexBytes64(ranks + idx, rankBegin, valid),
                (__mmask16)tailMask64(packedBytes(lanes, 2)));
    }
}

/**
 * Packs one 64-index block at depth 4 (output: 32 bytes, 2 indices/byte).
 * `packBytePairs16` already fuses each pair into an 8-bit field (two 4-bit
 * indices) in the low byte of every 16-bit lane, so we just narrow each 16-bit
 * lane to its low byte.
 */
ZL_AVX512_INLINE void
packFlatDepth4Block(uint8_t* bitmap, __m512i indices, __mmask32 storeMask)
{
    const __m512i pairs16 = packBytePairs16(indices, 4);
    _mm512_mask_cvtepi16_storeu_epi8(bitmap, storeMask, pairs16);
}

/// Packs depth-4 indices (4 bits each, 32 bytes per 64-index block).
static ZL_AVX512_ATTR void packFlatDepth4(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rankBegin)
{
    size_t idx    = 0;
    size_t outIdx = 0;
    for (; idx + 64 <= numRanks; idx += 64) {
        packFlatDepth4Block(
                bitmap + outIdx,
                loadRankIndexBytes64(ranks + idx, rankBegin, (__mmask64)-1),
                (__mmask32)0xffffffff);
        outIdx += 32;
    }
    if (idx < numRanks) {
        const size_t lanes    = numRanks - idx;
        const __mmask64 valid = tailMask64(lanes);
        packFlatDepth4Block(
                bitmap + outIdx,
                loadRankIndexBytes64(ranks + idx, rankBegin, valid),
                (__mmask32)tailMask64(packedBytes(lanes, 4)));
    }
}

/**
 * Builds the compress-store mask for the bitpack packer. The packer produces 8
 * bytes per group of 8 indices but only the low `packedBytes(8, kDepth)` of
 * each are meaningful; this mask selects, for each of the 8 groups, exactly the
 * meaningful bytes so `compressstoreu` writes them contiguously with no gaps.
 *
 * @param lanes Number of valid indices (64 for a full block, fewer for a tail);
 * groups beyond @p lanes contribute nothing.
 */
ZL_AVX512_INLINE __mmask64 packBitpackStoreMask(size_t kDepth, size_t lanes)
{
    __mmask64 storeMask = 0;
    for (size_t group = 0; group < 8; ++group) {
        const size_t firstLane = group * 8;
        if (lanes <= firstLane) {
            break;
        }

        size_t lanesInGroup = lanes - firstLane;
        if (lanesInGroup > 8) {
            lanesInGroup = 8;
        }

        // Each group occupies one byte-octet of the mask; set the low
        // bytesInGroup bits of that octet.
        const size_t bytesInGroup = packedBytes(lanesInGroup, kDepth);
        storeMask |= (__mmask64)(tailMask64(bytesInGroup) << (group * 8));
    }
    return storeMask;
}

/**
 * Packs one 64-index block for the depths (3, 5, 6, 7) whose fields don't land
 * on byte boundaries. Strategy: build 8*kDepth contiguous bits per group of 8
 * indices inside one 64-bit lane, then compress out the padding bytes.
 *
 * Steps:
 *  1. packBytePairs16  -> 2 indices (2*kDepth bits) per 16-bit lane.
 *  2. madd_epi16 with {1, 1<<(2*kDepth)} -> 4 indices (4*kDepth bits) in the
 *     low bits of each 32-bit lane (quads32).
 *  3. Fuse the two 32-bit halves of each 64-bit lane into 8 contiguous indices:
 *     keep the low half as-is, and shift the high half down so its 4*kDepth
 *     bits begin right where the low half's bits end (at bit 4*kDepth) -- i.e.
 *     right by (32 - 4*kDepth). OR them into octets64 (8*kDepth contiguous
 *     bits/lane). Those bits already sit in the lane's low `kDepth` bytes in
 *     little-endian order, so no further byte extraction is needed.
 *  4. compressstoreu drops the padding bytes (the high 8-kDepth bytes of each
 *     group) via @p storeMask, writing the packed bytes contiguously.
 */
ZL_AVX512_INLINE void packFlatDepthBitpackBlock(
        uint8_t* bitmap,
        __m512i indices,
        __mmask64 storeMask,
        const size_t kDepth)
{
    const __m512i kQuadPackMultiplier =
            _mm512_set1_epi32((int)(((1u << (2 * kDepth)) << 16) | 1u));
    const __m512i kLow32LaneMask = _mm512_set1_epi64((long long)0xffffffffULL);
    const __m512i pairs16        = packBytePairs16(indices, kDepth);
    const __m512i quads32     = _mm512_madd_epi16(pairs16, kQuadPackMultiplier);
    const __m512i lowQuads32  = _mm512_and_si512(quads32, kLow32LaneMask);
    const __m512i highQuads32 = _mm512_andnot_si512(kLow32LaneMask, quads32);
    const __m512i octets64    = _mm512_or_si512(
            lowQuads32,
            _mm512_srli_epi64(highQuads32, (unsigned int)(32 - 4 * kDepth)));
    _mm512_mask_compressstoreu_epi8(bitmap, storeMask, octets64);
}

/**
 * Drives packFlatDepthBitpackBlock over the whole input, precomputing the
 * loop-invariant control vector:
 *  - quadPackMultiplier {1, 1<<(2*depth)} fuses two pairs into four indices.
 */
ZL_AVX512_INLINE void packFlatDepthBitpackImpl(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rankBegin,
        const size_t kDepth)
{
    const __mmask64 fullStoreMask = packBitpackStoreMask(kDepth, 64);

    size_t idx    = 0;
    size_t outIdx = 0;
    for (; idx + 64 <= numRanks; idx += 64) {
        packFlatDepthBitpackBlock(
                bitmap + outIdx,
                loadRankIndexBytes64(ranks + idx, rankBegin, (__mmask64)-1),
                fullStoreMask,
                kDepth);
        // Each full block emits depth bytes per 8 indices == 8*depth bytes.
        outIdx += 8 * kDepth;
    }
    if (idx < numRanks) {
        const size_t lanes    = numRanks - idx;
        const __mmask64 valid = tailMask64(lanes);
        packFlatDepthBitpackBlock(
                bitmap + outIdx,
                loadRankIndexBytes64(ranks + idx, rankBegin, valid),
                packBitpackStoreMask(kDepth, lanes),
                kDepth);
    }
}

// Generates packFlatDepth3/5/6/7, each a thin wrapper that pins the depth so
// the shifts/multipliers in the bitpack impl fold to constants.
#    define DEFINE_PACK_FLAT_DEPTH_BITPACK(DEPTH)               \
        static ZL_AVX512_ATTR void packFlatDepth##DEPTH(        \
                uint8_t* bitmap,                                \
                const uint8_t* ranks,                           \
                size_t numRanks,                                \
                uint8_t rankBegin)                              \
        {                                                       \
            packFlatDepthBitpackImpl(                           \
                    bitmap, ranks, numRanks, rankBegin, DEPTH); \
        }

DEFINE_PACK_FLAT_DEPTH_BITPACK(3)
DEFINE_PACK_FLAT_DEPTH_BITPACK(5)
DEFINE_PACK_FLAT_DEPTH_BITPACK(6)
DEFINE_PACK_FLAT_DEPTH_BITPACK(7)

#    undef DEFINE_PACK_FLAT_DEPTH_BITPACK

/// Packs depth-8 indices: each index already fills a whole byte, so packing is
/// just `rank - rankBegin` stored one byte per index (64 bytes per block).
static ZL_AVX512_ATTR void packFlatDepth8(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rankBegin)
{
    size_t idx    = 0;
    size_t outIdx = 0;
    for (; idx + 64 <= numRanks; idx += 64) {
        _mm512_storeu_si512(
                (void*)(bitmap + outIdx),
                loadRankIndexBytes64(ranks + idx, rankBegin, (__mmask64)-1));
        outIdx += 64;
    }
    if (idx < numRanks) {
        const size_t lanes    = numRanks - idx;
        const __mmask64 valid = tailMask64(lanes);
        _mm512_mask_storeu_epi8(
                bitmap + outIdx,
                valid,
                loadRankIndexBytes64(ranks + idx, rankBegin, valid));
    }
}

/// Entry point for flat-leaf packing: dispatches to the depth-specialized
/// packer (depths 1..8). @see ZL_PivCoHuffmanEncode::packFlatDepth.
static ZL_AVX512_ATTR void packFlatDepth(
        uint8_t* bitmap,
        size_t depth,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rankBegin)
{
    switch (depth) {
        case 1:
            // Exactly equivalent to partitionNone, but need to pass in
            // rightRank (split point) instead of rankBegin.
            partitionNone(bitmap, ranks, numRanks, rankBegin + 1);
            return;
        case 2:
            packFlatDepth2(bitmap, ranks, numRanks, rankBegin);
            return;
        case 3:
            packFlatDepth3(bitmap, ranks, numRanks, rankBegin);
            return;
        case 4:
            packFlatDepth4(bitmap, ranks, numRanks, rankBegin);
            return;
        case 5:
            packFlatDepth5(bitmap, ranks, numRanks, rankBegin);
            return;
        case 6:
            packFlatDepth6(bitmap, ranks, numRanks, rankBegin);
            return;
        case 7:
            packFlatDepth7(bitmap, ranks, numRanks, rankBegin);
            return;
        default:
            assert(depth == 8);
            packFlatDepth8(bitmap, ranks, numRanks, rankBegin);
            return;
    }
}

const ZL_PivCoHuffmanEncode ZL_PivCoHuffmanEncode_avx512 = {
    .supported      = supported,
    .partitionFull  = partitionFull,
    .partitionLeft  = partitionLeft,
    .partitionRight = partitionRight,
    .partitionNone  = partitionNone,
    .packFlatDepth  = packFlatDepth,
};

#else

/// Non-x86-64 build: the AVX-512 kernels don't exist, so report unsupported and
/// leave the rest of the function table NULL.
static bool supported(const ZL_cpuid_t* cpuid)
{
    (void)cpuid;
    return false;
}

const ZL_PivCoHuffmanEncode ZL_PivCoHuffmanEncode_avx512 = {
    .supported = supported,
};

#endif
