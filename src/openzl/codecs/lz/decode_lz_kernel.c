// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/lz/decode_lz_kernel.h"

#include <string.h>

#include "openzl/codecs/common/copy.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/utils.h"

enum {
    // The loop is not currently unrolled, but leave kUnroll in place so it can
    // be easily unrolled in the future, and make sure all logic takes kUnroll
    // into account.
    ZL_Lz_kUnroll = 1,

    // Each sequences always copies 32-bytes of literals and match, and copies
    // in chunks of 32-bytes. So the loop needs to ensure that it is always safe
    // to copy up to roundUp(litLength, 32) and roundUp(matchLength, 32) bytes.
    ZL_Lz_kLitCopyLen   = 32,
    ZL_Lz_kMatchCopyLen = 32,

    // The amount of remaining literals the fast decoding loop needs to execute
    // one
    // iteration.
    ZL_Lz_kLitSlop = ZL_Lz_kUnroll * ZL_Lz_kLitCopyLen,

    // The amount of remaining output space the fast decoding loop needs to
    // execute one iteration.
    ZL_Lz_kOutSlop = ZL_Lz_kLitSlop + (ZL_Lz_kUnroll * ZL_Lz_kMatchCopyLen)
};

/**
 * Logically:
 *
 * ```
 * char tmp[kLength];
 * memcpy(tmp, src, kLength);
 * memcpy(dst, tmp, kLength);
 * ```
 *
 * Unlike `memcpy()` @p src and @p dst are allowed to overlap, and we guarantee
 * that the copy happens as-if they were not overlapped.
 */
ZL_FORCE_INLINE void
copyNonOverlapping(uint8_t* dst, const uint8_t* src, size_t kLength)
{
#if ZL_HAS_SSSE3
    if (kLength == 16) {
        const __m128i v = _mm_lddqu_si128((const __m128i_u*)src);
        _mm_storeu_si128((__m128i_u*)dst, v);
        return;
    }
#endif

#if ZL_HAS_AVX2
    if (kLength == 32) {
        const __m256i v = _mm256_lddqu_si256((const __m256i_u*)src);
        _mm256_storeu_si256((__m256i_u*)dst, v);
        return;
    } else if (kLength == 64) {
        const __m256i v0 = _mm256_lddqu_si256((const __m256i_u*)src);
        const __m256i v1 = _mm256_lddqu_si256((const __m256i_u*)(src + 32));
        _mm256_storeu_si256((__m256i_u*)dst, v0);
        _mm256_storeu_si256((__m256i_u*)(dst + 32), v1);
        return;
    }
#endif

    assert(kLength <= 64);
    char tmp[64];
    memcpy(tmp, src, kLength);
    memcpy(dst, tmp, kLength);
}

/**
 * Helper macro to copy @p kCopyLength bytes and sink a bounds check @p
 * shouldExit to only run if @p length > @p kCopyLength. And finally
 * copy @p length bytes @p kCopyLength bytes at a time. It uses the
 * @p copy function or function-like-macro to do the copy.
 *
 * Sinking the bounds check behind the unlikely long length check avoids
 * the bounds check in the majority of cases.
 */
#define ZL_LZ_COPY_LOOP(copy, kCopyLength, length, shouldExit) \
    do {                                                       \
        copy(0, kCopyLength);                                  \
        if (ZL_UNLIKELY(length > kCopyLength)) {               \
            if (ZL_UNLIKELY(shouldExit)) {                     \
                goto _exit;                                    \
            }                                                  \
            ptrdiff_t copied = kCopyLength;                    \
            do {                                               \
                copy(copied, kCopyLength);                     \
                copied += kCopyLength;                         \
            } while (copied < length);                         \
        }                                                      \
    } while (0)

#define ZL_LZ_COPY_LITERALS_IMPL(copied, kCopyLength) \
    copyNonOverlapping(                               \
            out + outPos + copied, lits + litPos + copied, kCopyLength)

