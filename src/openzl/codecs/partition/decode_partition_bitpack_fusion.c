// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/partition/decode_partition_bitpack_fusion.h"

#include "openzl/codecs/bitpack/decode_bitpack_binding.h"
#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/codecs/partition/common_partition.h"
#include "openzl/common/wire_format.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/numeric_operations.h"
#include "openzl/zl_input.h"
#include "openzl/zl_output.h"
#include "openzl/zl_portability.h"

#if ZL_HAS_SVE2_BITPERM
#    include <arm_neon.h>
#    include <arm_neon_sve_bridge.h>
#endif

// ---------------------------------------------------------------------------
// LUT builders: expand per-bucket base/mask arrays into packed 2-element LUTs
// ---------------------------------------------------------------------------

/// Build expanded LUT for nbBits=4: 2^4=16 raw entries -> 2^8=256 expanded.
ZL_UNUSED_ATTR static void expandLUT4(
        const uint16_t* LUTx1,
        uint32_t LUTx2[256])
{
    for (size_t idx = 0; idx < 256; ++idx) {
        const size_t lo = idx & 0xF;
        const size_t hi = idx >> 4;
        LUTx2[idx]      = (uint32_t)LUTx1[lo] | ((uint32_t)LUTx1[hi] << 16);
    }
}

/// Build expanded LUT for nbBits=5: 2^5=32 raw entries -> 2^10=1024 expanded.
ZL_UNUSED_ATTR static void expandLUT5(
        const uint16_t* LUTx1,
        uint32_t LUTx2[1024])
{
    for (size_t idx = 0; idx < 1024; ++idx) {
        const size_t lo = idx & 31;
        const size_t hi = idx >> 5;
        LUTx2[idx]      = (uint32_t)LUTx1[lo] | ((uint32_t)LUTx1[hi] << 16);
    }
}

/// Pack two u32 LUT entries into one u64, indexed by a pair of 4-bit bucket
/// IDs. This lets the fast loop decode two u32 values with a single PDEP+add.
static void expandLUT4_u32(const uint32_t* LUTx1, uint64_t LUTx2[256])
{
    for (size_t idx = 0; idx < 256; ++idx) {
        const size_t lo = idx & 0xF;
        const size_t hi = idx >> 4;
        LUTx2[idx]      = (uint64_t)LUTx1[lo] | ((uint64_t)LUTx1[hi] << 32);
    }
}

static void expandLUT5_u32(const uint32_t* LUTx1, uint64_t LUTx2[1024])
{
    for (size_t idx = 0; idx < 1024; ++idx) {
        const size_t lo = idx & 31;
        const size_t hi = idx >> 5;
        LUTx2[idx]      = (uint64_t)LUTx1[lo] | ((uint64_t)LUTx1[hi] << 32);
    }
}

// ---------------------------------------------------------------------------
// computeLimit: safe iteration bound for fast-path loops
// ---------------------------------------------------------------------------
//
// The fast-path loop processes kEltsPerIter elements per iteration:
//
//     for (; i < limit; i += kEltsPerIter) { /* read kEltsPerIter elts */ }
//
// This function computes a safe `limit` such that every iteration reads
// within both the fixed and variable stream buffers.
//
// The variable stream bound is a worst-case estimate (assumes maxVarBits
// bits per element). Actual consumption may be lower, so callers recompute
// after the inner loop to squeeze out additional iterations:
//
//     for (;;) {
//         limit = computeLimit(...);
//         if (i >= limit) break;
//         for (; i < limit; i += kEltsPerIter) { ... }
//     }
//
static size_t computeLimit(
        size_t i,
        size_t numElts,
        size_t kEltsPerIter,
        size_t maxVarBits,
        const uint8_t* v,
        const uint8_t* vEnd,
        const uint8_t* f,
        const uint8_t* fEnd,
        size_t fixedBytesPerIter)
{
    ZL_ASSERT_LE(i, numElts);
    ZL_ASSERT_LE(v, vEnd);
    ZL_ASSERT_LE(f, fEnd);

    // Need at least one full iteration's worth of output elements.
    if (numElts < kEltsPerIter) {
        return i;
    }

    // We need to be able to load 8 bytes from the fixed and variable inputs.
    if (fEnd - f < 8 || vEnd - v < 8) {
        return i;
    }

    // Fixed stream: each iteration consumes exactly fixedBytesPerIter bytes,
    // but individual reads within the iteration are 8-byte LE loads that may
    // read ahead of what is consumed (e.g. decodePartitionBitpack5 reads 8
    // bytes, advances 5). Reserve 7 bytes of margin so every 8-byte load stays
    // in bounds.
    const size_t fSafeElts =
            (((size_t)(fEnd - f) - 7) / fixedBytesPerIter) * kEltsPerIter;

    size_t vSafeElts = numElts;
    if (maxVarBits != 0) {
        // Total bits in remaining bytes, minus 7 bytes so that every 8-byte
        // load stays in bounds, minus up to 7 residual bits from the sub-byte
        // bitsConsumed offset carried into this call.
        const size_t vSafeBits = (8 * ((size_t)(vEnd - v) - 7) - 7);
        vSafeElts              = vSafeBits / maxVarBits;
    }

    // Take the tighter of the two stream bounds.
    const size_t safeElts = ZL_MIN(fSafeElts, vSafeElts);
    if (safeElts < kEltsPerIter) {
        return i;
    }

    // An iteration at index j reads elements j..j+kEltsPerIter-1, so the
    // last valid start index is safeElts - kEltsPerIter + 1 from the current i.
    // Also cap at numElts - kEltsPerIter + 1 to stay within the output buffer.
    const size_t limit = ZL_MIN(numElts, i + safeElts) - kEltsPerIter + 1;
    return limit;
}

