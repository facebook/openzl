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

ZL_AVX512_INLINE __m512i
loadRankIndices64(const uint8_t* ranks, uint8_t rankBegin)
{
    return _mm512_sub_epi8(
            _mm512_loadu_si512((const void*)ranks),
            _mm512_set1_epi8((char)rankBegin));
}

// Each packFlatDepthBlockN bit-packs 64 depth-N rank indices (LSB-first) into
// 8*N output bytes. Callers may pass fewer than 64 valid ranks for the tail:
// the block still reads 64 and writes 8*N bytes (relying on input/output slop).
// The valid ranks occupy the low bits; any over-read ranks land in the high
// bits, past the caller's valid bit count -- harmless, since those trailing
// bits are never read.

ZL_AVX512_INLINE void
packFlatDepthBlock1(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    const __m512i rankVec   = _mm512_loadu_si512((const void*)ranks);
    const __m512i threshold = _mm512_set1_epi8((char)(rankBegin + 1));
    const __mmask64 bits =
            _mm512_cmp_epu8_mask(rankVec, threshold, _MM_CMPINT_GE);
    ZL_writeLE64(out, (uint64_t)bits);
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
 * This is a single `maddubs`: it multiplies each even byte by 1
 * and each odd byte by `1 << depth`, summing the adjacent pair into a 16-bit
 * lane.
 */
ZL_AVX512_INLINE __m512i
packBytesToPairs16(__m512i indices, const size_t kDepth)
{
    const __m512i kPairMultiplier =
            _mm512_set1_epi16((short)(((1 << kDepth) << 8) | 1));
    // kPairMultiplier must be the first argument because the first argument is
    // unsigned and the second is signed. All indices are < 128, so it is fine
    // to make them signed. However, the odd multiplier for depth=7 is 2^7 =
    // 128, which is treated as negative by maddubs.
    assert(kDepth <= 7);
    return _mm512_maddubs_epi16(kPairMultiplier, indices);
}

/**
 * First fuses pairs with packBytesToPairs16, and then fuses those pairs into
 * quartets of indices packed in the low bits of 32-bit lanes.
 */
ZL_AVX512_INLINE __m512i
packBytesToQuads32(__m512i indices, const size_t kDepth)
{
    const __m512i kQuadMultiplier =
            _mm512_set1_epi32((int)(((1 << (2 * kDepth)) << 16) | 1));
    const __m512i pairs16 = packBytesToPairs16(indices, kDepth);
    return _mm512_madd_epi16(pairs16, kQuadMultiplier);
}

/**
 * First fuses quartets with packBytesToQuads32, and then fuses those quartets
 * into octets of indices packed in the low bits of 64-bit lanes.
 */
ZL_AVX512_INLINE __m512i
packBytesToOctets64(__m512i indices, const size_t kDepth)
{
    const __m512i kLowQuadMask =
            _mm512_set1_epi64((long long)((1ULL << (4 * kDepth)) - 1));
    const __m512i quads32 = packBytesToQuads32(indices, kDepth);
    const __m512i lo      = _mm512_and_si512(quads32, kLowQuadMask);
    const __m512i hi =
            _mm512_srli_epi64(quads32, (unsigned int)(32 - 4 * kDepth));
    return _mm512_or_si512(lo, _mm512_andnot_si512(kLowQuadMask, hi));
}

// Depth 2 can stop at quads because quartets of indices are byte aligned.
ZL_AVX512_INLINE void
packFlatDepthBlock2(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    const __m512i indices = loadRankIndices64(ranks, rankBegin);
    const __m512i quads32 = packBytesToQuads32(indices, 2);
    _mm512_mask_cvtepi32_storeu_epi8(out, (__mmask16)0xffff, quads32);
}

ZL_AVX512_INLINE __mmask64 packBytesToOctets64StoreMask(const size_t kDepth)
{
    const __mmask64 groupMask = tailMask64(kDepth);
    __mmask64 storeMask       = 0;
    for (size_t group = 0; group < 8; ++group) {
        storeMask |= (__mmask64)(groupMask << (group * 8));
    }
    return storeMask;
}

#    define ZL_DEFINE_PACK_FLAT_DEPTH_OCTET_BLOCK(DEPTH)                  \
        ZL_AVX512_INLINE void packFlatDepthBlock##DEPTH(                  \
                uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)    \
        {                                                                 \
            const __m512i indices  = loadRankIndices64(ranks, rankBegin); \
            const __m512i octets64 = packBytesToOctets64(indices, DEPTH); \
            _mm512_mask_compressstoreu_epi8(                              \
                    out, packBytesToOctets64StoreMask(DEPTH), octets64);  \
        }

ZL_DEFINE_PACK_FLAT_DEPTH_OCTET_BLOCK(3)

// Depth 4 can stop at pairs because pairs of indices are byte aligned.
ZL_AVX512_INLINE void
packFlatDepthBlock4(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    const __m512i indices = loadRankIndices64(ranks, rankBegin);
    const __m512i pairs16 = packBytesToPairs16(indices, 4);
    _mm512_mask_cvtepi16_storeu_epi8(out, (__mmask32)0xffffffff, pairs16);
}

ZL_DEFINE_PACK_FLAT_DEPTH_OCTET_BLOCK(5)
ZL_DEFINE_PACK_FLAT_DEPTH_OCTET_BLOCK(6)
ZL_DEFINE_PACK_FLAT_DEPTH_OCTET_BLOCK(7)

#    undef ZL_DEFINE_PACK_FLAT_DEPTH_OCTET_BLOCK

ZL_AVX512_INLINE void
packFlatDepthBlock8(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    _mm512_storeu_si512((void*)out, loadRankIndices64(ranks, rankBegin));
}

#    define ZL_DEFINE_PACK_FLAT_DEPTH(DEPTH)                      \
        static ZL_AVX512_ATTR void packFlatDepth##DEPTH(          \
                uint8_t* bitmap,                                  \
                const uint8_t* ranks,                             \
                size_t numRanks,                                  \
                uint8_t rankBegin)                                \
        {                                                         \
            assert(ZL_PIVCO_HUFFMAN_SLOP >= 64);                  \
            size_t idx    = 0;                                    \
            size_t outIdx = 0;                                    \
            for (; idx < numRanks; idx += 64) {                   \
                packFlatDepthBlock##DEPTH(                        \
                        bitmap + outIdx, ranks + idx, rankBegin); \
                outIdx += 8 * (DEPTH);                            \
            }                                                     \
        }

ZL_DEFINE_PACK_FLAT_DEPTH(1)
ZL_DEFINE_PACK_FLAT_DEPTH(2)
ZL_DEFINE_PACK_FLAT_DEPTH(3)
ZL_DEFINE_PACK_FLAT_DEPTH(4)
ZL_DEFINE_PACK_FLAT_DEPTH(5)
ZL_DEFINE_PACK_FLAT_DEPTH(6)
ZL_DEFINE_PACK_FLAT_DEPTH(7)
ZL_DEFINE_PACK_FLAT_DEPTH(8)

#    undef ZL_DEFINE_PACK_FLAT_DEPTH

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
            packFlatDepth1(bitmap, ranks, numRanks, rankBegin);
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