/**
 * Copies @p length literals from `lit + litPos` to `out + outPos`
 * `ZL_Lz_kLitCopyLen` bytes at a time. Uses the bounds check @p shouldExit
 * for lengths longer than `ZL_Lz_kLitCopyLen` to `goto _exit` when it is true.
 */
#define ZL_LZ_COPY_LITERALS(length, shouldExit) \
    ZL_LZ_COPY_LOOP(                            \
            ZL_LZ_COPY_LITERALS_IMPL, ZL_Lz_kLitCopyLen, length, shouldExit)

#define ZL_LZ_COPY_MATCH_NON_OVERLAPPING_IMPL(copied, kCopyLength) \
    copyNonOverlapping(                                            \
            out + outMatch + copied, out + match + copied, kCopyLength)

/**
 * Copies @p length bytes from `out + match` to `out + outMatch` @p kCopyLength
 * bytes at a time. Uses the bounds check @p shouldExit for lengths longer than
 * @p kCopyLen to `goto _exit` when it is true.
 *
 * @pre `offset >= kCopyLength || offset >= length`
 */
#define ZL_LZ_COPY_MATCH_NON_OVERLAPPING(length, kCopyLength, shouldExit) \
    assert(offset >= kCopyLength || offset >= length);                    \
    ZL_LZ_COPY_LOOP(                                                      \
            ZL_LZ_COPY_MATCH_NON_OVERLAPPING_IMPL,                        \
            kCopyLength,                                                  \
            length,                                                       \
            shouldExit)

#if ZL_HAS_AVX2
// clang-format off
// LUT for generating an initial 16-byte pattern from the first `offset` bytes.
// Entry [offset][pos] = pos % offset, used as a shuffle mask for _mm_shuffle_epi8.
// Entry [0] is unused (offset=0 is invalid) and zeroed out via 0x80.
static const uint8_t ZL_ALIGNED(16)
kPatternGeneration[17][16] = {
    {0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80}, // offset 0 (unused)
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},       // offset 1
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},       // offset 2
    {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0},       // offset 3
    {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},       // offset 4
    {0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0},       // offset 5
    {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3},       // offset 6
    {0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1},       // offset 7
    {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7},       // offset 8
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 6},       // offset 9
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5},       // offset 10
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10, 0, 1, 2, 3, 4},       // offset 11
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11, 0, 1, 2, 3},       // offset 12
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12, 0, 1, 2},       // offset 13
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13, 0, 1},       // offset 14
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14, 0},       // offset 15
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},       // offset 16
};

// LUT for reshuffling a pattern to advance by 16 positions.
// Entry [offset][pos] = (16 + pos) % offset, used to rotate the pattern
// for the next 16-byte store when offset is not a power of 2.
static const uint8_t ZL_ALIGNED(16)
kPatternReshuffle[17][16] = {
    {0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80}, // offset 0 (unused)
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},       // offset 1
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},       // offset 2
    {1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1},       // offset 3
    {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},       // offset 4
    {1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1},       // offset 5
    {4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1},       // offset 6
    {2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3},       // offset 7
    {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7},       // offset 8
    {7, 8, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4},       // offset 9
    {6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1},       // offset 10
    {5, 6, 7, 8, 9,10, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9},       // offset 11
    {4, 5, 6, 7, 8, 9,10,11, 0, 1, 2, 3, 4, 5, 6, 7},       // offset 12
    {3, 4, 5, 6, 7, 8, 9,10,11,12, 0, 1, 2, 3, 4, 5},       // offset 13
    {2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13, 0, 1, 2, 3},       // offset 14
    {1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14, 0, 1},       // offset 15
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},       // offset 16
};
// clang-format on