// ---------------------------------------------------------------------------
// DecodeTail: Decode the tail of the stream with a scalar safe path.
// ---------------------------------------------------------------------------
static ZL_Report decodeTail(
        void* out,
        size_t eltWidth,
        size_t i,
        size_t numElts,
        const uint8_t* fixed,
        const uint8_t* fixedEnd,
        size_t fixedBitsPerElt,
        const uint8_t* var,
        const uint8_t* varEnd,
        size_t varBitsConsumed,
        const uint64_t* bases,
        const uint8_t* bits,
        size_t numPartitions)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    ZS_BitDStreamFF fixedBits =
            ZS_BitDStreamFF_init(fixed, (size_t)(fixedEnd - fixed));
    ZS_BitDStreamFF varBits = ZS_BitDStreamFF_init(var, (size_t)(varEnd - var));
    ZS_BitDStreamFF_skip(&varBits, varBitsConsumed);
    for (; i < numElts; ++i) {
        size_t bucket = ZS_BitDStreamFF_read(&fixedBits, fixedBitsPerElt);
        ZS_BitDStreamFF_reload(&fixedBits);
        ZL_ERR_IF_GE(bucket, numPartitions, corruption, "Bucket ID OOB");
        uint64_t offset = ZS_BitDStreamFF_read(&varBits, bits[bucket]);
        ZS_BitDStreamFF_reload(&varBits);
        const uint64_t value = bases[bucket] + offset;
        if (eltWidth == 4) {
            ((uint32_t*)out)[i] = (uint32_t)value;
        } else {
            ((uint16_t*)out)[i] = (uint16_t)value;
        }
    }
    ZL_ERR_IF_ERR(ZS_BitDStreamFF_finish(&fixedBits));
    ZL_ERR_IF_ERR(ZS_BitDStreamFF_finish(&varBits));
    return ZL_returnSuccess();
}

// ---------------------------------------------------------------------------
// 2 bucket IDs packed per byte (4 bits each), 16 elts per iter
// ---------------------------------------------------------------------------
static ZL_Report decodePartitionBitpack4(
        uint16_t* out,
        size_t numElts,
        const uint8_t* fixed,
        size_t fixedSize,
        const uint8_t* var,
        size_t varSize,
        const uint64_t* bases,
        const uint8_t* bits,
        size_t numPartitions,
        size_t maxVarBits,
        uint32_t baseLUTx2[256],
        uint32_t maskLUTx2[256])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_LE(numPartitions, 16);

#if ZL_HAS_SSSE3
    (void)baseLUTx2;
    (void)maskLUTx2;

    // Build byte-split LUTs for vpshufb: split each uint16_t entry into
    // lo/hi byte tables. One vpshufb per {base,mask} byte component;
    // unpacklo/hi interleave the results back to uint16_t.
    //   [0] = low byte,  [1] = high byte
    __m128i baseLUT[2];
    __m128i maskLUT[2];
    {
        uint8_t baseLo[16] = { 0 }, baseHi[16] = { 0 };
        uint8_t maskLo[16] = { 0 }, maskHi[16] = { 0 };
        for (size_t p = 0; p < numPartitions; ++p) {
            const uint16_t b = (uint16_t)bases[p];
            const uint16_t m = (uint16_t)((1u << bits[p]) - 1);
            baseLo[p]        = (uint8_t)b;
            baseHi[p]        = (uint8_t)(b >> 8);
            maskLo[p]        = (uint8_t)m;
            maskHi[p]        = (uint8_t)(m >> 8);
        }
        baseLUT[0] = _mm_loadu_si128((__m128i const*)baseLo);
        baseLUT[1] = _mm_loadu_si128((__m128i const*)baseHi);
        maskLUT[0] = _mm_loadu_si128((__m128i const*)maskLo);
        maskLUT[1] = _mm_loadu_si128((__m128i const*)maskHi);
    }
    const __m128i nibbleMask = _mm_set1_epi8(0x0F);
#elif ZL_HAS_SVE2_BITPERM
    // Build uint16_t LUTs for svtbl2: each 16-entry table spans two SVE
    // registers (8 entries each at VL=128). svtbl2_u16 looks up directly at
    // uint16_t granularity, avoiding the byte-split + zip pattern.
    const svbool_t pg8u16 = svptrue_pat_b16(SV_VL8);
    const svbool_t pg16   = svptrue_pat_b8(SV_VL16);
    svuint16x2_t baseLUT16, maskLUT16;
    svuint8_t bitsLUT;
    {
        uint16_t baseArr[16]  = { 0 };
        uint16_t maskArr[16]  = { 0 };
        uint8_t bitsArr[16]   = { 0 };
        const svbool_t pg_u16 = svptrue_pat_b16(SV_VL8);
        for (size_t p = 0; p < numPartitions; ++p) {
            baseArr[p] = (uint16_t)bases[p];
            maskArr[p] = (uint16_t)((1u << bits[p]) - 1);
            bitsArr[p] = bits[p];
        }
        baseLUT16 = svcreate2_u16(
                svld1_u16(pg_u16, baseArr), svld1_u16(pg_u16, baseArr + 8));
        maskLUT16 = svcreate2_u16(
                svld1_u16(pg_u16, maskArr), svld1_u16(pg_u16, maskArr + 8));
        bitsLUT = svld1_u8(pg16, bitsArr);
    }

    // Also need to build LUTs for VL=256+ fallback paths
    {
        // Need to copy bases to local storage for when numPartitions < 16.
        uint16_t baseLUTx1[16] = { 0 };
        uint16_t maskLUTx1[16] = { 0 };
        for (size_t p = 0; p < numPartitions; ++p) {
            baseLUTx1[p] = (uint16_t)bases[p];
            maskLUTx1[p] = (uint16_t)((1u << bits[p]) - 1);
        }
        expandLUT4(baseLUTx1, baseLUTx2);
        expandLUT4(maskLUTx1, maskLUTx2);
    }
