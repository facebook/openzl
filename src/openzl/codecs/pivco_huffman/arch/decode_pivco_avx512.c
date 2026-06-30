// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/arch/decode_pivco_arch.h"

#if ZL_ARCH_X86_64 && ZL_HAS_ATTRIBUTE(__target__)

#    include <immintrin.h>
#    include <stdint.h>

#    include "common_pivco_avx512_tables.h"
#    include "openzl/shared/mem.h"
#    include "openzl/shared/utils.h"

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

/**************************************************************
 * mergeVector*
 **************************************************************/

/// @returns a 64-bit lane mask with the low @p bits bits set. Selects the valid
/// lanes/bytes of a partial 64-wide block.
static __mmask64 tailMask(size_t bits)
{
    return bits >= 64 ? ~(__mmask64)0 : (((__mmask64)1 << bits) - 1);
}

/**
 * Merges up to 64 output bytes for one 64-bit slice of the partition @p bits:
 * bit i picks the right child (1) or left child (0) for output lane i. A vector
 * child supplies bytes consecutively from its stream; a constant child supplies
 * a splatted value.
 *
 * The merge uses `expandloadu`, the inverse of the encoder's `compressstoreu`:
 * it reads contiguous bytes from a child stream and scatters them into the
 * lanes selected by a mask, so each child's bytes land back in their original
 * positions.
 *
 * @param lhsPos/rhsPos In/out cursors into the (contiguous) child streams;
 * advanced by the number of bytes this block consumed from each.
 * @param lhsVec/rhsVec Splatted constant values, used when that side is
 * constant (kLhsVector/kRhsVector false).
 * @param numBits Valid lanes in this block (64 for a full block; the tail mask
 * keeps the loop from reading past either stream).
 * @param kLhsVector/kRhsVector Compile-time flags selecting vector vs constant
 * for each side (at least one is a vector).
 */
ZL_AVX512_INLINE __m512i mergeVectorBlock(
        const uint8_t* lhs,
        size_t* lhsPos,
        __m512i lhsVec,
        const uint8_t* rhs,
        size_t* rhsPos,
        __m512i rhsVec,
        uint64_t bits,
        size_t numBits,
        const bool kLhsVector,
        const bool kRhsVector)
{
    // Restrict the partition to valid lanes; lhsMask/rhsMask are complementary
    // within those lanes. blockOnes counts how many bytes the right child
    // feeds.
    __mmask64 valid        = tailMask(numBits);
    __mmask64 rhsMask      = (__mmask64)bits & valid;
    __mmask64 lhsMask      = (__mmask64)(~bits) & valid;
    const size_t blockOnes = (size_t)ZL_popcount64((uint64_t)rhsMask);

    __m512i merged;
    if (kLhsVector && kRhsVector) {
        // Expand each contiguous stream into its selected lanes (zero
        // elsewhere); the two are disjoint, so OR fuses them into the output.
        const __m512i expandedRhs =
                _mm512_maskz_expandloadu_epi8(rhsMask, rhs + *rhsPos);
        const __m512i expandedLhs =
                _mm512_maskz_expandloadu_epi8(lhsMask, lhs + *lhsPos);
        merged = _mm512_or_si512(expandedLhs, expandedRhs);
    } else if (kLhsVector) {
        // rhs is constant: start from the rhs splat and overwrite the 0-bit
        // lanes with expanded lhs bytes.
        merged = _mm512_mask_expandloadu_epi8(rhsVec, lhsMask, lhs + *lhsPos);
    } else {
        assert(kRhsVector);
        // lhs is constant: start from the lhs splat and overwrite the 1-bit
        // lanes with expanded rhs bytes.
        merged = _mm512_mask_expandloadu_epi8(lhsVec, rhsMask, rhs + *rhsPos);
    }

    // Advance each vector cursor by the bytes it actually supplied.
    if (kLhsVector) {
        *lhsPos += numBits - blockOnes;
    }
    if (kRhsVector) {
        *rhsPos += blockOnes;
    }
    return merged;
}