/// Loads the pattern for copying 16 bytes from @p src when the offset is @p
/// offset, accounting for overlapping matches.
/// @pre offset <= 16
/// @note Defines offset == 0 to memset(0)
ZL_FORCE_INLINE __m128i loadPattern(const uint8_t* src, ptrdiff_t offset)
{
    assert(offset >= 0 && offset <= 16);
    __m128i generation =
            _mm_load_si128((const __m128i*)kPatternGeneration[offset]);
    __m128i data = _mm_lddqu_si128((const __m128i_u*)src);
    return _mm_shuffle_epi8(data, generation);
}

/// Loads the reshuffle mask for the pattern. This is used to rotate the pattern
/// after each 16-byte copy. For example, if the offset is 5, then the original
/// pattern is:
///
/// 0123401234012340
///
/// And after copying 16 bytes it needs to be reshuffled to start with 1
///
/// 1234012340123401
ZL_FORCE_INLINE __m128i loadReshuffle(ptrdiff_t offset)
{
    return _mm_load_si128((const __m128i*)kPatternReshuffle[offset]);
}

/// Store @p pattern to @p dst
ZL_FORCE_INLINE void copyPattern32(uint8_t* dst, __m256i pattern)
{
    _mm256_storeu_si256((__m256i_u*)dst, pattern);
}

/// Store @p pattern to @p dst twice
ZL_FORCE_INLINE void copyPattern2x16(uint8_t* dst, __m128i pattern)
{
    _mm_storeu_si128((__m128i_u*)dst, pattern);
    _mm_storeu_si128((__m128i_u*)(dst + 16), pattern);
}

/// Store @p pattern to @p dst twice and uses @p reshuffle to shuffle the mask
/// after each store.
/// @returns The shuffled pattern
ZL_FORCE_INLINE __m128i
copyPatternWithReshuffle2x16(uint8_t* dst, __m128i pattern, __m128i reshuffle)
{
    _mm_storeu_si128((__m128i_u*)dst, pattern);
    pattern = _mm_shuffle_epi8(pattern, reshuffle);
    _mm_storeu_si128((__m128i_u*)(dst + 16), pattern);
    pattern = _mm_shuffle_epi8(pattern, reshuffle);
    return pattern;
}

#    define ZL_LZ_COPY_MATCH_OVERLAPPING_OFFSET_1_IMPL(copied, kCopyLength) \
        copyPattern32(out + outMatch + copied, pattern)

/**
 * Copies @p length bytes from `out + match` to `out + outMatch` @p kCopyLength
 * bytes at a time. Uses the bounds check @p shouldExit for lengths longer than
 * @p kCopyLen to `goto _exit` when it is true.
 *
 * @pre `offset == 1`
 */
#    define ZL_LZ_COPY_MATCH_OVERLAPPING_OFFSET_1(length, shouldExit)   \
        do {                                                            \
            assert(offset == 1);                                        \
            assert(ZL_Lz_kMatchCopyLen >= 32);                          \
            const __m256i pattern = _mm256_set1_epi8((char)out[match]); \
            ZL_LZ_COPY_LOOP(                                            \
                    ZL_LZ_COPY_MATCH_OVERLAPPING_OFFSET_1_IMPL,         \
                    32,                                                 \
                    length,                                             \
                    shouldExit);                                        \
        } while (0)

#    define ZL_LZ_COPY_MATCH_OVERLAPPING_POW2_IMPL(copied, kCopyLength) \
        copyPattern2x16(out + outMatch + copied, pattern)

/**
 * Copies @p length bytes from `out + match` to `out + outMatch` @p kCopyLength
 * bytes at a time. Uses the bounds check @p shouldExit for lengths longer than
 * @p kCopyLen to `goto _exit` when it is true.
 *
 * @pre `ZL_isPow2(offset)`
 */
