// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/arch/encode_pivco_arch.h"

#if ZL_ARCH_X86_64

#    include <immintrin.h>
#    include <string.h>

#    include "common_pivco_avx2_tables.h"
#    include "common_pivco_tables.h"
#    include "openzl/shared/bits.h"
#    include "openzl/shared/mem.h"

#    define ZL_AVX2_ATTR ZL_TARGET_ATTRIBUTE("avx2,bmi2,ssse3,sse4.1")
#    define ZL_AVX2_INLINE ZL_FORCE_INLINE ZL_AVX2_ATTR

static bool supported(const ZL_cpuid_t* cpuid)
{
#    if ZL_HAS_AVX2 && ZL_HAS_BMI2
    (void)cpuid;
    return true;
#    else
    return cpuid != NULL && ZL_cpuid_avx2(*cpuid) && ZL_cpuid_bmi2(*cpuid);
#    endif
}

ZL_AVX2_INLINE uint32_t rankMask32(const uint8_t* ranks, uint8_t kRightRank)
{
    const __m256i kThreshold = _mm256_set1_epi8((char)kRightRank);

    const __m256i rankVec = _mm256_loadu_si256((const __m256i_u*)ranks);
    // A set bit marks a "right" rank (rank >= kRightRank)
    const __m256i ge =
            _mm256_cmpeq_epi8(_mm256_max_epu8(rankVec, kThreshold), rankVec);
    return (uint32_t)_mm256_movemask_epi8(ge);
}

ZL_AVX2_INLINE void
writeMask32(uint8_t* bitmap, size_t bitOffset, uint32_t bits, size_t lanes)
{
    ZL_writeLE64_N(bitmap + bitOffset / 8, (uint64_t)bits, (lanes + 7) / 8);
}

// Compacts 16 ranks (one __m128i) into a single vector laid out as
// [ right ranks ascending | left ranks descending ], using a single pshufb.
// @p mask16 is the partition bitmap for the 16 ranks (bit set => right).
//
// The shuffle control is assembled from two half-tables. The low control,
// ZL_kPivCoHuffmanPartitionLo[loMask], places the low 8 source bytes (0..7):
// right ranks at the front, left ranks reversed at the far back. The high
// control comes from the 0x80-padded ZL_kPivCoHuffmanPartitionHiPadded[hiMask],
// loaded at byte offset (8 - popcount(loMask)) -- the low half's left count,
// read directly from ZL_kPivCoHuffmanNotPopcount8 -- so the misaligned load
// slides its source bytes 8..15 into the lanes between the two low-half runs.
// Each table holds 0x80 in the lanes it does not fill, and the two fill
// disjoint lanes that together cover all 16 outputs -- so an unsigned min keeps
// each lane's real index (0..15, always < 0x80) and discards the other table's
// pad, yielding the full control for one pshufb.
ZL_AVX2_INLINE __m128i compactRanks16(__m128i ranks, uint16_t mask16)
{
    const uint8_t loMask = (uint8_t)mask16;
    const uint8_t hiMask = (uint8_t)(mask16 >> 8);
    // 8 - popcount(loMask): the low half's left count, used directly as the
    // high control's load offset.
    const size_t hiOffset = (size_t)ZL_kPivCoHuffmanNotPopcount8[loMask];

    const __m128i loCtrl =
            _mm_load_si128((const __m128i*)ZL_kPivCoHuffmanPartitionLo[loMask]);
    const __m128i hiCtrl = _mm_loadu_si128(
            (const __m128i_u*)(ZL_kPivCoHuffmanPartitionHiPadded[hiMask]
                               + hiOffset));

    return _mm_shuffle_epi8(ranks, _mm_min_epu8(loCtrl, hiCtrl));
}

ZL_AVX2_INLINE __m128i reverseBytes16(__m128i v)
{
    const __m128i control =
            _mm_setr_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    return _mm_shuffle_epi8(v, control);
}

