// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/arch/decode_pivco_arch.h"

#if ZL_ARCH_X86_64

#    include <immintrin.h>

#    include "common_pivco_avx2_tables.h"
#    include "openzl/shared/bits.h"
#    include "openzl/shared/mem.h"

#    define AVX2_ATTR ZL_TARGET_ATTRIBUTE("avx2,bmi2,ssse3,sse4.1")
#    define AVX2_INLINE ZL_FORCE_INLINE AVX2_ATTR

// AVX2 implementations behind the PivCo Huffman decoder's
// `ZL_PivCoHuffmanDecode` vtable:
//   - mergeVectorVector:   interleave two decoded symbol streams (`lhs`, `rhs`)
//                          into `out`, one bitmap bit per output choosing a
//                          side.
//   - mergeConstantVector: same, but `lhs` is a single repeated symbol.
//   - mergeFlatDepth:      expand packed `depth`-bit indices (depth 1..8) into
//                          symbols via a 2^depth-entry table; one kernel per
//                          depth, dispatched by `mergeFlatDepth`.
// Every kernel runs a wide SIMD fast loop and hands the short remainder to a
// temp-buffer tail, so the caller's slop-free buffers are never read or written
// past their bounds.

static bool supported(const ZL_cpuid_t* cpuid)
{
#    if ZL_HAS_AVX2 && ZL_HAS_BMI2
    (void)cpuid;
    return true;
#    else
    return cpuid != NULL && ZL_cpuid_avx2(*cpuid) && ZL_cpuid_bmi2(*cpuid);
#    endif
}

static size_t minSize(size_t a, size_t b)
{
    return a < b ? a : b;
}

// Build the 16-byte right-shuffle control for one 16-lane merge step. Set bits
// yield the running right-source index; clear bits yield 0xFF ^ leftIndex,
// whose high bit zeroes that lane for the right shuffle. XOR-ing the result
// with 0xFF recovers the left-shuffle control (right lanes become 0xFF ^
// rightIndex, which zeroes them for the left shuffle). The high half of the
// low-control load holds popcount(loMask) so adding it to the high-control load
// continues the ranks across the 16-lane boundary. The high control is loaded
// into the top 8 bytes of a vector so it lines up with that boundary.
AVX2_INLINE __m128i pivcoMergeRhsShuffle(uint8_t loMask, uint8_t hiMask)
{
    const __m128i loCtrl =
            _mm_load_si128((const __m128i*)ZL_kPivCoHuffmanMergeLoCtrl[loMask]);
    const __m128i hiCtrl = _mm_slli_si128(
            _mm_loadl_epi64(
                    (const __m128i_u*)ZL_kPivCoHuffmanMergeHiCtrl[hiMask]),
            8);
    return _mm_add_epi8(loCtrl, hiCtrl);
}

// Merge `blockSize` output bytes (a multiple of 16) in 16-lane steps. Each step
// gathers the right values into their lanes and the left values into the rest,
// then ORs. `rhsCur` advances by the popcount (right bytes consumed) and
// `lhsCur` by its complement; the `+ offset` on the left load and store folds
// away because the caller passes a constant blockSize, so the loop fully
// unrolls into straight-line code.
AVX2_INLINE void mergeVectorVectorBlock(
        uint8_t* out,
        const uint8_t* lhs,
        const uint8_t* rhs,
        const uint8_t* bitmap,
        size_t* lhsIdx,
        size_t* rhsIdx,
        size_t kBlockSize)
{
    ptrdiff_t lhsCur = (ptrdiff_t)*lhsIdx;
    ptrdiff_t rhsCur = (ptrdiff_t)*rhsIdx;
#    ifdef __clang__
#        pragma clang loop unroll(full)
#    endif
    for (size_t offset = 0; offset < kBlockSize; offset += 16) {
        uint16_t mask;
        ZL_memcpy(&mask, bitmap + offset / 8, sizeof(mask));
        const __m128i rhsBytes =
                _mm_loadu_si128((const __m128i_u*)(rhs + rhsCur));
        const __m128i lhsBytes =
                _mm_loadu_si128((const __m128i_u*)(lhs + lhsCur + offset));

        const __m128i rhsShuffle =
                pivcoMergeRhsShuffle((uint8_t)mask, (uint8_t)(mask >> 8));
        const __m128i lhsShuffle =
                _mm_xor_si128(rhsShuffle, _mm_set1_epi8((char)0xFF));
        const __m128i rhsOut = _mm_shuffle_epi8(rhsBytes, rhsShuffle);
        const __m128i lhsOut = _mm_shuffle_epi8(lhsBytes, lhsShuffle);
        _mm_storeu_si128(
                (__m128i_u*)(out + offset), _mm_or_si128(rhsOut, lhsOut));

        const size_t ones = (size_t)ZL_popcount64(mask);
        rhsCur += (ptrdiff_t)ones;
        lhsCur -= (ptrdiff_t)ones;
    }
    *lhsIdx = (size_t)(lhsCur + (ptrdiff_t)kBlockSize);
    *rhsIdx = (size_t)rhsCur;
}