#    define ZL_LZ_COPY_MATCH_OVERLAPPING_POW2(length, shouldExit)     \
        do {                                                          \
            assert(ZL_isPow2((uint64_t)offset));                      \
            assert(ZL_Lz_kMatchCopyLen >= 32);                        \
            const __m128i pattern = loadPattern(out + match, offset); \
            ZL_LZ_COPY_LOOP(                                          \
                    ZL_LZ_COPY_MATCH_OVERLAPPING_POW2_IMPL,           \
                    32,                                               \
                    length,                                           \
                    shouldExit);                                      \
        } while (0)

#    define ZL_LZ_COPY_MATCH_OVERLAPPING_NON_POW2_IMPL(copied, kCopyLength) \
        do {                                                                \
            pattern = copyPatternWithReshuffle2x16(                         \
                    out + outMatch + copied, pattern, reshuffle);           \
        } while (0)

/**
 * Copies @p length bytes from `out + match` to `out + outMatch` @p kCopyLength
 * bytes at a time. Uses the bounds check @p shouldExit for lengths longer than
 * @p kCopyLen to `goto _exit` when it is true.
 *
 * @pre `offset <= 16`
 */
#    define ZL_LZ_COPY_MATCH_OVERLAPPING_NON_POW2(length, shouldExit)   \
        do {                                                            \
            assert(offset <= 16);                                       \
            assert(ZL_Lz_kMatchCopyLen >= 32);                          \
            __m128i pattern         = loadPattern(out + match, offset); \
            const __m128i reshuffle = loadReshuffle(offset);            \
            ZL_LZ_COPY_LOOP(                                            \
                    ZL_LZ_COPY_MATCH_OVERLAPPING_NON_POW2_IMPL,         \
                    32,                                                 \
                    length,                                             \
                    shouldExit);                                        \
        } while (0)

/**
 * Copies @p length bytes from `out + match` to `out + outMatch` @p kCopyLength
 * bytes at a time. Uses the bounds check @p shouldExit for lengths longer than
 * @p kCopyLen to `goto _exit` when it is true.
 *
 * @note Handles the case when `offset < length`
 */
#    define ZL_LZ_COPY_MATCH_OVERLAPPING(length, shouldExit)                 \
        do {                                                                 \
            if (offset == 1) {                                               \
                ZL_LZ_COPY_MATCH_OVERLAPPING_OFFSET_1(matchLen, shouldExit); \
            } else if (                                                      \
                    offset == 2 || offset == 4 || offset == 8                \
                    || offset == 16) {                                       \
                ZL_LZ_COPY_MATCH_OVERLAPPING_POW2(matchLen, shouldExit);     \
            } else if (offset <= 16) {                                       \
                ZL_LZ_COPY_MATCH_OVERLAPPING_NON_POW2(matchLen, shouldExit); \
            } else {                                                         \
                ZL_LZ_COPY_MATCH_NON_OVERLAPPING(matchLen, 16, shouldExit);  \
            }                                                                \
        } while (0)
#else

/**
 * Copies @p length bytes from `out + match` to `out + outMatch` @p kCopyLength
 * bytes at a time. Uses the bounds check @p shouldExit for lengths longer than
 * @p kCopyLen to `goto _exit` when it is true.
 *
 * @note Handles the case when `offset < length`
 */
#    define ZL_LZ_COPY_MATCH_OVERLAPPING(length, shouldExit)       \
        do {                                                       \
            assert(ZL_Lz_kMatchCopyLen >= ZS_WILDCOPY_OVERLENGTH); \
            if (shouldExit) {                                      \
                goto _exit;                                        \
            }                                                      \
            ZS_wildcopy(                                           \
                    out + outMatch,                                \
                    out + match,                                   \
                    length,                                        \
                    ZS_wo_src_before_dst);                         \
        } while (0)
#endif

/**
 * Decodes a single LZ sequences and updates @p outPos and @p litPos
 */