#else
    {
        // Need to copy bases to local storage for when numPartitions < 16.
        uint16_t baseLUTx1[16] = { 0 };
        uint16_t maskLUTx1[16] = { 0 };
        for (size_t p = 0; p < numPartitions; ++p) {
            baseLUTx1[p] = (uint16_t)bases[p];
            maskLUTx1[p] = (uint16_t)((1u << bits[p]) - 1);
        }
        expandLUT4(baseLUTx1, baseLUTx2);
        expandLUT4(maskLUTx1, maskLUTx2);
    }
#endif

    uint16_t* o                  = out;
    const uint8_t* f             = fixed;
    const uint8_t* const fEnd    = fixed + fixedSize;
    const uint8_t* v             = var;
    const uint8_t* const vEnd    = var + varSize;
    const size_t kEltsPerIter    = 16;
    const size_t kFixedBytesIter = 8; // 16 elts * 4 bits / 8 = 8 bytes
    size_t i                     = 0;
    size_t bitsConsumed          = 0;

    for (;;) {
        const size_t limit = computeLimit(
                i,
                numElts,
                kEltsPerIter,
                maxVarBits,
                v,
                vEnd,
                f,
                fEnd,
                kFixedBytesIter);
        if (i >= limit) {
            break;
        }
        for (; i < limit; i += kEltsPerIter) {
#if ZL_HAS_SSSE3
            // Load 8 bytes containing 16 x 4-bit bucket indices
            const __m128i packed = _mm_loadl_epi64((__m128i_u const*)f);
            f += 8;

            // Unpack nibbles: split each byte into low and high nibble
            const __m128i lo = _mm_and_si128(packed, nibbleMask);
            const __m128i hi =
                    _mm_and_si128(_mm_srli_epi16(packed, 4), nibbleMask);

            // Interleave to element order: {lo[0], hi[0], lo[1], hi[1], ...}
            const __m128i nibbles = _mm_unpacklo_epi8(lo, hi);

            // vpshufb looks up lo/hi byte halves of base and mask.
            // b[0]/m[0] = lo bytes, b[1]/m[1] = hi bytes.
            __m128i b[2], m[2];
            for (size_t h = 0; h < 2; ++h) {
                b[h] = _mm_shuffle_epi8(baseLUT[h], nibbles);
                m[h] = _mm_shuffle_epi8(maskLUT[h], nibbles);
            }

            // Interleave lo/hi bytes to uint16_t.
            // baseV[0]/maskV[0] → elements 0..7
            // baseV[1]/maskV[1] → elements 8..15
            __m128i baseV[2], maskV[2];
            baseV[0] = _mm_unpacklo_epi8(b[0], b[1]);
            baseV[1] = _mm_unpackhi_epi8(b[0], b[1]);
            maskV[0] = _mm_unpacklo_epi8(m[0], m[1]);
            maskV[1] = _mm_unpackhi_epi8(m[0], m[1]);

            // Decode 4 groups of 4 elements.
            // u/2 selects the __m128i (elements 0..7 or 8..15),
            // u%2 selects the 64-bit half within it.
            for (size_t u = 0; u < 4; ++u) {
                const __m128i b128  = baseV[u / 2];
                const __m128i m128  = maskV[u / 2];
                const uint64_t base = (uint64_t)_mm_cvtsi128_si64(
                        u % 2 == 0 ? b128 : _mm_srli_si128(b128, 8));
                const uint64_t mask = (uint64_t)_mm_cvtsi128_si64(
                        u % 2 == 0 ? m128 : _mm_srli_si128(m128, 8));

                const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                const uint64_t offset = ZL_bitDeposit64(vData, mask);
                ZS_write64(o, base + offset);
                o += 4;
                bitsConsumed += (size_t)ZL_popcount64(mask);
                v += bitsConsumed >> 3;
                bitsConsumed &= 7;
            }
#else
#    if ZL_HAS_SVE2_BITPERM
            const bool useSVE2 = svcntb() == 16;
            if (useSVE2) { // special case for 128-bit SVE vectors
                // Load 8 bytes containing 16 x 4-bit bucket indices
                const svuint16_t packed = svld1ub_u16(pg8u16, f);
                f += 8;

                // Unpack nibbles: lane-wise SLI -> mask directly isolates each
                // nibble. Only the first 16 bytes are used downstream.
                const svuint16_t sliResult = svsli_n_u16(packed, packed, 4);
                const svuint8_t nibbles    = svreinterpret_u8_u16(
                        svand_n_u16_z(pg8u16, sliResult, 0x0F0F));

                // Widen indices from u8 to u16 for direct uint16_t table
                // lookup.
                const svuint16_t idx_lo = svunpklo_u16(nibbles);
                const svuint16_t idx_hi = svunpkhi_u16(nibbles);

                // svtbl2 looks up base and mask as uint16_t directly.
                // Each call processes 8 elements from a 16-entry table.
                const svuint16_t baseVlo = svtbl2_u16(baseLUT16, idx_lo);
                const svuint16_t baseVhi = svtbl2_u16(baseLUT16, idx_hi);
                const svuint16_t maskVlo = svtbl2_u16(maskLUT16, idx_lo);
                const svuint16_t maskVhi = svtbl2_u16(maskLUT16, idx_hi);

                // Each register holds two 64-bit lanes:
                //   *SVE01 lane[0] = group 0, lane[1] = group 1
                //   *SVE23 lane[0] = group 2, lane[1] = group 3
                const svuint64_t baseSVE01 = svreinterpret_u64_u16(baseVlo);
                const svuint64_t baseSVE23 = svreinterpret_u64_u16(baseVhi);
                const svuint64_t maskSVE01 = svreinterpret_u64_u16(maskVlo);
                const svuint64_t maskSVE23 = svreinterpret_u64_u16(maskVhi);

                // Precompute per-group bit count sums via TBL + pairwise adds.
                // This breaks the serial bitsConsumed dependency: all 4
                // variable-stream positions are known up front, so groups
                // can issue loads and bdep independently.
                const svuint8_t bitCounts = svtbl_u8(bitsLUT, nibbles);
                const uint8x16_t bcNeon   = svget_neonq_u8(bitCounts);
                const uint16x8_t bc16     = vpaddlq_u8(bcNeon);
                const uint32x4_t bc32     = vpaddlq_u16(bc16);

                const size_t gs0  = vgetq_lane_u32(bc32, 0);
                const size_t gs1  = vgetq_lane_u32(bc32, 1);
                const size_t gs2  = vgetq_lane_u32(bc32, 2);
                const size_t pos0 = bitsConsumed;
                const size_t pos1 = pos0 + gs0;
                const size_t pos2 = pos1 + gs1;
                const size_t pos3 = pos2 + gs2;

                // Groups 0+1: paired bdep processes both lanes in parallel.
                // Two vData values are packed into one SVE register; the
                // mask vector already has the right masks per lane from zip.
                {
                    const uint64_t vData0 =
                            ZL_readLE64(v + (pos0 >> 3)) >> (pos0 & 7);
                    const uint64_t vData1 =
                            ZL_readLE64(v + (pos1 >> 3)) >> (pos1 & 7);
                    const uint64x2_t src01Neon = { vData0, vData1 };
                    const svuint64_t src01 =
                            svset_neonq_u64(svundef_u64(), src01Neon);
                    const svuint64_t res01 = svbdep_u64(src01, maskSVE01);
                    const uint64x2_t out01 = vaddq_u64(
                            svget_neonq_u64(res01), svget_neonq_u64(baseSVE01));
                    vst1q_u64((uint64_t*)(void*)o, out01);
                }

                // Groups 2+3: same paired bdep pattern.
                {
                    const uint64_t vData2 =
                            ZL_readLE64(v + (pos2 >> 3)) >> (pos2 & 7);
                    const uint64_t vData3 =
                            ZL_readLE64(v + (pos3 >> 3)) >> (pos3 & 7);
                    const uint64x2_t src23Neon = { vData2, vData3 };
                    const svuint64_t src23 =
                            svset_neonq_u64(svundef_u64(), src23Neon);
                    const svuint64_t res23 = svbdep_u64(src23, maskSVE23);
                    const uint64x2_t out23 = vaddq_u64(
                            svget_neonq_u64(res23), svget_neonq_u64(baseSVE23));
                    vst1q_u64((uint64_t*)(void*)(o + 8), out23);
                }

                o += 16;
                {
                    const size_t gs3v         = vgetq_lane_u32(bc32, 3);
                    const size_t newBitsTotal = pos3 + gs3v;
                    v += newBitsTotal >> 3;
                    bitsConsumed = newBitsTotal & 7;
                }
            }
#    else
            const bool useSVE2 = false;
#    endif
            if (!useSVE2) {
                for (size_t u = 0; u < kEltsPerIter; u += 4) {
                    const uint64_t base = (uint64_t)baseLUTx2[f[0]]
                            | ((uint64_t)baseLUTx2[f[1]] << 32);
                    const uint64_t mask = (uint64_t)maskLUTx2[f[0]]
                            | ((uint64_t)maskLUTx2[f[1]] << 32);
                    f += 2;
                    const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                    const uint64_t offset = ZL_bitDeposit64(vData, mask);
                    ZS_write64(o, base + offset);
                    o += 4;
                    bitsConsumed += (size_t)ZL_popcount64(mask);
                    v += bitsConsumed >> 3;
                    bitsConsumed &= 7;
                }
            }
#endif
        }
    }

    return decodeTail(
            out,
            sizeof(uint16_t),
            i,
            numElts,
            f,
            fEnd,
            4,
            v,
            vEnd,
            bitsConsumed,
            bases,
            bits,
            numPartitions);
}