// Merge `kBlockSize` output bytes where the left side is the constant `lhsV`:
// gather the right values into the set-bit lanes and blend the constant into
// the clear-bit lanes (the shuffle control's high bit, set on clear lanes,
// drives the blend). `rhsCur` advances by the popcount. Fully unrolls for
// constant kBlockSize.
AVX2_INLINE void mergeConstantVectorBlock(
        uint8_t* out,
        const uint8_t* rhs,
        const uint8_t* bitmap,
        __m128i lhsV,
        size_t* rhsIdx,
        size_t kBlockSize)
{
    size_t rhsCur = *rhsIdx;
#    ifdef __clang__
#        pragma clang loop unroll(full)
#    endif
    for (size_t offset = 0; offset < kBlockSize; offset += 16) {
        uint16_t mask;
        ZL_memcpy(&mask, bitmap + offset / 8, sizeof(mask));
        const __m128i rhsBytes =
                _mm_loadu_si128((const __m128i_u*)(rhs + rhsCur));
        const __m128i ctrl =
                pivcoMergeRhsShuffle((uint8_t)mask, (uint8_t)(mask >> 8));
        const __m128i rhsOut = _mm_shuffle_epi8(rhsBytes, ctrl);
        _mm_storeu_si128(
                (__m128i_u*)(out + offset),
                _mm_blendv_epi8(rhsOut, lhsV, ctrl));

        rhsCur += (size_t)ZL_popcount64(mask);
    }
    *rhsIdx = rhsCur;
}

static AVX2_ATTR size_t mergeVectorVector(
        uint8_t* out,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* lhs,
        size_t lhsSize,
        const uint8_t* rhs,
        size_t rhsSize)
{
    const size_t outSize = lhsSize + rhsSize;
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= (outSize + 7) / 8);
    (void)outCapacity, (void)bitmapCapacity;

    size_t outIdx = 0;
    size_t lhsIdx = 0;
    size_t rhsIdx = 0;

    // Three phases. First, wide 256-byte blocks while all three buffers still
    // have a full block of room -- the common case, no over-read possible. Then
    // 16-byte blocks near the end, whose 16-byte loads may run up to a block
    // past lhs/rhs; the ZL_PIVCO_HUFFMAN_SLOP padding on those buffers absorbs
    // it. Finally, the last < 16 outputs go through a temp buffer so the
    // slop-free bitmap and `out` are not touched past their ends.
    for (;;) {
        const size_t kBlockSize = 256;
        size_t iters            = (outSize - outIdx) / kBlockSize;
        iters = minSize(iters, (lhsSize - lhsIdx) / kBlockSize);
        iters = minSize(iters, (rhsSize - rhsIdx) / kBlockSize);
        if (iters == 0) {
            break;
        }
        const size_t end = outIdx + iters * kBlockSize;
        for (; outIdx < end; outIdx += kBlockSize) {
            mergeVectorVectorBlock(
                    out + outIdx,
                    lhs,
                    rhs,
                    bitmap + outIdx / 8,
                    &lhsIdx,
                    &rhsIdx,
                    kBlockSize);
        }
    }
    for (; lhsIdx <= lhsSize && rhsIdx <= rhsSize;) {
        const size_t kBlockSize = 16;
        size_t iters            = (outSize - outIdx) / kBlockSize;
        iters                   = minSize(
                iters, (lhsSize - lhsIdx + ZL_PIVCO_HUFFMAN_SLOP) / kBlockSize);
        iters = minSize(
                iters, (rhsSize - rhsIdx + ZL_PIVCO_HUFFMAN_SLOP) / kBlockSize);
        if (iters == 0) {
            break;
        }
        const size_t end = outIdx + iters * kBlockSize;
        for (; outIdx < end; outIdx += kBlockSize) {
            mergeVectorVectorBlock(
                    out + outIdx,
                    lhs,
                    rhs,
                    bitmap + outIdx / 8,
                    &lhsIdx,
                    &rhsIdx,
                    kBlockSize);
        }
    }
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
        mergeVectorVectorBlock(
                tmp, lhs, rhs, (const uint8_t*)&mask, &lhsIdx, &rhsIdx, 16);
        ZL_memcpy(out + outIdx, tmp, remaining);
    }
    return rhsIdx;
}