static ZL_LzError ZL_Lz_decode_sequence(
        uint8_t* dst,
        size_t dstSize,
        const ZL_Lz_InSequences* src,
        ptrdiff_t seq,
        ptrdiff_t* outPos,
        ptrdiff_t* litPos)
{
    const uint8_t* const literals    = src->literals;
    ptrdiff_t const numLiterals      = (ptrdiff_t)src->numLiterals;
    const void* const offsets        = src->offsets;
    ptrdiff_t const offsetsEltWidth  = (ptrdiff_t)src->offsetsEltWidth;
    const void* const literalLengths = src->literalLengths;
    ptrdiff_t const literalLengthsEltWidth =
            (ptrdiff_t)src->literalLengthsEltWidth;
    const void* const matchLengths       = src->matchLengths;
    ptrdiff_t const matchLengthsEltWidth = (ptrdiff_t)src->matchLengthsEltWidth;

    assert((size_t)seq < src->numSequences);
    assert((size_t)*outPos <= dstSize);
    assert(*litPos <= numLiterals);

    // Must handle integer overflow for these values
    ptrdiff_t const litLen = (ptrdiff_t)ZL_readN(
            (const uint8_t*)literalLengths + seq * literalLengthsEltWidth,
            (size_t)literalLengthsEltWidth);
    ptrdiff_t const offset = (ptrdiff_t)ZL_readN(
            (const uint8_t*)offsets + seq * offsetsEltWidth,
            (size_t)offsetsEltWidth);
    ptrdiff_t const matchLen = (ptrdiff_t)ZL_readN(
            (const uint8_t*)matchLengths + seq * matchLengthsEltWidth,
            (size_t)matchLengthsEltWidth);

    // Copy literals
    if (litLen > numLiterals - *litPos) {
        return ZL_LzError_notEnoughLiterals;
    }
    if (litLen < 0 || litLen > (ptrdiff_t)dstSize - *outPos) {
        return ZL_LzError_literalLengthTooLarge;
    }
    if (litLen > 0) {
        memcpy(dst + *outPos, literals + *litPos, (size_t)litLen);
    }
    *outPos += litLen;
    *litPos += litLen;

    // Validate offset
    if (offset == 0) {
        return ZL_LzError_offsetZero;
    }
    if (offset > *outPos || offset < 0) {
        return ZL_LzError_offsetTooLarge;
    }

    // Copy match (byte-by-byte to handle overlapping matches)
    if (matchLen < 0 || matchLen > (ptrdiff_t)dstSize - *outPos) {
        return ZL_LzError_matchLengthTooLarge;
    }
    for (ptrdiff_t i = 0; i < matchLen; ++i) {
        dst[*outPos + i] = dst[*outPos + i - offset];
    }
    *outPos += matchLen;
    return ZL_LzError_ok;
}

/**
 * Decodes the remaining literals in the literals buffer after all sequences
 * have been executed.
 */
static ZL_LzError ZL_Lz_decode_lastLiterals(
        uint8_t* dst,
        size_t dstSize,
        const ZL_Lz_InSequences* src,
        ptrdiff_t outPos,
        ptrdiff_t litPos)
{
    const uint8_t* const literals = src->literals;
    ptrdiff_t const numLiterals   = (ptrdiff_t)src->numLiterals;

    if (litPos > numLiterals) {
        // This can happen when using the temporary literal buffer.
        // It is okay, however, because we are just reading zeros.
        assert(litPos - numLiterals <= ZL_Lz_kLitSlop);
        return ZL_LzError_notEnoughLiterals;
    }

    assert(outPos <= (ptrdiff_t)dstSize);

    if (litPos < numLiterals) {
        const ptrdiff_t lastLits = numLiterals - litPos;
        if (outPos + lastLits > (ptrdiff_t)dstSize) {
            return ZL_LzError_literalsTooLarge;
        }
        memcpy(dst + outPos, literals + litPos, (size_t)lastLits);
        outPos += lastLits;
    }

    if (outPos != (ptrdiff_t)dstSize) {
        return ZL_LzError_dstSizeTooLarge;
    }

    return ZL_LzError_ok;
}