// Scatters one 32-rank block into `lhs`/`rhs` given its partition mask `bits`
// (bit set => right, i.e. rank >= rightRank). Right ranks are appended
// ascending to `rhs` starting at *ones; left ranks descending to `lhs` starting
// at *zeros; both cursors then advance by their per-side counts. Handled as two
// 16-lane halves via compactRanks16, which lays each half out as [right
// ascending | left descending] -- so `rhs` takes it directly and `lhs` takes
// the byte-reverse.
//
// For a partial tail block, clear the mask bits beyond the valid lanes before
// calling: those lanes then count as left, so *ones and the `rhs` contents stay
// exact and the (garbage) over-read ranks land after the valid left ranks in
// `lhs`, past the caller's left count.
ZL_AVX2_INLINE void partitionFullBlock(
        uint8_t* bitmap,
        uint8_t* lhs,
        uint8_t* rhs,
        const uint8_t* ranks,
        uint32_t bits,
        size_t* ones,
        size_t* zeros)
{
    memcpy(bitmap, &bits, sizeof(bits));

    const __m128i r0 = _mm_loadu_si128((const __m128i_u*)ranks);
    const __m128i r1 = _mm_loadu_si128((const __m128i_u*)(ranks + 16));
    const __m128i v0 = compactRanks16(r0, (uint16_t)bits);
    const __m128i v1 = compactRanks16(r1, (uint16_t)(bits >> 16));

    size_t onesCur  = *ones;
    size_t zerosCur = *zeros;

    size_t const ones0 = (size_t)ZL_popcount64((uint64_t)(bits & 0xFFFF));
    _mm_storeu_si128((__m128i_u*)(rhs + onesCur), v0);
    _mm_storeu_si128((__m128i_u*)(lhs + zerosCur), reverseBytes16(v0));
    onesCur += ones0;
    zerosCur += 16 - ones0;

    size_t const ones1 = (size_t)ZL_popcount64((uint64_t)(bits >> 16));
    _mm_storeu_si128((__m128i_u*)(rhs + onesCur), v1);
    _mm_storeu_si128((__m128i_u*)(lhs + zerosCur), reverseBytes16(v1));
    onesCur += ones1;
    zerosCur += 16 - ones1;

    *ones  = onesCur;
    *zeros = zerosCur;
}

static ZL_AVX2_ATTR size_t partitionFull(
        uint8_t* bitmap,
        uint8_t* lhs,
        uint8_t* rhs,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    // The tail block over-reads `ranks` and over-writes `lhs`/`rhs` by up to a
    // full 32-rank block; ZL_PIVCO_HUFFMAN_SLOP covers it.
    assert(ZL_PIVCO_HUFFMAN_SLOP >= 32);
    size_t ones  = 0;
    size_t zeros = 0;
    size_t i     = 0;
    for (; i + 32 <= numRanks; i += 32) {
        uint32_t const bits = rankMask32(ranks + i, rightRank);
        partitionFullBlock(
                bitmap + i / 8, lhs, rhs, ranks + i, bits, &ones, &zeros);
    }
    if (i < numRanks) {
        size_t const lanes  = numRanks - i;
        uint32_t const mask = ((uint32_t)1 << lanes) - 1;
        uint32_t const bits = rankMask32(ranks + i, rightRank) & mask;
        partitionFullBlock(
                bitmap + i / 8, lhs, rhs, ranks + i, bits, &ones, &zeros);
    }
    return ones;
}

// Writes the 4-byte partition mask `bits` to `bitmap` and scatters the "right"
// ranks (rank >= rightRank, bit set) of one 32-rank block ascending into `rhs`
// starting at *ones, then advances *ones by the count. Handled as two 16-lane
// halves via compactRanks16, which puts the right ranks at the front of each
// half.
//
// For a partial tail block, clear the mask bits beyond the valid lanes before
// calling: those lanes then count as left and are excluded, so *ones and the
// `rhs` contents stay exact (the block still touches a full 32 ranks / 4 bitmap
// bytes, relying on buffer slop).
ZL_AVX2_INLINE void partitionRightBlock(
        uint8_t* bitmap,
        uint8_t* rhs,
        const uint8_t* ranks,
        uint32_t bits,
        size_t* ones)
{
    memcpy(bitmap, &bits, sizeof(bits));

    const __m128i r0 = _mm_loadu_si128((const __m128i_u*)ranks);
    const __m128i r1 = _mm_loadu_si128((const __m128i_u*)(ranks + 16));
    const __m128i v0 = compactRanks16(r0, (uint16_t)bits);
    const __m128i v1 = compactRanks16(r1, (uint16_t)(bits >> 16));

    size_t onesCur = *ones;
    _mm_storeu_si128((__m128i_u*)(rhs + onesCur), v0);
    onesCur += (size_t)ZL_popcount64((uint64_t)(bits & 0xFFFF));
    _mm_storeu_si128((__m128i_u*)(rhs + onesCur), v1);
    onesCur += (size_t)ZL_popcount64((uint64_t)(bits >> 16));
    *ones = onesCur;
}