static AVX2_ATTR size_t mergeConstantVector(
        uint8_t* out,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        uint8_t lhs,
        size_t lhsSize,
        const uint8_t* rhs,
        size_t rhsSize)
{
    const size_t outSize = lhsSize + rhsSize;
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= (outSize + 7) / 8);
    (void)outCapacity, (void)bitmapCapacity;

    // Like mergeVectorVector, but the left input is the constant `lhs`: set
    // bits gather a right value and clear bits get `lhs` blended in (so there
    // is no left buffer to advance). Same three phases minus the SLOP concern,
    // since only `rhs` is gathered.
    const __m128i lhsV = _mm_set1_epi8((char)lhs);
    size_t outIdx      = 0;
    size_t rhsIdx      = 0;
    for (; outIdx + 256 <= outSize && rhsIdx + 256 <= rhsSize; outIdx += 256) {
        mergeConstantVectorBlock(
                out + outIdx, rhs, bitmap + outIdx / 8, lhsV, &rhsIdx, 256);
    }
    for (; outIdx + 16 <= outSize && rhsIdx <= rhsSize; outIdx += 16) {
        mergeConstantVectorBlock(
                out + outIdx, rhs, bitmap + outIdx / 8, lhsV, &rhsIdx, 16);
    }
    if (outIdx < outSize && rhsIdx <= rhsSize) {
        const size_t remaining = outSize - outIdx;
        assert(remaining < 16);
        uint16_t mask = bitmap[outIdx / 8];
        if (outIdx + 8 < outSize) {
            mask |= (uint16_t)((uint16_t)bitmap[(outIdx + 8) / 8] << 8);
        }
        // Clear bits past the valid tail so those lanes take the constant and
        // do not over-count right elements.
        mask &= (uint16_t)((1u << remaining) - 1u);
        uint8_t const bits[2] = { (uint8_t)mask, (uint8_t)(mask >> 8) };
        uint8_t tmp[16];
        mergeConstantVectorBlock(tmp, rhs, bits, lhsV, &rhsIdx, 16);
        ZL_memcpy(out + outIdx, tmp, remaining);
    }
    return rhsIdx;
}

// Shared signature of every mergeFlatDepthN kernel, so mergeFlatDepthTail can
// call back into the matching one. Params mirror the kernels:
// (out, outSize, outCapacity, bitmap, bitmapCapacity, symbols).
typedef void (*mergeFlatDepthKernel)(
        uint8_t*,
        size_t,
        size_t,
        const uint8_t*,
        size_t,
        const uint8_t*);

// Number of elements the flat-depth tail decodes in one shot. The fast loops
// leave a remainder bounded by `16 + 128/depth` elements (worst case depth==3,
// < 59), covering both the bitmap-limited stop and the tiny-input case where
// the fast loop runs zero blocks. 64 >= that bound, so a single block drains
// the whole tail. It is also a multiple of 32 (all kernels' native block) and
// its packed bitmap (<= 64*7/8 = 56 bytes) plus each kernel's SIMD over-read
// fit the 64-byte scratch below.
#    define FLAT_DEPTH_TAIL_ELEMS 64

// Decode the final `outSize - outIdx` (< FLAT_DEPTH_TAIL_ELEMS) flat-depth
// elements without a scalar fallback: copy the packed-bitmap tail into a
// zero-padded temporary, run one FLAT_DEPTH_TAIL_ELEMS-element block of
// `kernel` into a temporary output, then copy out the valid bytes. Mirrors the
// temp-buffer tail used by the merge kernels: the temp bitmap absorbs the
// block's over-read and the temp output absorbs its over-write, so neither the
// (slop-free) bitmap nor `out` is touched past its bounds.
AVX2_INLINE void mergeFlatDepthTail(
        mergeFlatDepthKernel kernel,
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        size_t depth,
        const uint8_t* symbols,
        size_t outIdx)
{
    const size_t remaining = outSize - outIdx;
    assert(remaining < FLAT_DEPTH_TAIL_ELEMS);
    const size_t byteOffset = (outIdx * depth) / 8;
    const size_t start      = minSize(byteOffset, bitmapCapacity);
    const size_t toCopy =
            minSize((remaining * depth + 7) / 8, bitmapCapacity - start);

    ZL_ALIGNED(64) uint8_t tmpBitmap[64];
    ZL_memset(tmpBitmap, 0, sizeof(tmpBitmap));
    ZL_memcpy(tmpBitmap, bitmap + start, toCopy);

    ZL_ALIGNED(64) uint8_t tmpOut[64];
    kernel(tmpOut,
           FLAT_DEPTH_TAIL_ELEMS,
           FLAT_DEPTH_TAIL_ELEMS,
           tmpBitmap,
           sizeof(tmpBitmap),
           symbols);
    ZL_memcpy(out + outIdx, tmpOut, remaining);
}