/// The fallback LZ decoder that works on all numeric widths but is slow
static ZL_LzError
ZL_Lz_decode_generic(uint8_t* dst, size_t dstSize, const ZL_Lz_InSequences* src)
{
    ptrdiff_t seq    = 0;
    ptrdiff_t outPos = 0;
    ptrdiff_t litPos = 0;

    ptrdiff_t const numSequences = (ptrdiff_t)src->numSequences;
    for (; seq < numSequences; seq++) {
        const ZL_LzError err =
                ZL_Lz_decode_sequence(dst, dstSize, src, seq, &outPos, &litPos);
        if (err != ZL_LzError_ok) {
            return err;
        }
    }

    return ZL_Lz_decode_lastLiterals(dst, dstSize, src, outPos, litPos);
}

typedef struct {
    ZL_Lz_InSequences src;
    ptrdiff_t seq;
    ptrdiff_t outPos;
    ptrdiff_t litPos;
    ptrdiff_t litLimit;
} ZL_Lz_DecodeState;

ZL_FORCE_INLINE ptrdiff_t
loadOffset(const void* offs, ptrdiff_t seq, size_t offWidth)
{
    if (offWidth == sizeof(uint16_t)) {
        return ((const uint16_t*)offs)[seq];
    }
    ZL_ASSERT_EQ(offWidth, sizeof(uint32_t));
    return ((const uint32_t*)offs)[seq];
}

/**
 * The hot LZ decoding loop.
 *
 * @pre state->seq <= state->src.numSequences - ZL_Lz_kUnroll
 * @pre state->outPos <= dstSize - ZL_Lz_kOutSlop
 * @pre state->litPos <= state->litLimit
 *
 * NOTE: This is force inlined into width-specific force-outlined wrappers
 * because the loop is tight on registers. If there is a function call in the
 * loop body, then the compiler has to worry about callee saved registers, and
 * spills more variables to the stack.
 *
 * NOTE: This kernel uses ptrdiff_t rather than pointers to avoid UB with
 * pointers, which are only valid within the buffer and one past the end.
 */