static ZL_AVX2_ATTR size_t partitionRight(
        uint8_t* bitmap,
        uint8_t* rhs,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    // The tail block over-reads `ranks` and over-writes `bitmap`/`rhs` by up to
    // a full 32-rank block; ZL_PIVCO_HUFFMAN_SLOP covers it.
    assert(ZL_PIVCO_HUFFMAN_SLOP >= 32);
    size_t ones = 0;
    size_t i    = 0;
    for (; i + 32 <= numRanks; i += 32) {
        uint32_t const bits = rankMask32(ranks + i, rightRank);
        partitionRightBlock(bitmap + i / 8, rhs, ranks + i, bits, &ones);
    }
    if (i < numRanks) {
        size_t const lanes  = numRanks - i;
        uint32_t const mask = ((uint32_t)1 << lanes) - 1;
        uint32_t const bits = rankMask32(ranks + i, rightRank) & mask;
        partitionRightBlock(bitmap + i / 8, rhs, ranks + i, bits, &ones);
    }
    return ones;
}

static ZL_AVX2_ATTR void partitionNone(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    size_t i = 0;
    for (; i + 32 <= numRanks; i += 32) {
        const uint32_t bits = rankMask32(ranks + i, rightRank);
        memcpy(bitmap + i / 8, &bits, sizeof(bits));
    }
    if (i < numRanks) {
        size_t const lanes  = numRanks - i;
        uint32_t const mask = ((uint32_t)1 << lanes) - 1;
        uint32_t const bits = rankMask32(ranks + i, rightRank) & mask;
        memcpy(bitmap + i / 8, &bits, sizeof(bits));
    }
}

ZL_AVX2_INLINE __m256i
loadRankIndices32(const uint8_t* ranks, uint8_t rankBegin)
{
    return _mm256_sub_epi8(
            _mm256_loadu_si256((const __m256i_u*)ranks),
            _mm256_set1_epi8((char)rankBegin));
}

// Each packFlatDepthBlockN bit-packs 32 depth-N rank indices (LSB-first) into
// 4*N output bytes. Callers may pass fewer than 32 valid ranks for the tail:
// the block still reads 32 and writes 4*N bytes (relying on input/output slop).
// The valid ranks occupy the low bits; any over-read ranks land in the high
// bits, past the caller's valid bit count -- harmless, since the bitmap region
// is byte-aligned and those trailing bits are never read.