/**
 * Shared body of the three merge kernels. Reconstructs `lhsSize + rhsSize`
 * output bytes by walking the partition bitmap 64 bits at a time and merging
 * each block with mergeVectorBlock. A constant side passes its value via
 * @p lhsValue / @p rhsValue (splatted once up front); a vector side reads from
 * @p lhs / @p rhs. At least one side is a vector.
 *
 * @returns the number of 1-bits consumed (the right-child size as recomputed
 * from the bitmap). The caller compares it to @p rhsSize: a mismatch means a
 * corrupt bitstream. The early returns inside the loops fire as soon as a
 * vector cursor overruns its stream -- the count returned there is guaranteed
 * to differ from @p rhsSize, so corruption is still reported (and we stop
 * before reading further out of bounds).
 *
 * @note bitmapCapacity/outCapacity are asserted, then ignored: the loop relies
 * on the documented SLOP so the full-width 64-byte loads/stores are safe.
 */
ZL_AVX512_INLINE size_t mergeVectorImpl(
        uint8_t* out,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* lhs,
        uint8_t lhsValue,
        size_t lhsSize,
        const uint8_t* rhs,
        uint8_t rhsValue,
        size_t rhsSize,
        const bool kLhsVector,
        const bool kRhsVector)
{
    const size_t outSize = (size_t)(lhsSize + rhsSize);
    assert(outCapacity >= outSize);
    assert(bitmapCapacity >= (outSize + 7) / 8);
    assert(kLhsVector || kRhsVector);
    (void)outCapacity;
    (void)bitmapCapacity;

    size_t lhsPos = 0;
    size_t rhsPos = 0;

    const __m512i lhsVec = _mm512_set1_epi8((char)lhsValue);
    const __m512i rhsVec = _mm512_set1_epi8((char)rhsValue);

    // Main loop: process kUnroll outputs at a time in fully-unrolled 64-wide
    // steps. Every block here is full-width (numBits == 64) and uses unmasked
    // loads/stores, which is the fast path.
    size_t bitOffset      = 0;
    const size_t kUnroll  = 256;
    const size_t outLimit = outSize & ~(kUnroll - 1);
    for (; bitOffset < outLimit; bitOffset += kUnroll) {
#    ifdef __clang__
#        pragma clang loop unroll(full)
#    endif
        for (size_t u = 0; u < kUnroll; u += 64) {
            const uint64_t bits  = ZL_readLE64(bitmap + bitOffset / 8 + u / 8);
            const __m512i merged = mergeVectorBlock(
                    lhs,
                    &lhsPos,
                    lhsVec,
                    rhs,
                    &rhsPos,
                    rhsVec,
                    bits,
                    64,
                    kLhsVector,
                    kRhsVector);
            _mm512_storeu_si512(out + bitOffset + u, merged);
            // Corruption guard: a cursor past its stream means the bitmap had
            // more 1s (or 0s) than the child has bytes. Bail with a count that
            // can't equal rhsSize so the caller flags it.
            if (kRhsVector && ZL_UNLIKELY(rhsPos > rhsSize)) {
                return rhsPos;
            }
            if (kLhsVector && ZL_UNLIKELY(lhsPos > lhsSize)) {
                assert(outSize - lhsPos < rhsSize);
                return outSize - lhsPos;
            }
        }
    }

    // Tail: the remaining < kUnroll outputs, handled 64 at a time with the last
    // block masked to numBits valid lanes.
    for (; bitOffset < outSize; bitOffset += 64) {
        const size_t numBits  = ZL_MIN(outSize - bitOffset, 64);
        const __mmask64 valid = tailMask(numBits);
        const uint64_t bits =
                ZL_readLE64_N(bitmap + bitOffset / 8, (numBits + 7) / 8);
        const __m512i merged = mergeVectorBlock(
                lhs,
                &lhsPos,
                lhsVec,
                rhs,
                &rhsPos,
                rhsVec,
                bits,
                numBits,
                kLhsVector,
                kRhsVector);
        _mm512_mask_storeu_epi8(out + bitOffset, valid, merged);
        if (kRhsVector && ZL_UNLIKELY(rhsPos > rhsSize)) {
            return rhsPos;
        }
        if (kLhsVector && ZL_UNLIKELY(lhsPos > lhsSize)) {
            assert(outSize - lhsPos < rhsSize);
            return outSize - lhsPos;
        }
    }

    // ones count: rhsPos directly, or (for a constant rhs) the lanes not taken
    // from the lhs vector.
    return kRhsVector ? rhsPos : outSize - lhsPos;
}