ZL_FORCE_INLINE ZL_LzError ZL_Lz_decode_fastLoop(
        uint8_t* const dst,
        size_t dstSize,
        ZL_Lz_DecodeState* state,
        size_t offWidth)
{
    uint8_t* const out              = dst;
    const size_t numSeqs            = state->src.numSequences;
    const uint8_t* const lits       = state->src.literals;
    const void* const offs          = state->src.offsets;
    const uint16_t* const litLens   = state->src.literalLengths;
    const uint16_t* const matchLens = state->src.matchLengths;

    ptrdiff_t outPos         = state->outPos;
    const ptrdiff_t outLimit = (ptrdiff_t)dstSize - ZL_Lz_kOutSlop;

    ptrdiff_t litPos         = state->litPos;
    const ptrdiff_t litLimit = state->litLimit;

    ptrdiff_t seq            = state->seq;
    const ptrdiff_t seqLimit = (ptrdiff_t)numSeqs - ZL_Lz_kUnroll;

    do {
        assert(seq <= seqLimit);
        assert(litPos <= litLimit);
        assert(outPos <= outLimit);
#ifdef __clang__
#    pragma clang loop unroll(full)
#endif
        for (size_t u = 0; u < ZL_Lz_kUnroll; ++u) {
            assert((size_t)seq < numSeqs);
            assert((size_t)litPos <= state->src.numLiterals);
            assert((size_t)outPos <= dstSize);

            // These values are capped at 64K
            // We validated the source size is <= PTRDIFF_MAX - 2 * UINT16_MAX
            // before calling this loop to guarantee no overflow when adding
            // to outPos.
            const ptrdiff_t litLen   = litLens[seq];
            const ptrdiff_t matchLen = matchLens[seq];
            const ptrdiff_t offset   = loadOffset(offs, seq, offWidth);
            assert(sizeof(ptrdiff_t) > offWidth);
            assert(litLen >= 0 && matchLen >= 0 && offset >= 0);

            const ptrdiff_t outMatch = outPos + litLen;
            const ptrdiff_t outNext  = outMatch + matchLen;
            const ptrdiff_t litNext  = litPos + litLen;

            // Copies literals and breaks if the literals go beyond litLimit or
            // the sequences goes beyond outLimit. Check outNext rather than
            // outMatch so that if the literals are long, we don't need to
            // re-copy the literals if the match will exit.
            //
            // The bounds check only needs to be executed when
            // litLen > ZL_Lz_kLitCopyLen because we guarantee there is enough
            // space to copy short literals.
            ZL_LZ_COPY_LITERALS(
                    litLen, (litNext > litLimit || outNext > outLimit));

            const ptrdiff_t match = outMatch - offset;
            if (ZL_UNLIKELY(match < 0)) {
                return ZL_LzError_offsetTooLarge;
            }

            // Copies the match using different strategies for the common case
            // where the match doesn't overlap, and for when the match does
            // overlap.
            //
            // The bounds check only needs to be executed when
            // matchLen > ZL_Lz_kMatchCopyLen because we guarantee there is
            // enough space to copy short matches.
            if (ZL_UNLIKELY(offset < matchLen)) {
                ZL_LZ_COPY_MATCH_OVERLAPPING(matchLen, (outNext > outLimit));
            } else {
                ZL_LZ_COPY_MATCH_NON_OVERLAPPING(
                        matchLen, ZL_Lz_kMatchCopyLen, (outNext > outLimit));
            }

            // Only update the state at the end of the loop because either the
            // literal or match copy may reach a limit, and we will need to
            // reprocess the current sequence.
            outPos = outNext;
            litPos = litNext;
            ++seq;
        }
    } while (seq <= seqLimit && outPos <= outLimit && litPos <= litLimit);

_exit:
    state->seq    = seq;
    state->outPos = outPos;
    state->litPos = litPos;

    return ZL_LzError_ok;
}

ZL_FORCE_NOINLINE ZL_LzError ZL_Lz_decode_u16Loop(
        uint8_t* const dst,
        size_t dstSize,
        ZL_Lz_DecodeState* state)
{
    return ZL_Lz_decode_fastLoop(dst, dstSize, state, sizeof(uint16_t));
}

ZL_FORCE_NOINLINE ZL_LzError ZL_Lz_decode_u32Loop(
        uint8_t* const dst,
        size_t dstSize,
        ZL_Lz_DecodeState* state)
{
    return ZL_Lz_decode_fastLoop(dst, dstSize, state, sizeof(uint32_t));
}

typedef ZL_LzError (
        *ZL_Lz_LoopFn)(uint8_t* dst, size_t dstSize, ZL_Lz_DecodeState* state);