// ---------------------------------------------------------------------------
// 8 bucket IDs packed into 5 bytes (5 bits each), 16 elts per iter
// ---------------------------------------------------------------------------
static ZL_Report decodePartitionBitpack5(
        uint16_t* out,
        size_t numElts,
        const uint8_t* fixed,
        size_t fixedSize,
        const uint8_t* var,
        size_t varSize,
        const uint64_t* bases,
        const uint8_t* bits,
        size_t numPartitions,
        size_t maxVarBits,
        uint32_t baseLUTx2[1024],
        uint32_t maskLUTx2[1024])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_LE(numPartitions, 32);

#if ZL_HAS_SSSE3
    (void)baseLUTx2;
    (void)maskLUTx2;

    // Build byte-split LUTs for vpshufb: 32 entries split into two 16-entry
    // halves (partitions 0..15 vs 16..31), each uint16_t split into lo/hi
    // bytes. pshufb's zero-on-high-bit behaviour lets us blend the two
    // partition halves with OR instead of blendv.
    //   [0][h] = partitions 0..15,  [1][h] = partitions 16..31
    //   [p][0] = low byte,          [p][1] = high byte
    __m128i baseLUT[2][2];
    __m128i maskLUT[2][2];
    {
        uint8_t baseLo0[16] = { 0 }, baseHi0[16] = { 0 };
        uint8_t maskLo0[16] = { 0 }, maskHi0[16] = { 0 };
        uint8_t baseLo1[16] = { 0 }, baseHi1[16] = { 0 };
        uint8_t maskLo1[16] = { 0 }, maskHi1[16] = { 0 };
        for (size_t p = 0; p < numPartitions; ++p) {
            const uint16_t b = (uint16_t)bases[p];
            const uint16_t m = (uint16_t)((1u << bits[p]) - 1);
            uint8_t* bLo     = (p < 16) ? baseLo0 : baseLo1;
            uint8_t* bHi     = (p < 16) ? baseHi0 : baseHi1;
            uint8_t* mLo     = (p < 16) ? maskLo0 : maskLo1;
            uint8_t* mHi     = (p < 16) ? maskHi0 : maskHi1;
            const size_t idx = p & 15;
            bLo[idx]         = (uint8_t)b;
            bHi[idx]         = (uint8_t)(b >> 8);
            mLo[idx]         = (uint8_t)m;
            mHi[idx]         = (uint8_t)(m >> 8);
        }
        baseLUT[0][0] = _mm_loadu_si128((__m128i const*)baseLo0);
        baseLUT[0][1] = _mm_loadu_si128((__m128i const*)baseHi0);
        baseLUT[1][0] = _mm_loadu_si128((__m128i const*)baseLo1);
        baseLUT[1][1] = _mm_loadu_si128((__m128i const*)baseHi1);
        maskLUT[0][0] = _mm_loadu_si128((__m128i const*)maskLo0);
        maskLUT[0][1] = _mm_loadu_si128((__m128i const*)maskHi0);
        maskLUT[1][0] = _mm_loadu_si128((__m128i const*)maskLo1);
        maskLUT[1][1] = _mm_loadu_si128((__m128i const*)maskHi1);
    }
    const __m128i nibbleMask = _mm_set1_epi8(0x0F);
    // For unpacking 8 x 5-bit fields from 5 bytes: arrange byte pairs into
    // 16-bit lanes, then variable-shift via multiply to align each 5-bit
    // field to bits [15:11], then uniform right-shift by 11.
    const __m128i shufIdx5 =
            _mm_setr_epi8(0, 1, 0, 1, 1, 2, 1, 2, 2, 3, 3, 4, 3, 4, 4, 5);
    const __m128i multipliers5 =
            _mm_setr_epi16(2048, 64, 512, 16, 128, 1024, 32, 256);