static AVX2_ATTR void mergeFlatDepth1(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* symbols)
{
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= (outSize + 7) / 8);
    (void)outCapacity, (void)bitmapCapacity;

    // Depth 1: each output is one bit selecting symbols[0] (clear) or
    // symbols[1] (set). `spreadCtrl` replicates each bitmap byte across its 8
    // output lanes and `bitSel` isolates one bit per lane; `cmpeq` turns the
    // isolated bit into a 0x00/0xFF mask that selects between the two symbols
    // as sym0 ^ (mask & (sym0 ^ sym1)). 32 outputs per store.
    const __m256i lhs    = _mm256_set1_epi8((char)symbols[0]);
    const __m256i rhs    = _mm256_set1_epi8((char)symbols[1]);
    const __m256i diff   = _mm256_xor_si256(lhs, rhs);
    const __m256i bitSel = _mm256_set1_epi64x((long long)0x8040201008040201ULL);
    const __m256i spreadCtrl = _mm256_setr_epi64x(
            0x0000000000000000,
            0x0101010101010101,
            0x0202020202020202,
            0x0303030303030303);

    size_t outIdx = 0;
    for (; outIdx + 64 <= outSize; outIdx += 64) {
        uint32_t bits0;
        uint32_t bits1;
        ZL_memcpy(&bits0, bitmap + outIdx / 8, sizeof(bits0));
        ZL_memcpy(&bits1, bitmap + outIdx / 8 + 4, sizeof(bits1));

        const __m256i spread0 =
                _mm256_shuffle_epi8(_mm256_set1_epi32((int)bits0), spreadCtrl);
        const __m256i mask0 =
                _mm256_cmpeq_epi8(_mm256_and_si256(spread0, bitSel), bitSel);
        _mm256_storeu_si256(
                (__m256i_u*)(out + outIdx),
                _mm256_xor_si256(lhs, _mm256_and_si256(mask0, diff)));

        const __m256i spread1 =
                _mm256_shuffle_epi8(_mm256_set1_epi32((int)bits1), spreadCtrl);
        const __m256i mask1 =
                _mm256_cmpeq_epi8(_mm256_and_si256(spread1, bitSel), bitSel);
        _mm256_storeu_si256(
                (__m256i_u*)(out + outIdx + 32),
                _mm256_xor_si256(lhs, _mm256_and_si256(mask1, diff)));
    }

    if (outIdx < outSize) {
        mergeFlatDepthTail(
                mergeFlatDepth1,
                out,
                outSize,
                bitmap,
                bitmapCapacity,
                1,
                symbols,
                outIdx);
    }
}

static AVX2_ATTR void mergeFlatDepth2(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* symbols)
{
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= (outSize + 7) / 8);
    (void)outCapacity, (void)bitmapCapacity;

    // Decode 32 depth-2 outputs per iteration, 16 per 128-bit lane.
    // `_mm256_set1_epi64x` broadcasts the 8 loaded bitmap bytes to both 128-bit
    // lanes, so each lane holds the low word (outputs 0..15) in its low 4 bytes
    // and the high word (outputs 16..31) in its next 4 bytes. `kSpreadShuf`
    // then fans, per lane, the correct word across all four 32-bit sublanes.
    // Each 32-bit sublane is then shifted so its four depth-2 indices land at
    // byte boundaries, `kShuffle` gathers them into order, `kMask` isolates the
    // low 2 bits, and `lookup` maps each index to its symbol.
    const __m256i kSpreadShuf = _mm256_setr_m128i(
            _mm_set1_epi32(0x03020100), _mm_set1_epi32(0x07060504));
    const __m256i kShift   = _mm256_setr_epi32(0, 2, 4, 6, 0, 2, 4, 6);
    const __m256i kShuffle = _mm256_broadcastsi128_si256(_mm_setr_epi8(
            0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15));
    const __m256i kMask    = _mm256_set1_epi8(3);
    const __m256i lookup   = _mm256_broadcastsi128_si256(_mm_setr_epi8(
            (char)symbols[0],
            (char)symbols[1],
            (char)symbols[2],
            (char)symbols[3],
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0));

    size_t outIdx = 0;
    for (; outIdx + 32 <= outSize; outIdx += 32) {
        const __m256i bits = _mm256_shuffle_epi8(
                _mm256_set1_epi64x((long long)ZL_readLE64(bitmap + outIdx / 4)),
                kSpreadShuf);
        const __m256i shifted = _mm256_srlv_epi32(bits, kShift);
        const __m256i indices =
                _mm256_and_si256(_mm256_shuffle_epi8(shifted, kShuffle), kMask);
        _mm256_storeu_si256(
                (__m256i_u*)(out + outIdx),
                _mm256_shuffle_epi8(lookup, indices));
    }

    if (outIdx < outSize) {
        mergeFlatDepthTail(
                mergeFlatDepth2,
                out,
                outSize,
                bitmap,
                bitmapCapacity,
                2,
                symbols,
                outIdx);
    }
}

