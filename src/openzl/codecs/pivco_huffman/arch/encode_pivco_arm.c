// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/arch/encode_pivco_arch.h"

// The ARM encoding code is heavily inspired by upstream's implementation,
// see ./README.md for credits.

#if ZL_ARCH_ARM64

#    include <arm_neon.h>
#    include <assert.h>

#    include "common_pivco_neon_tables.h"
#    include "common_pivco_tables.h"

static bool supported(const ZL_cpuid_t* cpuid)
{
    (void)cpuid;
    return true;
}

ZL_FORCE_INLINE uint8x8_t rankMasks64(
        uint8x16_t r0,
        uint8x16_t r1,
        uint8x16_t r2,
        uint8x16_t r3,
        uint8x16_t threshold)
{
    const uint8_t kMasksArr[16] = {
        1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128,
    };
    const uint8x16_t kMasks   = vld1q_u8(kMasksArr);
    const uint8x16_t bits0    = vandq_u8(vcgeq_u8(r0, threshold), kMasks);
    const uint8x16_t bits1    = vandq_u8(vcgeq_u8(r1, threshold), kMasks);
    const uint8x16_t bits2    = vandq_u8(vcgeq_u8(r2, threshold), kMasks);
    const uint8x16_t bits3    = vandq_u8(vcgeq_u8(r3, threshold), kMasks);
    const uint8x16_t bits01   = vpaddq_u8(bits0, bits1);
    const uint8x16_t bits23   = vpaddq_u8(bits2, bits3);
    const uint8x16_t bits0123 = vpaddq_u8(bits01, bits23);
    return vget_low_u8(vpaddq_u8(bits0123, bits0123));
}