#else
    {
        // Need to copy bases to local storage for when numPartitions < 32.
        uint16_t baseLUTx1[32] = { 0 };
        uint16_t maskLUTx1[32] = { 0 };
        for (size_t p = 0; p < numPartitions; ++p) {
            baseLUTx1[p] = (uint16_t)bases[p];
            maskLUTx1[p] = (uint16_t)((1u << bits[p]) - 1);
        }
        expandLUT5(baseLUTx1, baseLUTx2);
        expandLUT5(maskLUTx1, maskLUTx2);
    }
#endif

    uint16_t* o                  = out;
    const uint8_t* f             = fixed;
    const uint8_t* const fEnd    = fixed + fixedSize;
    const uint8_t* v             = var;
    const uint8_t* const vEnd    = var + varSize;
    const size_t kEltsPerIter    = 16;
    const size_t kFixedBytesIter = 10; // 16 elts * 5 bits / 8 = 10 bytes
    size_t i                     = 0;
    size_t bitsConsumed          = 0;

    for (;;) {
        const size_t limit = computeLimit(
                i,
                numElts,
                kEltsPerIter,
                maxVarBits,
                v,
                vEnd,
                f,
                fEnd,
                kFixedBytesIter);
        if (i >= limit) {
            break;
        }
        for (; i < limit; i += kEltsPerIter) {
#if ZL_HAS_SSSE3
            // Extract 16 x 5-bit bucket indices from 10 bytes.
            // Two groups of 8 (bytes 0-4 and 5-9) each processed with
            // one vpshufb + one vpmullw + one vpsrlw.
            __m128i idxVec;
            {
                const __m128i raw = _mm_loadu_si128((__m128i_u const*)f);
                f += 10;
                const __m128i pairs0 = _mm_shuffle_epi8(raw, shufIdx5);
                const __m128i elts0  = _mm_srli_epi16(
                        _mm_mullo_epi16(pairs0, multipliers5), 11);
                const __m128i pairs1 =
                        _mm_shuffle_epi8(_mm_srli_si128(raw, 5), shufIdx5);
                const __m128i elts1 = _mm_srli_epi16(
                        _mm_mullo_epi16(pairs1, multipliers5), 11);
                idxVec = _mm_packus_epi16(elts0, elts1);
            }

            // Lower 4 bits for vpshufb index (selects within a 16-entry
            // half).
            const __m128i idx_low4 = _mm_and_si128(idxVec, nibbleMask);
            // pshufb zeros bytes where the index has bit 7 set. OR the
            // ge16/lt16 mask into the index so the wrong partition half
            // zeros out, then OR the two halves together.
            const __m128i ge16   = _mm_cmpgt_epi8(idxVec, _mm_set1_epi8(15));
            const __m128i lt16   = _mm_cmpgt_epi8(_mm_set1_epi8(16), idxVec);
            const __m128i idx_v0 = _mm_or_si128(idx_low4, ge16);
            const __m128i idx_v1 = _mm_or_si128(idx_low4, lt16);

            // vpshufb looks up lo/hi byte halves of base and mask.
            // pshufb zeros the wrong partition half; OR merges them.
            // b[0]/m[0] = lo bytes, b[1]/m[1] = hi bytes.
            __m128i b[2], m[2];
            for (size_t h = 0; h < 2; ++h) {
                b[h] = _mm_or_si128(
                        _mm_shuffle_epi8(baseLUT[0][h], idx_v0),
                        _mm_shuffle_epi8(baseLUT[1][h], idx_v1));
                m[h] = _mm_or_si128(
                        _mm_shuffle_epi8(maskLUT[0][h], idx_v0),
                        _mm_shuffle_epi8(maskLUT[1][h], idx_v1));
            }

            // Interleave lo/hi bytes to uint16_t.
            // baseV[0]/maskV[0] → elements 0..7
            // baseV[1]/maskV[1] → elements 8..15
            __m128i baseV[2], maskV[2];
            baseV[0] = _mm_unpacklo_epi8(b[0], b[1]);
            baseV[1] = _mm_unpackhi_epi8(b[0], b[1]);
            maskV[0] = _mm_unpacklo_epi8(m[0], m[1]);
            maskV[1] = _mm_unpackhi_epi8(m[0], m[1]);

            // Decode 4 groups of 4 elements.
            // u/2 selects the __m128i (elements 0..7 or 8..15),
            // u%2 selects the 64-bit half within it.
            for (size_t u = 0; u < 4; ++u) {
                const __m128i b128  = baseV[u / 2];
                const __m128i m128  = maskV[u / 2];
                const uint64_t base = (uint64_t)_mm_cvtsi128_si64(
                        u % 2 == 0 ? b128 : _mm_srli_si128(b128, 8));
                const uint64_t mask = (uint64_t)_mm_cvtsi128_si64(
                        u % 2 == 0 ? m128 : _mm_srli_si128(m128, 8));

                const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                const uint64_t offset = ZL_bitDeposit64(vData, mask);
                ZS_write64(o, base + offset);
                o += 4;
                bitsConsumed += (size_t)ZL_popcount64(mask);
                v += bitsConsumed >> 3;
                bitsConsumed &= 7;
            }
#else
            for (size_t u = 0; u < kEltsPerIter; u += 8) {
                const uint64_t F     = ZL_readLE64(f);
                const uint64_t fs[4] = {
                    (F >> 0) & 1023,
                    (F >> 10) & 1023,
                    (F >> 20) & 1023,
                    (F >> 30) & 1023,
                };
                f += 5;
                for (size_t k = 0; k < 4; k += 2) {
                    const uint64_t base = (uint64_t)baseLUTx2[fs[k]]
                            | ((uint64_t)baseLUTx2[fs[k + 1]] << 32);
                    const uint64_t mask = (uint64_t)maskLUTx2[fs[k]]
                            | ((uint64_t)maskLUTx2[fs[k + 1]] << 32);
                    const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                    const uint64_t offset = ZL_bitDeposit64(vData, mask);
                    ZS_write64(o, base + offset);
                    o += 4;
                    bitsConsumed += (size_t)ZL_popcount64(mask);
                    v += bitsConsumed >> 3;
                    bitsConsumed &= 7;
                }
            }
#endif
        }
    }

    return decodeTail(
            out,
            sizeof(uint16_t),
            i,
            numElts,
            f,
            fEnd,
            5,
            v,
            vEnd,
            bitsConsumed,
            bases,
            bits,
            numPartitions);
}