ZL_AVX2_INLINE void
packFlatDepthBlock1(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    const uint32_t bits = rankMask32(ranks, rankBegin + 1);
    memcpy(out, &bits, sizeof(bits));
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
ZL_AVX2_INLINE __m256i packBytesToPairs16(__m256i indices, const size_t kDepth)
{
    const __m256i kPairMultiplier =
            _mm256_set1_epi16((short)(((1 << kDepth) << 8) | 1));
    // kPairMultiplier must be the first argument because the first argument is
    // unsigned and the second is signed. All indices are < 128, so it is fine
    // to make them signed. However, the odd multplier for depth=7 is 2^7 = 128,
    // which is treated as negative by maddubs.
    assert(kDepth <= 7);
    return _mm256_maddubs_epi16(kPairMultiplier, indices);
}

/**
 * First fuses pairs with packBytesToPairs16, and then fuses those pairs into
 * quartets of indices packed in the low bits of 32-bit lanes.
 */
ZL_AVX2_INLINE __m256i packBytesToQuads32(__m256i indices, const size_t kDepth)
{
    const __m256i kQuadMultiplier =
            _mm256_set1_epi32(((1 << (2 * kDepth)) << 16) | 1);
    const __m256i pairs16 = packBytesToPairs16(indices, kDepth);
    return _mm256_madd_epi16(pairs16, kQuadMultiplier);
}

/**
 * First fuses quartets with packBytesToQuads32, and then fuses those quartets
 * into octets of indices packed in the low bits of 64-bit lanes.
 */
ZL_AVX2_INLINE __m256i packBytesToOctets64(__m256i indices, const size_t kDepth)
{
    const __m256i kLowQuadMask = _mm256_set1_epi64x((1LL << (4 * kDepth)) - 1);
    const __m256i quads32      = packBytesToQuads32(indices, kDepth);
    const __m256i lo           = _mm256_and_si256(quads32, kLowQuadMask);
    const __m256i hi = _mm256_srli_epi64(quads32, (int)(32 - 4 * kDepth));
    return _mm256_or_si256(lo, _mm256_andnot_si256(kLowQuadMask, hi));
}

// Depth 2 can stop at quads because quartets of indices are byte aligned
ZL_AVX2_INLINE void
packFlatDepthBlock2(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    // Pack low byte of each 32-bit lane into the low 4 bytes of each 128-bit
    // lane
    const __m256i kCompress = _mm256_broadcastsi128_si256(_mm_setr_epi8(
            0, 4, 8, 12, -1, -1, -1, -1, -1, -1 - 1, -1, -1, -1, -1, -1, -1));

    const __m256i indices = loadRankIndices32(ranks, rankBegin);
    const __m256i quads32 = packBytesToQuads32(indices, 2);
    const __m256i packed  = _mm256_shuffle_epi8(quads32, kCompress);
    // Low dword of lane 0 = bytes 0..3, low dword of lane 1 = bytes 4..7;
    // interleave the two low dwords so the 8 packed bytes are contiguous.
    const __m128i lo = _mm256_castsi256_si128(packed);
    const __m128i hi = _mm256_extracti128_si256(packed, 1);
    _mm_storel_epi64((__m128i_u*)out, _mm_unpacklo_epi32(lo, hi));
}

ZL_AVX2_INLINE void
packFlatDepthBlock3(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    // Pack the low 3 bytes of each 32-bit lane into the low 12 bytes of the
    // first 128-bit lane
    const __m256i kCompress = _mm256_setr_m128i(
            _mm_setr_epi8(
                    0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, -1, -1, -1, -1),
            _mm_set1_epi8((char)0x80));

    const __m256i indices  = loadRankIndices32(ranks, rankBegin);
    const __m256i octets64 = packBytesToOctets64(indices, 3);
    // Pack the low 4 bytes of each 64-bit lane into the first 4 32-bit lanes.
    // This allows us to write all 32 packed indices in a single store.
    const __m256i octets32 = _mm256_permutevar8x32_epi32(
            octets64, _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7));
    const __m256i result = _mm256_shuffle_epi8(octets32, kCompress);
    _mm_storeu_si128((__m128i_u*)out, _mm256_castsi256_si128(result));
}

// Depth 4 can stop at pairs because pairs of indices are byte aligned.
ZL_AVX2_INLINE void
packFlatDepthBlock4(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    // Pack low byte of each 16-bit lane into the low 8 bytes of each 128-bit
    // lane
    const __m256i kCompress = _mm256_broadcastsi128_si256(_mm_setr_epi8(
            0, 2, 4, 6, 8, 10, 12, 14, -1, -1 - 1, -1, -1, -1, -1, -1, -1));

    const __m256i indices  = loadRankIndices32(ranks, rankBegin);
    const __m256i pairs16  = packBytesToPairs16(indices, 4);
    const __m256i gathered = _mm256_shuffle_epi8(pairs16, kCompress);
    const __m256i result   = _mm256_permute4x64_epi64(gathered, 0xD8);
    _mm_storeu_si128((__m128i_u*)out, _mm256_castsi256_si128(result));
}