ZL_FORCE_INLINE uint8_t rankMask8(uint8x8_t ranks, uint8x8_t threshold)
{
    const uint8_t kMasks[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    return vaddv_u8(vand_u8(vcge_u8(ranks, threshold), vld1_u8(kMasks)));
}

ZL_FORCE_INLINE uint8x16_t compactRanks16(uint8x16_t ranks, uint16_t mask)
{
    const uint8_t loMask  = (uint8_t)mask;
    const uint8_t hiMask  = (uint8_t)(mask >> 8);
    const size_t hiOffset = ZL_kPivCoHuffmanNotPopcount8[loMask];
    const uint8x16_t lo   = vld1q_u8(ZL_kPivCoHuffmanPartitionLo[loMask]);
    const uint8x16_t hi =
            vld1q_u8(ZL_kPivCoHuffmanPartitionHiPadded[hiMask] + hiOffset);
    return vqtbl1q_u8(ranks, vminq_u8(lo, hi));
}

ZL_FORCE_INLINE uint8x16_t reverseBytes16(uint8x16_t value)
{
    const uint8_t kReverse[16] = {
        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    };
    return vqtbl1q_u8(value, vld1q_u8(kReverse));
}

ZL_FORCE_INLINE size_t partitionFullBlock16(
        uint8_t* bitmap,
        uint8_t* lhs,
        uint8_t* rhs,
        uint8x16_t ranks,
        uint8x8_t threshold,
        size_t lanes)
{
    uint16_t mask = rankMask8(vget_low_u8(ranks), threshold)
            | (uint16_t)(rankMask8(vget_high_u8(ranks), threshold) << 8);
    if (lanes < 16) {
        mask &= (uint16_t)((1u << lanes) - 1);
    }
    bitmap[0] = (uint8_t)mask;
    bitmap[1] = (uint8_t)(mask >> 8);

    uint8x16_t const compact = compactRanks16(ranks, mask);
    vst1q_u8(rhs, compact);
    vst1q_u8(lhs, reverseBytes16(compact));
    return ZL_kPivCoHuffmanNeonPopcount[(uint8_t)mask]
            + ZL_kPivCoHuffmanNeonPopcount[(uint8_t)(mask >> 8)];
}

ZL_FORCE_INLINE size_t partitionFullBlock64(
        uint8_t* bitmap,
        uint8_t* lhs,
        uint8_t* rhs,
        uint8x16_t r0,
        uint8x16_t r1,
        uint8x16_t r2,
        uint8x16_t r3,
        uint8x16_t threshold,
        size_t lanes)
{
    uint8x8_t masks   = rankMasks64(r0, r1, r2, r3, threshold);
    uint64_t maskWord = vget_lane_u64(vreinterpret_u64_u8(masks), 0);
    if (lanes < 64) {
        maskWord &= (1ULL << lanes) - 1;
        masks = vreinterpret_u8_u64(vcreate_u64(maskWord));
    }
    vst1_u8(bitmap, masks);

    const uint64_t countWord =
            vget_lane_u64(vreinterpret_u64_u8(vcnt_u8(masks)), 0);
    const uint64_t prefix = countWord * 0x0101010101010101ULL;

#    define ZL_SCATTER_FULL(K, RANKS)                                        \
        do {                                                                 \
            const size_t rightOffset = (K) == 0                              \
                    ? 0                                                      \
                    : (size_t)((prefix >> (8 * (2 * (K) - 1))) & 0xFF);      \
            const uint16_t mask      = (uint16_t)(maskWord >> (16 * (K)));   \
            const uint8x16_t compact = compactRanks16((RANKS), mask);        \
            vst1q_u8(rhs + rightOffset, compact);                            \
            vst1q_u8(lhs + 16 * (K) - rightOffset, reverseBytes16(compact)); \
        } while (0)

    ZL_SCATTER_FULL(0, r0);
    if (lanes > 16)
        ZL_SCATTER_FULL(1, r1);
    if (lanes > 32)
        ZL_SCATTER_FULL(2, r2);
    if (lanes > 48)
        ZL_SCATTER_FULL(3, r3);

#    undef ZL_SCATTER_FULL

    return (size_t)(prefix >> 56);
}

ZL_FORCE_INLINE size_t partitionRightBlock64(
        uint8_t* bitmap,
        uint8_t* rhs,
        uint8x16_t r0,
        uint8x16_t r1,
        uint8x16_t r2,
        uint8x16_t r3,
        uint8x16_t threshold,
        size_t lanes)
{
    uint8x8_t masks   = rankMasks64(r0, r1, r2, r3, threshold);
    uint64_t maskWord = vget_lane_u64(vreinterpret_u64_u8(masks), 0);
    if (lanes < 64) {
        maskWord &= (1ULL << lanes) - 1;
        masks = vreinterpret_u8_u64(vcreate_u64(maskWord));
    }
    vst1_u8(bitmap, masks);

    const uint64_t countWord =
            vget_lane_u64(vreinterpret_u64_u8(vcnt_u8(masks)), 0);
    const uint64_t prefix = countWord * 0x0101010101010101ULL;

    // Only the right ranks at the front of each compacted vector are live; the
    // reversed left ranks in the tail are overwritten by the next group's
    // store, or land in the caller's slop for the last one.
#    define ZL_SCATTER_RIGHT(K, RANKS)                                  \
        do {                                                            \
            const size_t offset = (K) == 0                              \
                    ? 0                                                 \
                    : (size_t)((prefix >> (8 * (2 * (K) - 1))) & 0xFF); \
            const uint16_t mask = (uint16_t)(maskWord >> (16 * (K)));   \
            vst1q_u8(rhs + offset, compactRanks16((RANKS), mask));      \
        } while (0)

    ZL_SCATTER_RIGHT(0, r0);
    if (lanes > 16)
        ZL_SCATTER_RIGHT(1, r1);
    if (lanes > 32)
        ZL_SCATTER_RIGHT(2, r2);
    if (lanes > 48)
        ZL_SCATTER_RIGHT(3, r3);

#    undef ZL_SCATTER_RIGHT

    return (size_t)(prefix >> 56);
}

static size_t partitionFull(
        uint8_t* bitmap,
        uint8_t* lhs,
        uint8_t* rhs,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    const uint8x16_t threshold = vdupq_n_u8(rightRank);
    size_t ones                = 0;
    size_t zeros               = 0;
    size_t i                   = 0;
    for (; i + 64 <= numRanks; i += 64) {
        const uint8x16_t r0    = vld1q_u8(ranks + i);
        const uint8x16_t r1    = vld1q_u8(ranks + i + 16);
        const uint8x16_t r2    = vld1q_u8(ranks + i + 32);
        const uint8x16_t r3    = vld1q_u8(ranks + i + 48);
        size_t const blockOnes = partitionFullBlock64(
                bitmap + i / 8,
                lhs + zeros,
                rhs + ones,
                r0,
                r1,
                r2,
                r3,
                threshold,
                64);
        ones += blockOnes;
        zeros += 64 - blockOnes;
    }
    if (i < numRanks) {
        const uint8x16_t r0 = vld1q_u8(ranks + i);
        const uint8x16_t r1 = vld1q_u8(ranks + i + 16);
        const uint8x16_t r2 = vld1q_u8(ranks + i + 32);
        const uint8x16_t r3 = vld1q_u8(ranks + i + 48);
        ones += partitionFullBlock64(
                bitmap + i / 8,
                lhs + zeros,
                rhs + ones,
                r0,
                r1,
                r2,
                r3,
                threshold,
                numRanks - i);
    }
    return ones;
}

static size_t partitionRight(
        uint8_t* bitmap,
        uint8_t* rhs,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    const uint8x16_t threshold = vdupq_n_u8(rightRank);
    size_t ones                = 0;
    size_t i                   = 0;
    for (; i + 64 <= numRanks; i += 64) {
        const uint8x16_t r0 = vld1q_u8(ranks + i);
        const uint8x16_t r1 = vld1q_u8(ranks + i + 16);
        const uint8x16_t r2 = vld1q_u8(ranks + i + 32);
        const uint8x16_t r3 = vld1q_u8(ranks + i + 48);
        ones += partitionRightBlock64(
                bitmap + i / 8, rhs + ones, r0, r1, r2, r3, threshold, 64);
    }
    if (i < numRanks) {
        const uint8x16_t r0 = vld1q_u8(ranks + i);
        const uint8x16_t r1 = vld1q_u8(ranks + i + 16);
        const uint8x16_t r2 = vld1q_u8(ranks + i + 32);
        const uint8x16_t r3 = vld1q_u8(ranks + i + 48);
        ones += partitionRightBlock64(
                bitmap + i / 8,
                rhs + ones,
                r0,
                r1,
                r2,
                r3,
                threshold,
                numRanks - i);
    }
    return ones;
}

static void partitionNone(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rightRank)
{
    const uint8x16_t threshold = vdupq_n_u8(rightRank);
    size_t i                   = 0;
    for (; i < numRanks; i += 64) {
        const uint8x16_t r0 = vld1q_u8(ranks + i);
        const uint8x16_t r1 = vld1q_u8(ranks + i + 16);
        const uint8x16_t r2 = vld1q_u8(ranks + i + 32);
        const uint8x16_t r3 = vld1q_u8(ranks + i + 48);
        vst1_u8(bitmap + i / 8, rankMasks64(r0, r1, r2, r3, threshold));
    }
    if ((numRanks & 7) != 0) {
        bitmap[numRanks / 8] &= (uint8_t)((1u << (numRanks & 7)) - 1);
    }
}

static void packFlatDepth1(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rankBegin)
{
    partitionNone(bitmap, ranks, numRanks, rankBegin + 1);
}

// Folds each adjacent (even, odd) index pair into one byte holding
// even | (odd << depth), where @p shifts is {0, depth, 0, depth, ...}. The two
// lanes contribute one rankBegin each, so @p bias removes both at once:
// (1 + (1 << depth)) * rankBegin. Requires 2 * depth <= 8 so the pair fits.
ZL_FORCE_INLINE uint8x16_t
packIndexPairs(uint8x16_t r0, uint8x16_t r1, int8x16_t shifts, uint8x16_t bias)
{
    return vsubq_u8(
            vpaddq_u8(vshlq_u8(r0, shifts), vshlq_u8(r1, shifts)), bias);
}

// Gather controls for packFields(), indexed by kWidth - 5. Each row keeps the
// 2 * kWidth payload bytes of the two 64-bit lanes and drops the rest; 0xff is
// out of range for a 16-byte table and so gathers zero.
static const uint8_t kPackFieldsCompact[3][16] = {
    { 1, 2, 3, 4, 5, 9, 10, 11, 12, 13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14, 0xff, 0xff, 0xff, 0xff },
    { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 0xff, 0xff },
};

// Concatenates the @p kWidth-bit field held at the bottom of each of 16 bytes
// into 2 * kWidth packed bytes, LSB-first, gathered into the low lanes.
//
// Each level views the register at the next wider granularity and shifts the
// even lanes up while shifting the odd lanes down, closing the gap between
// neighbours by exactly the slack that granularity carries. After the third
// level the 8 * kWidth-bit group sits at bit 32 - 4 * kWidth of its 64-bit
// lane, which is a byte boundary for even widths; an odd width needs one more
// 4-bit nudge before the byte shuffle can gather the groups.
//
// @p kWidth must be a compile-time constant in [5, 7] so the shift vectors and
// the gather control fold away.
ZL_FORCE_INLINE uint8x16_t packFields(uint8x16_t codes, const size_t kWidth)
{
    assert(kWidth >= 5 && kWidth <= 7);

    const int8_t s1          = (int8_t)(8 - kWidth);
    const int16_t s2         = (int16_t)(8 - kWidth);
    const int16_t s2Neg      = (int16_t)(kWidth - 8);
    const int32_t s3         = (int32_t)(16 - 2 * kWidth);
    const int32_t s3Neg      = (int32_t)(2 * kWidth - 16);
    const int8_t kShift1[16] = {
        s1, 0, s1, 0, s1, 0, s1, 0, s1, 0, s1, 0, s1, 0, s1, 0,
    };
    const int16_t kShift2[8] = { s2, s2Neg, s2, s2Neg, s2, s2Neg, s2, s2Neg };
    const int32_t kShift3[4] = { s3, s3Neg, s3, s3Neg };

    const uint16x8_t words16 =
            vreinterpretq_u16_u8(vshlq_u8(codes, vld1q_s8(kShift1)));
    const uint32x4_t words32 =
            vreinterpretq_u32_u16(vshlq_u16(words16, vld1q_s16(kShift2)));
    uint64x2_t words64 =
            vreinterpretq_u64_u32(vshlq_u32(words32, vld1q_s32(kShift3)));
    if ((kWidth & 1) != 0) {
        words64 = vshrq_n_u64(words64, 4);
    }
    return vqtbl1q_u8(
            vreinterpretq_u8_u64(words64),
            vld1q_u8(kPackFieldsCompact[kWidth - 5]));
}

static void packFlatDepth2(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rankBegin)
{
    const int8_t kShiftArr[16] = {
        0, 2, 4, 6, 0, 2, 4, 6, 0, 2, 4, 6, 0, 2, 4, 6,
    };
    const int8x16_t kShift = vld1q_s8(kShiftArr);
    // Pack rankBegin 4x into a byte
    const uint8x16_t basePacked = vdupq_n_u8((uint8_t)(0x55 * rankBegin));
    for (size_t i = 0; i < numRanks; i += 64) {
        const uint8x16_t bits0    = vshlq_u8(vld1q_u8(ranks + i), kShift);
        const uint8x16_t bits1    = vshlq_u8(vld1q_u8(ranks + i + 16), kShift);
        const uint8x16_t bits2    = vshlq_u8(vld1q_u8(ranks + i + 32), kShift);
        const uint8x16_t bits3    = vshlq_u8(vld1q_u8(ranks + i + 48), kShift);
        const uint8x16_t bits01   = vpaddq_u8(bits0, bits1);
        const uint8x16_t bits23   = vpaddq_u8(bits2, bits3);
        const uint8x16_t bits0123 = vpaddq_u8(bits01, bits23);
        vst1q_u8(bitmap + i / 4, vsubq_u8(bits0123, basePacked));
    }
    if ((numRanks & 3) != 0) {
        size_t const bits = numRanks * 2;
        bitmap[bits / 8] &= (uint8_t)((1u << (bits & 7)) - 1);
    }
}

static void packFlatDepth3(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rankBegin)
{
    const int8_t kPairShift[16] = {
        0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3,
    };
    const int8x16_t pairShift = vld1q_s8(kPairShift);
    // Pack rankBegin twice into a byte
    const uint8x16_t basePacked = vdupq_n_u8((uint8_t)(0x9 * rankBegin));
    for (size_t i = 0; i < numRanks; i += 64) {
        // Two 3-bit indices per byte is a 6-bit field, so the width-6 cascade
        // finishes the job unchanged.
        const uint8x16_t packed = packFields(
                packIndexPairs(
                        vld1q_u8(ranks + i),
                        vld1q_u8(ranks + i + 16),
                        pairShift,
                        basePacked),
                6);
        // Each group owns 12 bytes; the 4-byte tail of the store is rewritten
        // by the next group, or lands in the caller's slop for the last one.
        // Storing the whole vector beats an exact 8+4 store here, unlike in the
        // depth 5-7 loops.
        uint8_t* const out = bitmap + i * 3 / 8;
        vst1q_u8(out, packed);

        if (i + 32 >= numRanks) {
            continue;
        }

        const uint8x16_t packed2 = packFields(
                packIndexPairs(
                        vld1q_u8(ranks + i + 32),
                        vld1q_u8(ranks + i + 48),
                        pairShift,
                        basePacked),
                6);
        vst1q_u8(out + 12, packed2);
    }
    const size_t bits = numRanks * 3;
    if ((bits & 7) != 0) {
        bitmap[bits / 8] &= (uint8_t)((1u << (bits & 7)) - 1);
    }
}

static void packFlatDepth4(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rankBegin)
{
    const int8_t kShifts[16] = {
        0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4,
    };
    const int8x16_t shifts      = vld1q_s8(kShifts);
    const uint8x16_t basePacked = vdupq_n_u8((uint8_t)(0x11 * rankBegin));
    for (size_t i = 0; i < numRanks; i += 32) {
        // Two 4-bit indices exactly fill a byte, so the pair fold is the whole
        // job.
        vst1q_u8(
                bitmap + i / 2,
                packIndexPairs(
                        vld1q_u8(ranks + i),
                        vld1q_u8(ranks + i + 16),
                        shifts,
                        basePacked));
    }
    if ((numRanks & 1) != 0) {
        bitmap[numRanks / 2] &= 0x0f;
    }
}

// Width 6 stores exactly the 12 bytes the group owns, which measures faster
// than a full vector store at its 12-byte output stride. Widths 5 and 7 round
// up to a full vector store, spilling into the bytes the next group overwrites
// or into the caller's slop; exact stores were slower for both.
ZL_FORCE_INLINE void
storePackedFields(uint8_t* out, uint8x16_t packed, const size_t kWidth)
{
    if (kWidth == 6) {
        vst1_u8(out, vget_low_u8(packed));
        vst1q_lane_u32(
                (uint32_t*)(void*)(out + 8), vreinterpretq_u32_u8(packed), 2);
    } else {
        vst1q_u8(out, packed);
    }
}

// Depths 5..7 hold one index per byte, so the cascade runs on the raw indices
// with no pair fold.
#    define ZL_DEFINE_PACK_FLAT_DEPTH_567(DEPTH)                              \
        static void packFlatDepth##DEPTH(                                     \
                uint8_t* bitmap,                                              \
                const uint8_t* ranks,                                         \
                size_t numRanks,                                              \
                uint8_t rankBegin)                                            \
        {                                                                     \
            const uint8x16_t base = vdupq_n_u8(rankBegin);                    \
            for (size_t i = 0; i < numRanks; i += 16) {                       \
                uint8x16_t const packed = packFields(                         \
                        vsubq_u8(vld1q_u8(ranks + i), base), (DEPTH));        \
                storePackedFields(bitmap + i * (DEPTH) / 8, packed, (DEPTH)); \
            }                                                                 \
            const size_t bits = numRanks * (DEPTH);                           \
            if ((bits & 7) != 0) {                                            \
                bitmap[bits / 8] &= (uint8_t)((1u << (bits & 7)) - 1);        \
            }                                                                 \
        }