static ZL_Report decodePartitionBitpack4_u32(
        uint32_t* out,
        size_t numElts,
        const uint8_t* fixed,
        size_t fixedSize,
        const uint8_t* var,
        size_t varSize,
        const uint64_t* bases,
        const uint8_t* bits,
        size_t numPartitions,
        size_t maxVarBits,
        uint64_t baseLUTx2[256],
        uint64_t maskLUTx2[256])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_LE(numPartitions, 16);

    uint32_t baseLUTx1[16] = { 0 };
    uint32_t maskLUTx1[16] = { 0 };
    for (size_t p = 0; p < numPartitions; ++p) {
        baseLUTx1[p] = (uint32_t)bases[p];
        maskLUTx1[p] = (uint32_t)((1ULL << bits[p]) - 1);
    }
    expandLUT4_u32(baseLUTx1, baseLUTx2);
    expandLUT4_u32(maskLUTx1, maskLUTx2);

    uint32_t* o                  = out;
    const uint8_t* f             = fixed;
    const uint8_t* const fEnd    = fixed + fixedSize;
    const uint8_t* v             = var;
    const uint8_t* const vEnd    = var + varSize;
    const size_t kEltsPerIter    = 8;
    const size_t kFixedBytesIter = 4; // 8 elts * 4 bits / 8 = 4 bytes
    size_t i                     = 0;
    size_t bitsConsumed          = 0;

    for (;;) {
        const size_t limit = computeLimit(
                i,
                numElts,
                kEltsPerIter,
                maxVarBits,
                v,
                vEnd,
                f,
                fEnd,
                kFixedBytesIter);
        if (i >= limit) {
            break;
        }
        for (; i < limit; i += kEltsPerIter) {
            for (size_t u = 0; u < kEltsPerIter; u += 2) {
                const uint64_t base = baseLUTx2[*f];
                const uint64_t mask = maskLUTx2[*f];
                ++f;
                const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                const uint64_t offset = ZL_bitDeposit64(vData, mask);
                ZS_write64(o, base + offset);
                o += 2;
                bitsConsumed += (size_t)ZL_popcount64(mask);
                v += bitsConsumed >> 3;
                bitsConsumed &= 7;
            }
        }
    }

    return decodeTail(
            out,
            sizeof(uint32_t),
            i,
            numElts,
            f,
            fEnd,
            4,
            v,
            vEnd,
            bitsConsumed,
            bases,
            bits,
            numPartitions);
}