/// Merge where both children are decoded vectors. @see
/// ZL_PivCoHuffmanDecode::mergeVectorVector.
static ZL_AVX512_ATTR size_t mergeVectorVector(
        uint8_t* out,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* lhs,
        size_t lhsSize,
        const uint8_t* rhs,
        size_t rhsSize)
{
    return mergeVectorImpl(
            out,
            outCapacity,
            bitmap,
            bitmapCapacity,
            lhs,
            0,
            lhsSize,
            rhs,
            0,
            rhsSize,
            true,
            true);
}

/// Merge where the left child is a constant symbol and the right is a vector.
/// @see ZL_PivCoHuffmanDecode::mergeConstantVector.
static ZL_AVX512_ATTR size_t mergeConstantVector(
        uint8_t* out,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        uint8_t lhs,
        size_t lhsSize,
        const uint8_t* rhs,
        size_t rhsSize)
{
    return mergeVectorImpl(
            out,
            outCapacity,
            bitmap,
            bitmapCapacity,
            NULL,
            lhs,
            lhsSize,
            rhs,
            0,
            rhsSize,
            false,
            true);
}

/// Merge where the left child is a vector and the right is a constant symbol.
/// @see ZL_PivCoHuffmanDecode::mergeVectorConstant.
static ZL_AVX512_ATTR size_t mergeVectorConstant(
        uint8_t* out,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* lhs,
        size_t lhsSize,
        uint8_t rhs,
        size_t rhsSize)
{
    return mergeVectorImpl(
            out,
            outCapacity,
            bitmap,
            bitmapCapacity,
            lhs,
            0,
            lhsSize,
            NULL,
            rhs,
            rhsSize,
            true,
            false);
}

/**************************************************************
 * mergeFlatDepth*
 **************************************************************/

/// @returns the permute write-mask for unpacking: within each of the 8 u64
/// lanes (one per group of 8 indices) keep the low @p kDepth bytes -- the
/// packed bytes that group occupies -- and zero the rest. Matches the layout
/// UnpackPermute scatters into.
ZL_AVX512_INLINE __mmask64 unpackGroupByteMask(size_t kDepth)
{
    __mmask64 mask = 0;
    for (size_t group = 0; group < 8; ++group) {
        mask |= (__mmask64)(tailMask(kDepth) << (group * 8));
    }
    return mask;
}

/**
 * Unpacks one 64-symbol block of depth-`d` indices. Produces a __m512i with one
 * depth-bit index per byte lane.
 *
 *  1. Load the packed bytes (loadMask covers the 8*d bytes for 64 indices).
 *  2. permutexvar via @p unpackPermute regathers each group's `d` packed bytes
 *     into the low bytes of one u64 lane, so each lane holds 8*d contiguous
 *     bits == 8 indices. @p unpackMask zeroes the unused high bytes of each
 *     lane.
 *  3. multishift via @p shiftControl ({0,d,2d,...,7d}) copies the byte starting
 *     at each index's bit position into a byte lane, so lane j holds index j in
 *     its low d bits (with high bits spilled from the neighbour).
 *  4. AND with @p indexMask (low d bits) clears that spill.
 */
ZL_AVX512_INLINE __m512i unpackFlatDepth(
        const uint8_t* bitmap,
        __mmask64 loadMask,
        __m512i unpackPermute,
        __mmask64 unpackMask,
        __m512i shiftControl,
        __m512i indexMask)
{
    const __m512i packed = _mm512_maskz_loadu_epi8(loadMask, bitmap);
    // Group each packed byte run into one u64 lane for multishift unpacking
    const __m512i groups =
            _mm512_maskz_permutexvar_epi8(unpackMask, unpackPermute, packed);
    // Extract one depth-bit index into each byte lane
    return _mm512_and_si512(
            _mm512_multishift_epi64_epi8(shiftControl, groups), indexMask);
}