static AVX2_ATTR void mergeFlatDepth3(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* symbols)
{
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= (outSize + 7) / 8);
    (void)outCapacity, (void)bitmapCapacity;

    // Decode 64 depth-3 outputs per iteration. Depth-3 codes straddle byte
    // boundaries awkwardly, so they are extracted two at a time as 6-bit pairs.
    // Each 16-bit lane handles two adjacent pairs: `kShuffle` loads the two
    // source bytes covering them, `kMul` (x16 on even 16-bit lanes, x1 on odd)
    // aligns both pairs to bits 4..9 and 10..15, and the mask/or packs one
    // 6-bit pair into each byte. Every pair then splits into its low and high
    // 3-bit code, which index the 8-entry symbol table.
    //
    // The two 128-bit lanes decode outputs 0..31 (bytes 0..11) and 32..63
    // (bytes 12..23). `_mm256_shuffle_epi8` is per-128-bit-lane, so the low
    // lane is loaded with bytes 0..15 and the high lane with bytes 8..23 --
    // both stay within the 24 bytes this iteration consumes (no over-read).
    // Since the high lane starts at byte 8, its half of `kShuffle` is the low
    // half's pattern +4.
    const __m128i kShuffleLo =
            _mm_setr_epi8(0, 1, 1, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 10, 11);
    const __m256i kShuffle = _mm256_setr_m128i(
            kShuffleLo, _mm_add_epi8(kShuffleLo, _mm_set1_epi8(4)));
    const __m256i kMul   = _mm256_set1_epi32(16 | (1 << 16));
    const __m256i kMask  = _mm256_set1_epi8(0x7);
    const __m256i lookup = _mm256_broadcastsi128_si256(
            _mm_loadl_epi64((const __m128i_u*)symbols));

    size_t outIdx = 0;
    for (size_t bitIdx = 0; outIdx + 64 <= outSize;
         outIdx += 64, bitIdx += 24) {
        const __m256i bits = _mm256_set_m128i(
                _mm_loadu_si128((const __m128i_u*)(bitmap + bitIdx + 8)),
                _mm_loadu_si128((const __m128i_u*)(bitmap + bitIdx)));
        const __m256i spread  = _mm256_shuffle_epi8(bits, kShuffle);
        const __m256i shifted = _mm256_mullo_epi16(spread, kMul);
        // Pack the two aligned pairs of each 16-bit lane into its two bytes:
        // the pair at bits 4..9 into the low byte, the pair at bits 10..15 into
        // the high byte.
        const __m256i pairs = _mm256_or_si256(
                _mm256_and_si256(
                        _mm256_srli_epi16(shifted, 4),
                        _mm256_set1_epi16(0x003F)),
                _mm256_and_si256(
                        _mm256_srli_epi16(shifted, 2),
                        _mm256_set1_epi16(0x3F00)));
        // Split each 6-bit pair into its two 3-bit codes: the low 3 bits in
        // symbols0, the high 3 bits in symbols1. Look up both.
        const __m256i symbols0 =
                _mm256_shuffle_epi8(lookup, _mm256_and_si256(pairs, kMask));
        const __m256i symbols1 = _mm256_shuffle_epi8(
                lookup, _mm256_and_si256(_mm256_srli_epi16(pairs, 3), kMask));
        // Interleave symbols back into output order: unpacklo yields outputs
        // {0..15,32..47} and unpackhi {16..31,48..63}, so `permute2x128`
        // reunites each contiguous 32-output half before storing.
        const __m256i interleavedLo = _mm256_unpacklo_epi8(symbols0, symbols1);
        const __m256i interleavedHi = _mm256_unpackhi_epi8(symbols0, symbols1);
        _mm256_storeu_si256(
                (__m256i_u*)(out + outIdx),
                _mm256_permute2x128_si256(interleavedLo, interleavedHi, 0x20));
        _mm256_storeu_si256(
                (__m256i_u*)(out + outIdx + 32),
                _mm256_permute2x128_si256(interleavedLo, interleavedHi, 0x31));
    }

    if (outIdx < outSize) {
        mergeFlatDepthTail(
                mergeFlatDepth3,
                out,
                outSize,
                bitmap,
                bitmapCapacity,
                3,
                symbols,
                outIdx);
    }
}

static AVX2_ATTR void mergeFlatDepth4(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* symbols)
{
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= (outSize + 7) / 8);
    (void)outCapacity, (void)bitmapCapacity;

    const __m256i kMask  = _mm256_set1_epi8(0x0f);
    const __m256i lookup = _mm256_broadcastsi128_si256(
            _mm_loadu_si128((const __m128i_u*)symbols));

    size_t outIdx = 0;
    // Decode 64 depth-4 outputs per iteration: split each bitmap byte into its
    // low and high nibble (the two indices it packs), interleave them back into
    // output order, then map indices to symbols. The 256-bit interleaves work
    // per 128-bit lane, so `il`/`ih` hold outputs {0..15,32..47} and
    // {16..31,48..63}; `permute2x128` reunites each contiguous 32-output half
    // before storing.
    for (; outIdx + 64 <= outSize; outIdx += 64) {
        const __m256i bits =
                _mm256_loadu_si256((const __m256i_u*)(bitmap + outIdx / 2));
        const __m256i lo = _mm256_and_si256(bits, kMask);
        const __m256i hi = _mm256_and_si256(_mm256_srli_epi16(bits, 4), kMask);
        const __m256i indicesLo = _mm256_unpacklo_epi8(lo, hi);
        const __m256i indicesHi = _mm256_unpackhi_epi8(lo, hi);
        const __m256i symbolsLo = _mm256_shuffle_epi8(lookup, indicesLo);
        const __m256i symbolsHi = _mm256_shuffle_epi8(lookup, indicesHi);
        _mm256_storeu_si256(
                (__m256i_u*)(out + outIdx),
                _mm256_permute2x128_si256(symbolsLo, symbolsHi, 0x20));
        _mm256_storeu_si256(
                (__m256i_u*)(out + outIdx + 32),
                _mm256_permute2x128_si256(symbolsLo, symbolsHi, 0x31));
    }

    if (outIdx < outSize) {
        mergeFlatDepthTail(
                mergeFlatDepth4,
                out,
                outSize,
                bitmap,
                bitmapCapacity,
                4,
                symbols,
                outIdx);
    }
}