ZL_DEFINE_PACK_FLAT_DEPTH_567(5)
ZL_DEFINE_PACK_FLAT_DEPTH_567(6)
ZL_DEFINE_PACK_FLAT_DEPTH_567(7)

#    undef ZL_DEFINE_PACK_FLAT_DEPTH_567

static void packFlatDepth8(
        uint8_t* bitmap,
        const uint8_t* ranks,
        size_t numRanks,
        uint8_t rankBegin)
{
    const uint8x16_t base = vdupq_n_u8(rankBegin);
    for (size_t i = 0; i < numRanks; i += 64) {
        const uint8x16_t r0 = vld1q_u8(ranks + i);
        const uint8x16_t r1 = vld1q_u8(ranks + i + 16);
        const uint8x16_t r2 = vld1q_u8(ranks + i + 32);
        const uint8x16_t r3 = vld1q_u8(ranks + i + 48);
        vst1q_u8(bitmap + i, vsubq_u8(r0, base));
        vst1q_u8(bitmap + i + 16, vsubq_u8(r1, base));
        vst1q_u8(bitmap + i + 32, vsubq_u8(r2, base));
        vst1q_u8(bitmap + i + 48, vsubq_u8(r3, base));
    }
}

static void packFlatDepth(
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
        case 8:
            packFlatDepth8(bitmap, ranks, numRanks, rankBegin);
            return;
        default:
            ZL_PivCoHuffmanEncode_generic.packFlatDepth(
                    bitmap, depth, ranks, numRanks, rankBegin);
            return;
    }
}

const ZL_PivCoHuffmanEncode ZL_PivCoHuffmanEncode_arm = {
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

const ZL_PivCoHuffmanEncode ZL_PivCoHuffmanEncode_arm = {
    .supported = supported,
};

#endif