/**
 * Builds the symbol lookup table(s) for depths 1..7, laid out for the shuffle
 * primitive each tier uses (see shuffleSymbolsDepth):
 *  - depth <= 4: the 2^depth symbols (zero-padded to 16) replicated into all
 *    four 128-bit lanes, because `_mm512_shuffle_epi8` looks up within each
 *    lane.
 *  - depth 5..6: the 2^depth symbols in one 64-byte table for `permutexvar`.
 *  - depth 7: 128 symbols split across two 64-byte pages (lut0 = 0..63,
 *    lut1 = 64..127), selected later by index bit 6.
 */
ZL_AVX512_INLINE void buildShuffleTables(
        uint8_t* lut0,
        uint8_t* lut1,
        size_t kDepth,
        const uint8_t* symbols)
{
    for (size_t i = 0; i < 64; ++i) {
        lut0[i] = 0;
        lut1[i] = 0;
    }

    const size_t numSymbols = (size_t)1 << kDepth;
    if (kDepth <= 4) {
        for (size_t lane = 0; lane < 4; ++lane) {
            for (size_t idx = 0; idx < 16; ++idx) {
                lut0[lane * 16 + idx] = idx < numSymbols ? symbols[idx] : 0;
            }
        }
    } else if (kDepth <= 6) {
        for (size_t idx = 0; idx < numSymbols; ++idx) {
            lut0[idx] = symbols[idx];
        }
    } else {
        for (size_t idx = 0; idx < 64; ++idx) {
            lut0[idx] = symbols[idx];
            lut1[idx] = symbols[idx + 64];
        }
    }
}

/// Maps 64 depth-7 indices (0..127) to symbols. permutexvar handles a 64-entry
/// page, so look up both pages with the low 6 bits and pick per lane by bit 6.
ZL_AVX512_INLINE __m512i
shuffleSymbols7(__m512i indices, __m512i lut0, __m512i lut1)
{
    // Depth 7 needs two 64-byte LUT pages: bit 6 selects the page.
    const __m512i localIndexMask = _mm512_set1_epi8(63);
    const __m512i pageBit        = _mm512_set1_epi8(64);
    const __m512i localIndices   = _mm512_and_si512(indices, localIndexMask);
    const __m512i lowSymbols     = _mm512_permutexvar_epi8(localIndices, lut0);
    const __m512i highSymbols    = _mm512_permutexvar_epi8(localIndices, lut1);
    const __mmask64 highPageMask = _mm512_test_epi8_mask(indices, pageBit);
    return _mm512_mask_mov_epi8(lowSymbols, highPageMask, highSymbols);
}

/// Maps per-byte indices to symbols for depths 1..7, picking the right shuffle
/// primitive for the table layout buildShuffleTables produced:
///  - depth <= 4: in-lane `shuffle_epi8` (the table is replicated per 128
///  bits).
///  - depth 5..6: full-width `permutexvar` over a single 64-byte page.
///  - depth 7: two-page lookup (shuffleSymbols7). Depth 8 is handled
///  separately.
ZL_AVX512_INLINE __m512i shuffleSymbolsDepth(
        const size_t kDepth,
        __m512i indices,
        __m512i lut0,
        __m512i lut1)
{
    if (kDepth <= 4) {
        return _mm512_shuffle_epi8(lut0, indices);
    }
    if (kDepth <= 6) {
        return _mm512_permutexvar_epi8(indices, lut0);
    }
    assert(kDepth == 7);
    return shuffleSymbols7(indices, lut0, lut1);
}

/**
 * Expands a flat leaf at depth 2..7: reads @p outSize packed @p kDepth-bit
 * indices from @p bitmap and writes `symbols[index]` for each. Per 64-symbol
 * block: unpackFlatDepth recovers the indices, shuffleSymbolsDepth maps them to
 * symbols. Loop-invariant control vectors (the generated permute/shift tables,
 * the unpack mask, and the index mask) and the symbol LUTs are built once up
 * front. The tail handles the final < 64 symbols with masked load/store.
 */