// A symbol table has 2^depth entries; for depth > 4 that exceeds one `pshufb`
// (which reaches 16 bytes), so it is split into 16-entry "pages". A code's low
// 4 bits index within a page (`pshufb` uses only those, since codes are < 128
// so the high bit is clear), and its higher bits choose the page, blended
// together: bit 4 picks between 2 pages, bit 5 between page-pairs, bit 6
// between the two halves of 8. Each `pages[]` entry is one page broadcast to
// both 128-bit lanes.
AVX2_INLINE __m256i lookup2Pages(__m256i indices, const __m256i pages[2])
{
    __m256i const bit4 = _mm256_cmpeq_epi8(
            _mm256_and_si256(indices, _mm256_set1_epi8(0x10)),
            _mm256_set1_epi8(0x10));

    __m256i const page0 = _mm256_shuffle_epi8(pages[0], indices);
    __m256i const page1 = _mm256_shuffle_epi8(pages[1], indices);
    return _mm256_blendv_epi8(page0, page1, bit4);
}

static AVX2_ATTR void mergeFlatDepth5(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* symbols)
{
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= (outSize + 7) / 8);
    (void)outCapacity, (void)bitmapCapacity;

    // Decode 32 depth-5 outputs per iteration with the same strategy as
    // mergeFlatDepth6, extracting codes two at a time as 10-bit pairs (two
    // 5-bit codes). `kShuffle` gathers, into each 16-bit lane, the two source
    // bytes covering a pair; `kMul` multiplies each lane so its pair lands at
    // bits 6..15. Unlike depth 6, 10-bit pairs start at sub-byte offsets
    // 0/2/4/6, so the per-lane factors cycle 64/16/4/1 (i.e. <<6/<<4/<<2/<<0).
    // The mask/or then packs the low 5-bit code (bits 6..10) into the low byte
    // and the high code (bits 11..15) into the high byte. Codes index the
    // 32-entry table as 2 pages of 16.
    //
    // The two 128-bit lanes decode outputs 0..15 (bytes 0..9) and 16..31 (bytes
    // 10..19), already in order, so the 256-bit result stores directly. The low
    // lane loads bytes 0..15 and the high lane bytes 4..19 (no over-read of the
    // 20 bytes consumed), so the high half of `kShuffle` is the low half's byte
    // pattern +6.
    const __m128i kShuffleLo =
            _mm_setr_epi8(0, 1, 1, 2, 2, 3, 3, 4, 5, 6, 6, 7, 7, 8, 8, 9);
    const __m256i kShuffle = _mm256_setr_m128i(
            kShuffleLo, _mm_add_epi8(kShuffleLo, _mm_set1_epi8(6)));
    const __m256i kMul =
            _mm256_set1_epi64x(64 | (16 << 16) | (4ull << 32) | (1ull << 48));

    __m256i pages[2];
    for (size_t pageIdx = 0; pageIdx < 2; ++pageIdx) {
        pages[pageIdx] = _mm256_broadcastsi128_si256(
                _mm_loadu_si128((const __m128i_u*)(symbols + pageIdx * 16)));
    }

    size_t outIdx = 0;
    for (size_t bitIdx = 0; outIdx + 32 <= outSize;
         outIdx += 32, bitIdx += 20) {
        const __m256i bits = _mm256_setr_m128i(
                _mm_loadu_si128((const __m128i_u*)(bitmap + bitIdx)),
                _mm_loadu_si128((const __m128i_u*)(bitmap + bitIdx + 4)));
        const __m256i spread  = _mm256_shuffle_epi8(bits, kShuffle);
        const __m256i shifted = _mm256_mullo_epi16(spread, kMul);
        // Low 5-bit code of each 16-bit lane (bits 6..10) into the low byte,
        // high code (bits 11..15) into the high byte.
        const __m256i codes = _mm256_or_si256(
                _mm256_and_si256(
                        _mm256_srli_epi16(shifted, 6),
                        _mm256_set1_epi16(0x001F)),
                _mm256_and_si256(
                        _mm256_srli_epi16(shifted, 3),
                        _mm256_set1_epi16(0x1F00)));
        _mm256_storeu_si256(
                (__m256i_u*)(out + outIdx), lookup2Pages(codes, pages));
    }

    if (outIdx < outSize) {
        mergeFlatDepthTail(
                mergeFlatDepth5,
                out,
                outSize,
                bitmap,
                bitmapCapacity,
                5,
                symbols,
                outIdx);
    }
}