static ZL_Report decodePartitionBitpack5_u32(
        uint32_t* out,
        size_t numElts,
        const uint8_t* fixed,
        size_t fixedSize,
        const uint8_t* var,
        size_t varSize,
        const uint64_t* bases,
        const uint8_t* bits,
        size_t numPartitions,
        size_t maxVarBits,
        uint64_t baseLUTx2[1024],
        uint64_t maskLUTx2[1024])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_LE(numPartitions, 32);

    uint32_t baseLUTx1[32] = { 0 };
    uint32_t maskLUTx1[32] = { 0 };
    for (size_t p = 0; p < numPartitions; ++p) {
        baseLUTx1[p] = (uint32_t)bases[p];
        maskLUTx1[p] = (uint32_t)((1ULL << bits[p]) - 1);
    }
    expandLUT5_u32(baseLUTx1, baseLUTx2);
    expandLUT5_u32(maskLUTx1, maskLUTx2);

    uint32_t* o                  = out;
    const uint8_t* f             = fixed;
    const uint8_t* const fEnd    = fixed + fixedSize;
    const uint8_t* v             = var;
    const uint8_t* const vEnd    = var + varSize;
    const size_t kEltsPerIter    = 8;
    const size_t kFixedBytesIter = 5; // 8 elts * 5 bits / 8 = 5 bytes
    size_t i                     = 0;
    size_t bitsConsumed          = 0;

    for (;;) {
        const size_t limit = computeLimit(
                i,
                numElts,
                kEltsPerIter,
                maxVarBits,
                v,
                vEnd,
                f,
                fEnd,
                kFixedBytesIter);
        if (i >= limit) {
            break;
        }
        for (; i < limit; i += kEltsPerIter) {
            const uint64_t F     = ZL_readLE64(f);
            const uint64_t fs[4] = {
                (F >> 0) & 1023,
                (F >> 10) & 1023,
                (F >> 20) & 1023,
                (F >> 30) & 1023,
            };
            f += 5;
            for (size_t k = 0; k < 4; ++k) {
                const uint64_t base   = baseLUTx2[fs[k]];
                const uint64_t mask   = maskLUTx2[fs[k]];
                const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                const uint64_t offset = ZL_bitDeposit64(vData, mask);
                ZS_write64(o, base + offset);
                o += 2;
                bitsConsumed += (size_t)ZL_popcount64(mask);
                v += bitsConsumed >> 3;
                bitsConsumed &= 7;
            }
        }
    }

    return decodeTail(
            out,
            sizeof(uint32_t),
            i,
            numElts,
            f,
            fEnd,
            5,
            v,
            vEnd,
            bitsConsumed,
            bases,
            bits,
            numPartitions);
}

static ZL_Report ZL_partitionBitpackFusedDecode_fallback(
        ZL_DecoderFusion* state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);
    ZL_ERR_IF_ERR(ZL_DecoderFusion_runCodec(state, 0));
    ZL_ERR_IF_ERR(ZL_DecoderFusion_runCodec(state, 1));
    return ZL_returnSuccess();
}