ZL_AVX512_INLINE void mergeFlatDepthImpl(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* symbols,
        const size_t kDepth)
{
    size_t outIdx               = 0;
    const __m512i unpackPermute = _mm512_load_si512(
            (const void*)ZL_kPivCoHuffmanAvx512UnpackPermute[kDepth - 2]);
    const __m512i shiftControl = _mm512_load_si512(
            (const void*)ZL_kPivCoHuffmanAvx512ShiftControl[kDepth - 2]);
    const __mmask64 unpackMask   = unpackGroupByteMask(kDepth);
    const size_t inputBytes      = 8 * kDepth;
    const __mmask64 fullLoadMask = tailMask(inputBytes);
    const __m512i indexMask = _mm512_set1_epi8((char)((1u << kDepth) - 1u));

    ZL_ALIGNED(64) uint8_t lut0Bytes[64];
    ZL_ALIGNED(64) uint8_t lut1Bytes[64];
    buildShuffleTables(lut0Bytes, lut1Bytes, kDepth, symbols);
    const __m512i lut0 = _mm512_load_si512((const void*)lut0Bytes);
    const __m512i lut1 = _mm512_load_si512((const void*)lut1Bytes);

    (void)bitmapCapacity;
    for (; outIdx + 64 <= outSize; outIdx += 64) {
        const size_t byteOffset = (outIdx / 8) * kDepth;
        // Load only the bytes needed for this 64-symbol block
        const __m512i indices = unpackFlatDepth(
                bitmap + byteOffset,
                fullLoadMask,
                unpackPermute,
                unpackMask,
                shiftControl,
                indexMask);
        _mm512_storeu_si512(
                out + outIdx, shuffleSymbolsDepth(kDepth, indices, lut0, lut1));
    }

    if (outIdx < outSize) {
        const size_t remaining  = outSize - outIdx;
        const size_t byteOffset = (outIdx / 8) * kDepth;
        const size_t tailBytes  = (remaining * kDepth + 7) / 8;
        // Mask both the packed input bytes and output symbols
        const __m512i indices = unpackFlatDepth(
                bitmap + byteOffset,
                tailMask(tailBytes),
                unpackPermute,
                unpackMask,
                shiftControl,
                indexMask);
        _mm512_mask_storeu_epi8(
                out + outIdx,
                tailMask(remaining),
                shuffleSymbolsDepth(kDepth, indices, lut0, lut1));
    }
}

// Generates mergeFlatDepth2..7, each pinning the depth so the unpack shifts and
// table indexing fold to constants.
#    define DEFINE_MERGE_FLAT_DEPTH(DEPTH)                                 \
        static ZL_AVX512_ATTR void mergeFlatDepth##DEPTH(                  \
                uint8_t* out,                                              \
                size_t outSize,                                            \
                const uint8_t* bitmap,                                     \
                size_t bitmapCapacity,                                     \
                const uint8_t* symbols)                                    \
        {                                                                  \
            mergeFlatDepthImpl(                                            \
                    out, outSize, bitmap, bitmapCapacity, symbols, DEPTH); \
        }

DEFINE_MERGE_FLAT_DEPTH(2)
DEFINE_MERGE_FLAT_DEPTH(3)
DEFINE_MERGE_FLAT_DEPTH(4)
DEFINE_MERGE_FLAT_DEPTH(5)
DEFINE_MERGE_FLAT_DEPTH(6)
DEFINE_MERGE_FLAT_DEPTH(7)