// 64-entry lookup: bit 5 selects which pair of pages, bit 4 within it.
AVX2_INLINE __m256i lookup4Pages(__m256i indices, const __m256i pages[4])
{
    __m256i const bit5 = _mm256_cmpeq_epi8(
            _mm256_and_si256(indices, _mm256_set1_epi8(0x20)),
            _mm256_set1_epi8(0x20));

    __m256i const lo = lookup2Pages(indices, pages);
    __m256i const hi = lookup2Pages(indices, pages + 2);
    return _mm256_blendv_epi8(lo, hi, bit5);
}

static AVX2_ATTR void mergeFlatDepth6(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* symbols)
{
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= (outSize + 7) / 8);
    (void)outCapacity, (void)bitmapCapacity;

    // Decode 32 depth-6 outputs per iteration using the same 6-bit extraction
    // as mergeFlatDepth3 -- but here each extracted 6-bit value is a whole
    // code, not a pair of 3-bit codes. `kShuffle` gathers, into each 16-bit
    // lane, the two source bytes covering two adjacent codes; `kMul` (x16 on
    // even 16-bit lanes, x1 on odd) aligns both codes to bits 4..9 and 10..15;
    // the mask/or packs one 6-bit code into each byte. The 64-entry symbol
    // table is too big for one `pshufb`, so `lookup4Pages` selects
    // among 4 pages of 16.
    //
    // The two 128-bit lanes decode outputs 0..15 (bytes 0..11) and 16..31
    // (bytes 12..23), already in order, so the 256-bit result stores directly
    // (no interleave). As in mergeFlatDepth3 the low lane loads bytes 0..15 and
    // the high lane bytes 8..23 (no over-read), so the high half of `kShuffle`
    // is the low half's pattern +4.
    const __m128i kShuffleLo =
            _mm_setr_epi8(0, 1, 1, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 10, 11);
    const __m256i kShuffle = _mm256_setr_m128i(
            kShuffleLo, _mm_add_epi8(kShuffleLo, _mm_set1_epi8(4)));
    const __m256i kMul = _mm256_set1_epi32(16 | (1 << 16));

    __m256i pages[4];
    for (size_t pageIdx = 0; pageIdx < 4; ++pageIdx) {
        pages[pageIdx] = _mm256_broadcastsi128_si256(
                _mm_loadu_si128((const __m128i_u*)(symbols + pageIdx * 16)));
    }

    size_t outIdx = 0;
    for (size_t bitIdx = 0; outIdx + 32 <= outSize;
         outIdx += 32, bitIdx += 24) {
        const __m256i bits = _mm256_setr_m128i(
                _mm_loadu_si128((const __m128i_u*)(bitmap + bitIdx)),
                _mm_loadu_si128((const __m128i_u*)(bitmap + bitIdx + 8)));
        const __m256i spread  = _mm256_shuffle_epi8(bits, kShuffle);
        const __m256i shifted = _mm256_mullo_epi16(spread, kMul);
        // Each 16-bit lane's two aligned codes: bits 4..9 into the low byte,
        // bits 10..15 into the high byte.
        const __m256i codes = _mm256_or_si256(
                _mm256_and_si256(
                        _mm256_srli_epi16(shifted, 4),
                        _mm256_set1_epi16(0x003F)),
                _mm256_and_si256(
                        _mm256_srli_epi16(shifted, 2),
                        _mm256_set1_epi16(0x3F00)));
        _mm256_storeu_si256(
                (__m256i_u*)(out + outIdx), lookup4Pages(codes, pages));
    }

    if (outIdx < outSize) {
        mergeFlatDepthTail(
                mergeFlatDepth6,
                out,
                outSize,
                bitmap,
                bitmapCapacity,
                6,
                symbols,
                outIdx);
    }
}

// 128-entry lookup: bit 6 selects which group of 4 pages, then bits 5/4 within.
AVX2_INLINE __m256i lookup8Pages(__m256i indices, const __m256i* pages)
{
    __m256i const bit6 = _mm256_cmpeq_epi8(
            _mm256_and_si256(indices, _mm256_set1_epi8(0x40)),
            _mm256_set1_epi8(0x40));
    __m256i const low  = lookup4Pages(indices, pages);
    __m256i const high = lookup4Pages(indices, pages + 4);
    return _mm256_blendv_epi8(low, high, bit6);
}