// ---------------------------------------------------------------------------
// Main fusion entry point
// ---------------------------------------------------------------------------
ZL_Report ZL_partitionBitpackFusedDecode(ZL_DecoderFusion* state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);

    // Codec index 0 = child (bitpack_int), codec index 1 = parent
    // (partition)
    const size_t bitpackIdx   = 0;
    const size_t partitionIdx = 1;

    // --- Get bitpack input (packed serial stream) ---
    ZL_TRY_LET(
            ZL_CodecInputs,
            bpInputs,
            ZL_DecoderFusion_getCodecInputs(state, bitpackIdx));
    ZL_ERR_IF_NE(bpInputs.singleton.numInputs, 1, corruption);
    const ZL_Input* packedIn = bpInputs.singleton.inputs[0];
    ZL_ERR_IF_NULL(packedIn, corruption);
    const uint8_t* packedData = (const uint8_t*)ZL_Input_ptr(packedIn);
    const size_t packedSize   = ZL_Input_numElts(packedIn);

    // --- Parse bitpack header ---
    ZL_TRY_LET(
            ZL_RBuffer,
            bpHeaderBuf,
            ZL_DecoderFusion_getCodecHeader(state, bitpackIdx));
    ZL_BitpackHeader bpHeader;
    ZL_ERR_IF_ERR(ZL_BitpackHeader_parse(
            &bpHeader, bpHeaderBuf.start, bpHeaderBuf.size, packedSize));
    ZL_ERR_IF_NE(
            bpHeader.eltWidth,
            1,
            corruption,
            "Fused bitpack must produce bytes");
    const size_t nbBits  = bpHeader.nbBits;
    const size_t numElts = bpHeader.numElts;

    // --- Parse partition header ---
    ZL_TRY_LET(
            ZL_RBuffer,
            partHeader,
            ZL_DecoderFusion_getCodecHeader(state, partitionIdx));

    uint64_t* partitionSizesBuffer =
            (uint64_t*)ZL_DecoderFusion_getScratchSpace(
                    state, sizeof(uint64_t) * ZL_PARTITION_MAX_PARTITIONS);
    ZL_ERR_IF_NULL(partitionSizesBuffer, allocation);
    ZL_PartitionParams params = { 0 };
    size_t outEltWidth        = 0;
    ZL_ERR_IF_ERR(ZL_PartitionParams_parseHeader(
            &params,
            &outEltWidth,
            (const uint8_t*)partHeader.start,
            partHeader.size,
            partitionSizesBuffer));

    const uint64_t largestPartitionSize =
            ZL_PartitionParams_getLargestPartitionSize(&params);
    const bool useU16FastPath = outEltWidth == 2 && nbBits >= 4 && nbBits <= 5
            && params.numPartitions <= (1u << nbBits)
            && largestPartitionSize
                    <= ZL_PARTITION_MAX_PARTITION_SIZE_FOR_UNROLL4;
    const bool useU32FastPath = outEltWidth == 4 && nbBits >= 4 && nbBits <= 5
            && params.numPartitions <= (1u << nbBits)
            && largestPartitionSize
                    <= ZL_PARTITION_MAX_PARTITION_SIZE_FOR_UNROLL2;
    if (!useU16FastPath && !useU32FastPath) {
        return ZL_partitionBitpackFusedDecode_fallback(state);
    }

    // --- Get partition offsets input (input 1, not fused) ---
    ZL_TRY_LET(
            ZL_CodecInputs,
            partInputs,
            ZL_DecoderFusion_getCodecInputs(state, partitionIdx));
    // Input 0 is fused (NULL), input 1 is the offsets serial stream
    ZL_ERR_IF_NE(partInputs.singleton.numInputs, 2, corruption);
    ZL_ASSERT_NULL(partInputs.singleton.inputs[0]); // fused
    const ZL_Input* offsetsIn = partInputs.singleton.inputs[1];
    ZL_ERR_IF_NULL(offsetsIn, corruption);
    const uint8_t* offsetsData = (const uint8_t*)ZL_Input_ptr(offsetsIn);
    const size_t offsetsSize   = ZL_Input_numElts(offsetsIn);

    // --- Fast path: uint16_t or uint32_t output ---
    const size_t numPartitions = params.numPartitions;

    // Compute uint16_t bases and uint8_t bits
    uint64_t* basesU64 = (uint64_t*)ZL_DecoderFusion_getScratchSpace(
            state, sizeof(uint64_t) * ZL_PARTITION_MAX_PARTITIONS);
    ZL_ERR_IF_NULL(basesU64, allocation);
    uint8_t* varBits = (uint8_t*)ZL_DecoderFusion_getScratchSpace(
            state, sizeof(uint8_t) * ZL_PARTITION_MAX_PARTITIONS);
    ZL_ERR_IF_NULL(varBits, allocation);
    ZL_PartitionParams_computeBasesU64(&params, basesU64);
    ZL_PartitionParams_computeBits(&params, varBits);

    const size_t maxVarBits = NUMOP_findMaxU8(varBits, numPartitions);

    // Create output stream
    ZL_Output* const out =
            ZL_DecoderFusion_createTypedStream(state, 0, numElts, outEltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    // Dispatch to specialized decoder
    ZL_Report ret;
    if (useU16FastPath) {
        ZL_ASSERT_LE(maxVarBits, 14, "Already validated");

#if ZL_HAS_SSSE3
        // SSSE3 variants don't use these LUTs
        uint32_t* baseLUTx2 = NULL;
        uint32_t* maskLUTx2 = NULL;
#else
        // Allocate LUT scratch space (decodePartitionBitpack5 needs 1024
        // entries, decodePartitionBitpack4 needs 256).
        const size_t lutSize = (nbBits == 5) ? 1024 : 256;
        uint32_t* baseLUTx2  = (uint32_t*)ZL_DecoderFusion_getScratchSpace(
                state, sizeof(uint32_t) * lutSize);
        ZL_ERR_IF_NULL(baseLUTx2, allocation);
        uint32_t* maskLUTx2 = (uint32_t*)ZL_DecoderFusion_getScratchSpace(
                state, sizeof(uint32_t) * lutSize);
        ZL_ERR_IF_NULL(maskLUTx2, allocation);
#endif

        uint16_t* outPtr = (uint16_t*)ZL_Output_ptr(out);
        switch (nbBits) {
            case 4:
                ret = decodePartitionBitpack4(
                        outPtr,
                        numElts,
                        packedData,
                        packedSize,
                        offsetsData,
                        offsetsSize,
                        basesU64,
                        varBits,
                        numPartitions,
                        maxVarBits,
                        baseLUTx2,
                        maskLUTx2);
                break;
            case 5:
                ret = decodePartitionBitpack5(
                        outPtr,
                        numElts,
                        packedData,
                        packedSize,
                        offsetsData,
                        offsetsSize,
                        basesU64,
                        varBits,
                        numPartitions,
                        maxVarBits,
                        baseLUTx2,
                        maskLUTx2);
                break;
            default:
                ZL_ASSERT_FAIL("Unreachable");
                break;
        }
    } else {
        ZL_ASSERT(useU32FastPath);
        ZL_ASSERT_LE(maxVarBits, 28, "Already validated");

        const size_t lutSize   = (nbBits == 5) ? 1024 : 256;
        uint64_t* baseLUTx2U32 = (uint64_t*)ZL_DecoderFusion_getScratchSpace(
                state, sizeof(uint64_t) * lutSize);
        ZL_ERR_IF_NULL(baseLUTx2U32, allocation);
        uint64_t* maskLUTx2U32 = (uint64_t*)ZL_DecoderFusion_getScratchSpace(
                state, sizeof(uint64_t) * lutSize);
        ZL_ERR_IF_NULL(maskLUTx2U32, allocation);

        uint32_t* outPtr = (uint32_t*)ZL_Output_ptr(out);
        switch (nbBits) {
            case 4:
                ret = decodePartitionBitpack4_u32(
                        outPtr,
                        numElts,
                        packedData,
                        packedSize,
                        offsetsData,
                        offsetsSize,
                        basesU64,
                        varBits,
                        numPartitions,
                        maxVarBits,
                        baseLUTx2U32,
                        maskLUTx2U32);
                break;
            case 5:
                ret = decodePartitionBitpack5_u32(
                        outPtr,
                        numElts,
                        packedData,
                        packedSize,
                        offsetsData,
                        offsetsSize,
                        basesU64,
                        varBits,
                        numPartitions,
                        maxVarBits,
                        baseLUTx2U32,
                        maskLUTx2U32);
                break;
            default:
                ZL_ASSERT_FAIL("Unreachable");
                break;
        }
    }
    ZL_ERR_IF_ERR(ret);

    ZL_ERR_IF_ERR(ZL_Output_commit(out, numElts));

    return ZL_returnSuccess();
}