/// Expands a depth-1 flat leaf: one bit per symbol selecting symbols[0] (0) or
/// symbols[1] (1). Each 64-bit packed word becomes a 64-lane blend, so no
/// unpacking or table is needed.
static ZL_AVX512_ATTR void mergeFlatDepth1(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* symbols)
{
    (void)bitmapCapacity;

    size_t outIdx     = 0;
    const __m512i lhs = _mm512_set1_epi8((char)symbols[0]);
    const __m512i rhs = _mm512_set1_epi8((char)symbols[1]);
    for (; outIdx + 64 <= outSize; outIdx += 64) {
        const __mmask64 mask = (__mmask64)ZL_readLE64(bitmap + outIdx / 8);
        _mm512_storeu_si512(out + outIdx, _mm512_mask_mov_epi8(lhs, mask, rhs));
    }
    if (outIdx < outSize) {
        const size_t remaining  = outSize - outIdx;
        const size_t byteOffset = outIdx / 8;
        const size_t tailBytes  = (remaining + 7) / 8;
        const __mmask64 valid   = tailMask(remaining);
        const __m128i packed    = _mm_maskz_loadu_epi8(
                (__mmask16)tailMask(tailBytes), bitmap + byteOffset);
        // Move the packed tail bytes into a mask and ignore invalid lanes.
        const __mmask64 mask = (__mmask64)_mm_cvtsi128_si64(packed) & valid;
        _mm512_mask_storeu_epi8(
                out + outIdx, valid, _mm512_mask_mov_epi8(lhs, mask, rhs));
    }
}

static ZL_AVX512_ATTR void mergeFlatDepth8(
        uint8_t* out,
        size_t outSize,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        const uint8_t* symbols)
{
    // Just auto-vectorize
    assert(bitmapCapacity >= outSize);
    (void)bitmapCapacity;
    for (size_t i = 0; i < outSize; ++i) {
        out[i] = symbols[bitmap[i]];
    }
}

/// Entry point for flat-leaf expansion: dispatches to the depth-specialized
/// unpacker (depths 1..8). @see ZL_PivCoHuffmanDecode::mergeFlatDepth.
static ZL_AVX512_ATTR void mergeFlatDepth(
        uint8_t* out,
        size_t outSize,
        size_t outCapacity,
        const uint8_t* bitmap,
        size_t bitmapCapacity,
        size_t depth,
        const uint8_t* symbols)
{
    assert(outCapacity >= outSize);
    assert(depth >= 1 && depth <= 8);
    assert(bitmapCapacity >= (outSize * depth + 7) / 8);
    (void)outCapacity;

    // The contract guarantees the exact packed bytes are readable. Extra
    // bitmap capacity only determines when 64-byte overreads are allowed.
    switch (depth) {
        case 1:
            mergeFlatDepth1(out, outSize, bitmap, bitmapCapacity, symbols);
            return;
        case 2:
            mergeFlatDepth2(out, outSize, bitmap, bitmapCapacity, symbols);
            return;
        case 3:
            mergeFlatDepth3(out, outSize, bitmap, bitmapCapacity, symbols);
            return;
        case 4:
            mergeFlatDepth4(out, outSize, bitmap, bitmapCapacity, symbols);
            return;
        case 5:
            mergeFlatDepth5(out, outSize, bitmap, bitmapCapacity, symbols);
            return;
        case 6:
            mergeFlatDepth6(out, outSize, bitmap, bitmapCapacity, symbols);
            return;
        case 7:
            mergeFlatDepth7(out, outSize, bitmap, bitmapCapacity, symbols);
            return;
        default:
            mergeFlatDepth8(out, outSize, bitmap, bitmapCapacity, symbols);
            return;
    }
}

const ZL_PivCoHuffmanDecode ZL_PivCoHuffmanDecode_avx512 = {
    .supported           = supported,
    .mergeVectorVector   = mergeVectorVector,
    .mergeConstantVector = mergeConstantVector,
    .mergeVectorConstant = mergeVectorConstant,
    .mergeFlatDepth      = mergeFlatDepth,
};
#else

/// Non-x86-64 build: the AVX-512 kernels don't exist, so report unsupported and
/// leave the rest of the function table NULL.
static bool supported(const ZL_cpuid_t* cpuid)
{
    (void)cpuid;
    return false;
}

const ZL_PivCoHuffmanDecode ZL_PivCoHuffmanDecode_avx512 = {
    .supported = supported,
};
#endif