static ZL_LzError ZL_Lz_decode_typed(
        uint8_t* const dst,
        size_t dstSize,
        const ZL_Lz_InSequences* src,
        ZL_Lz_LoopFn loopFn)
{
    ZL_Lz_DecodeState state = {
        .src      = *src,
        .seq      = 0,
        .outPos   = 0,
        .litPos   = 0,
        .litLimit = (ptrdiff_t)state.src.numLiterals - ZL_Lz_kLitSlop,
    };

    const ptrdiff_t numSeqs  = (ptrdiff_t)state.src.numSequences;
    const ptrdiff_t outLimit = (ptrdiff_t)dstSize - ZL_Lz_kOutSlop;
    const ptrdiff_t seqLimit =
            (ptrdiff_t)state.src.numSequences - ZL_Lz_kUnroll;

    uint8_t litBuffer[2 * ZL_Lz_kLitSlop] = { 0 };

    for (; state.seq < numSeqs;) {
        // Transfer the literals to a temporary buffer that is guaranteed to
        // have enough slop space when close to the end. This avoids pessimizing
        // the edge case where there are many sequences remaining but very few
        // literals remaining.
        if (state.litPos > state.litLimit) {
            if (state.src.literals != src->literals) {
                // We've already transferred the literals
                assert(state.litPos <= (ptrdiff_t)sizeof(litBuffer));
                // Implies not enough literals
                return ZL_LzError_notEnoughLiterals;
            }
            assert(state.litPos <= (ptrdiff_t)state.src.numLiterals);
            const size_t remainingLiterals =
                    state.src.numLiterals - (size_t)state.litPos;
            assert(remainingLiterals <= ZL_Lz_kLitSlop);
            if (remainingLiterals > 0) {
                memcpy(litBuffer,
                       state.src.literals + state.litPos,
                       remainingLiterals);
            }
            state.src.literals    = litBuffer;
            state.src.numLiterals = remainingLiterals;
            state.litPos          = 0;
            state.litLimit        = (ptrdiff_t)remainingLiterals;
        }
        if (state.outPos <= outLimit && state.litPos <= state.litLimit
            && state.seq <= seqLimit) {
            const ZL_LzError err = loopFn(dst, dstSize, &state);
            if (err != ZL_LzError_ok) {
                return err;
            }
            if (state.litPos > (ptrdiff_t)state.src.numLiterals) {
                // This can happen when using the temporary literal buffer.
                // But still guaranteed not to overflow the buffer.
                assert(state.src.literals == litBuffer);
                assert(state.litPos <= (ptrdiff_t)sizeof(litBuffer));
                return ZL_LzError_notEnoughLiterals;
            }
        }

        if (state.seq < numSeqs) {
            // Execute a single sequence. The loop function may be exactly one
            // sequence short of hitting a limit, so we need to execute one
            // before transferring the literals (if that was the limiting
            // factor), otherwise we will infinite loop.
            //
            // Then, after transferring literals, this code also handles the
            // trailing sequences.
            const ZL_LzError err = ZL_Lz_decode_sequence(
                    dst,
                    dstSize,
                    &state.src,
                    state.seq,
                    &state.outPos,
                    &state.litPos);
            if (err != ZL_LzError_ok) {
                return err;
            }
            ++state.seq;
        }
    }

    return ZL_Lz_decode_lastLiterals(
            dst, dstSize, &state.src, state.outPos, state.litPos);
}

ZL_LzError
ZL_Lz_decode(uint8_t* dst, size_t dstSize, const ZL_Lz_InSequences* src)
{
    const size_t maxSeqLenU16 = 2 * UINT16_MAX;
    // Optimize for cases that the encoder actually produces
    if (src->offsetsEltWidth == 2 && src->literalLengthsEltWidth == 2
        && src->matchLengthsEltWidth == 2
        && dstSize <= PTRDIFF_MAX - maxSeqLenU16 /* no overflow in outPos */) {
        return ZL_Lz_decode_typed(dst, dstSize, src, ZL_Lz_decode_u16Loop);
    }
    if (src->offsetsEltWidth == 4 && src->literalLengthsEltWidth == 2
        && src->matchLengthsEltWidth == 2
        && dstSize <= PTRDIFF_MAX - maxSeqLenU16 /* no overflow in outPos */
        && sizeof(ptrdiff_t) == 8 /* offset must be non-negative*/) {
        return ZL_Lz_decode_typed(dst, dstSize, src, ZL_Lz_decode_u32Loop);
    }

    // Generic fallback to handle any width to allow us to update the encoder
    // without guaranteeing 100% of decoders are updated. If the encoder is
    // updated, a new specialization should be added, but then we only need to
    // wait for most decoders to be updated, rather than 100% of decoders.
    return ZL_Lz_decode_generic(dst, dstSize, src);
}