ZL_AVX2_INLINE void
packFlatDepthBlock5(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    const __m256i kCompress = _mm256_broadcastsi128_si256(_mm_setr_epi8(
            0, 1, 2, 3, 4, 8, 9, 10, 11, 12, -1, -1, -1, -1, -1, -1));

    const __m256i indices  = loadRankIndices32(ranks, rankBegin);
    const __m256i octets64 = packBytesToOctets64(indices, 5);
    const __m256i packed   = _mm256_shuffle_epi8(octets64, kCompress);
    _mm_storeu_si128((__m128i_u*)out, _mm256_castsi256_si128(packed));
    _mm_storeu_si128(
            (__m128i_u*)(out + 2 * 5), _mm256_extracti128_si256(packed, 1));
}

ZL_AVX2_INLINE void
packFlatDepthBlock6(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    const __m256i kCompress = _mm256_broadcastsi128_si256(_mm_setr_epi8(
            0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1));

    const __m256i indices  = loadRankIndices32(ranks, rankBegin);
    const __m256i octets64 = packBytesToOctets64(indices, 6);
    const __m256i packed   = _mm256_shuffle_epi8(octets64, kCompress);
    _mm_storeu_si128((__m128i_u*)out, _mm256_castsi256_si128(packed));
    _mm_storeu_si128(
            (__m128i_u*)(out + 2 * 6), _mm256_extracti128_si256(packed, 1));
}

ZL_AVX2_INLINE void
packFlatDepthBlock7(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    const __m256i kCompress = _mm256_broadcastsi128_si256(_mm_setr_epi8(
            0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, -1, -1));

    const __m256i indices   = loadRankIndices32(ranks, rankBegin);
    const __m256i octects64 = packBytesToOctets64(indices, 7);
    const __m256i packed    = _mm256_shuffle_epi8(octects64, kCompress);
    _mm_storeu_si128((__m128i_u*)out, _mm256_castsi256_si128(packed));
    _mm_storeu_si128(
            (__m128i_u*)(out + 2 * 7), _mm256_extracti128_si256(packed, 1));
}

ZL_AVX2_INLINE void
packFlatDepthBlock8(uint8_t* out, const uint8_t* ranks, uint8_t rankBegin)
{
    _mm256_storeu_si256((__m256i_u*)out, loadRankIndices32(ranks, rankBegin));
}

#    define ZL_DEFINE_PACK_FLAT_DEPTH(DEPTH)                           \
        static ZL_AVX2_ATTR void packFlatDepth##DEPTH(                 \
                uint8_t* bitmap,                                       \
                const uint8_t* ranks,                                  \
                size_t numRanks,                                       \
                uint8_t rankBegin)                                     \
        {                                                              \
            size_t idx    = 0;                                         \
            size_t outIdx = 0;                                         \
            for (; idx < numRanks; idx += 64) {                        \
                packFlatDepthBlock##DEPTH(                             \
                        bitmap + outIdx, ranks + idx, rankBegin);      \
                outIdx += 4 * (DEPTH);                                 \
                packFlatDepthBlock##DEPTH(                             \
                        bitmap + outIdx, ranks + idx + 32, rankBegin); \
                outIdx += 4 * (DEPTH);                                 \
            }                                                          \
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

static ZL_AVX2_ATTR void packFlatDepth(
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
            packFlatDepth8(bitmap, ranks, numRanks, rankBegin);
            return;
    }
}

const ZL_PivCoHuffmanEncode ZL_PivCoHuffmanEncode_avx2 = {
    .supported      = supported,
    .partitionFull  = partitionFull,
    .partitionRight = partitionRight,
    .partitionNone  = partitionNone,
    .packFlatDepth  = packFlatDepth,
};

#else

static bool supported(const ZL_cpuid_t* cpuid)
{
    (void)cpuid;
    return false;
}

const ZL_PivCoHuffmanEncode ZL_PivCoHuffmanEncode_avx2 = {
    .supported = supported,
};

#endif