static AVX2_ATTR void mergeFlatDepth7(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* symbols)
{
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= (outSize + 7) / 8);
    (void)outCapacity, (void)bitmapCapacity;

    // Decode 32 depth-7 outputs per iteration, 4 codes per 32-bit lane. Four
    // 7-bit codes span 28 bits and start only at byte offsets 0 or 4 (28 mod
    // 8), so 28 + 4 = 32 fits a 32-bit lane exactly -- unlike the
    // 2-codes-per-16-bit pair trick, where 14 + 6 = 20 > 16. `kGather` loads
    // each lane's 4 source bytes; `kShift` shifts each lane left by 4 or 0 (via
    // `sllv`) so its 28-bit group lands at bits 4..31; the shift/mask/or then
    // slices the four 7-bit codes into the lane's four bytes, already in order
    // -- so the 256-bit result needs no pack and stores directly. The 128-entry
    // table is 8 pages of 16.
    //
    // The two 128-bit lanes decode outputs 0..15 (bytes 0..13) and 16..31
    // (bytes 14..27). The low lane loads bytes 0..15 and the high lane
    // bytes 12..27 (no over-read of the 28 bytes consumed), so the high half of
    // `kGather` is the low half's byte pattern +2.
    const __m128i kGatherLo =
            _mm_setr_epi8(0, 1, 2, 3, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11, 12, 13);
    const __m256i kGather = _mm256_setr_m128i(
            kGatherLo, _mm_add_epi8(kGatherLo, _mm_set1_epi8(2)));
    const __m256i kShift = _mm256_setr_epi32(4, 0, 4, 0, 4, 0, 4, 0);

    __m256i pages[8];
    for (size_t pageIdx = 0; pageIdx < 8; ++pageIdx) {
        pages[pageIdx] = _mm256_broadcastsi128_si256(
                _mm_loadu_si128((const __m128i_u*)(symbols + pageIdx * 16)));
    }

    size_t outIdx = 0;
    for (size_t bitIdx = 0; outIdx + 32 <= outSize;
         outIdx += 32, bitIdx += 28) {
        const __m256i bits = _mm256_set_m128i(
                _mm_loadu_si128((const __m128i_u*)(bitmap + bitIdx + 12)),
                _mm_loadu_si128((const __m128i_u*)(bitmap + bitIdx)));
        const __m256i gathered = _mm256_shuffle_epi8(bits, kGather);
        const __m256i aligned  = _mm256_sllv_epi32(gathered, kShift);
        // Slice the four aligned 7-bit codes (bits 4..10, 11..17, 18..24,
        // 25..31) into the lane's four bytes, in order.
        const __m256i codes = _mm256_or_si256(
                _mm256_or_si256(
                        _mm256_and_si256(
                                _mm256_srli_epi32(aligned, 4),
                                _mm256_set1_epi32(0x0000007F)),
                        _mm256_and_si256(
                                _mm256_srli_epi32(aligned, 3),
                                _mm256_set1_epi32(0x00007F00))),
                _mm256_or_si256(
                        _mm256_and_si256(
                                _mm256_srli_epi32(aligned, 2),
                                _mm256_set1_epi32(0x007F0000)),
                        _mm256_and_si256(
                                _mm256_srli_epi32(aligned, 1),
                                _mm256_set1_epi32(0x7F000000))));
        _mm256_storeu_si256(
                (__m256i_u*)(out + outIdx), lookup8Pages(codes, pages));
    }

    if (outIdx < outSize) {
        mergeFlatDepthTail(
                mergeFlatDepth7,
                out,
                outSize,
                bitmap,
                bitmapCapacity,
                7,
                symbols,
                outIdx);
    }
}

static AVX2_ATTR void mergeFlatDepth8(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* symbols)
{
    // Depth 8: each output is a full-byte index into the 256-entry table, so
    // this is a direct table lookup -- left as a scalar loop for the compiler
    // to auto-vectorize.
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= (outSize + 7) / 8);
    (void)outCapacity, (void)bitmapCapacity;
    for (size_t outIdx = 0; outIdx < outSize; ++outIdx) {
        out[outIdx] = symbols[bitmap[outIdx]];
    }
}

static AVX2_ATTR void mergeFlatDepth(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        size_t depth,
        const uint8_t* symbols)
{
    switch (depth) {
        case 1:
            mergeFlatDepth1(
                    out, outSize, outCapacity, bitmap, bitmapCapacity, symbols);
            return;
        case 2:
            mergeFlatDepth2(
                    out, outSize, outCapacity, bitmap, bitmapCapacity, symbols);
            return;
        case 3:
            mergeFlatDepth3(
                    out, outSize, outCapacity, bitmap, bitmapCapacity, symbols);
            return;
        case 4:
            mergeFlatDepth4(
                    out, outSize, outCapacity, bitmap, bitmapCapacity, symbols);
            return;
        case 5:
            mergeFlatDepth5(
                    out, outSize, outCapacity, bitmap, bitmapCapacity, symbols);
            return;
        case 6:
            mergeFlatDepth6(
                    out, outSize, outCapacity, bitmap, bitmapCapacity, symbols);
            return;
        case 7:
            mergeFlatDepth7(
                    out, outSize, outCapacity, bitmap, bitmapCapacity, symbols);
            return;
        case 8:
            mergeFlatDepth8(
                    out, outSize, outCapacity, bitmap, bitmapCapacity, symbols);
            return;
        default:
            assert(false);
            return;
    }
}

const ZL_PivCoHuffmanDecode ZL_PivCoHuffmanDecode_avx2 = {
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

const ZL_PivCoHuffmanDecode ZL_PivCoHuffmanDecode_avx2 = {
    .supported = supported,
};

#endif
