// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

extern "C" {
#include "openzl/codecs/lz/decode_lz_kernel.h"
}

namespace {

ZL_Lz_InSequences makeSequences(
        const uint8_t* literals,
        size_t numLiterals,
        const void* offsets,
        const void* literalLengths,
        const void* matchLengths,
        size_t numSequences,
        size_t eltWidth)
{
    return {
        .literals               = literals,
        .numLiterals            = numLiterals,
        .offsets                = offsets,
        .offsetsEltWidth        = eltWidth,
        .literalLengths         = literalLengths,
        .literalLengthsEltWidth = eltWidth,
        .matchLengths           = matchLengths,
        .matchLengthsEltWidth   = eltWidth,
        .numSequences           = numSequences,
    };
}

TEST(LzDecodeTest, BasicValid)
{
    // "ABCDBC" encoded as: litLen=4 ("ABCD"), offset=3, matchLen=2 ("BC")
    uint8_t literals[]   = { 'A', 'B', 'C', 'D' };
    uint64_t litLens[]   = { 4 };
    uint64_t offsets[]   = { 3 };
    uint64_t matchLens[] = { 2 };

    auto src = makeSequences(
            literals, 4, offsets, litLens, matchLens, 1, sizeof(uint64_t));

    uint8_t dst[6] = {};
    EXPECT_EQ(ZL_Lz_decode(dst, 6, &src), ZL_LzError_ok);
    EXPECT_EQ(memcmp(dst, "ABCDBC", 6), 0);
}

TEST(LzDecodeOverflowTest, NegativeLiteralLength)
{
    // 8-byte litLen with high bit set → negative ptrdiff_t.
    // Passes the notEnoughLiterals check (negative < 0 ≤ numLiterals),
    // caught by the litLen < 0 check added in the fix.
    uint64_t litLens[]   = { static_cast<uint64_t>(PTRDIFF_MIN) };
    uint64_t offsets[]   = { 1 };
    uint64_t matchLens[] = { 0 };

    auto src = makeSequences(
            nullptr, 0, offsets, litLens, matchLens, 1, sizeof(uint64_t));

    uint8_t dst[16] = {};
    EXPECT_EQ(
            ZL_Lz_decode(dst, sizeof(dst), &src),
            ZL_LzError_literalLengthTooLarge);
}

TEST(LzDecodeOverflowTest, NegativeOffset)
{
    // 8-byte offset with high bit set → negative ptrdiff_t.
    // litLen=4 to advance outPos so the offset validity window is non-empty.
    // Old code only checked offset > outPos (negative < 4 → passes).
    // New code also checks offset < 0.
    uint8_t literals[4]  = {};
    uint64_t litLens[]   = { 4 };
    uint64_t offsets[]   = { static_cast<uint64_t>(PTRDIFF_MIN) };
    uint64_t matchLens[] = { 1 };

    auto src = makeSequences(
            literals, 4, offsets, litLens, matchLens, 1, sizeof(uint64_t));

    uint8_t dst[16] = {};
    EXPECT_EQ(ZL_Lz_decode(dst, sizeof(dst), &src), ZL_LzError_offsetTooLarge);
}

TEST(LzDecodeOverflowTest, NegativeMatchLength)
{
    // 8-byte matchLen with high bit set → negative ptrdiff_t.
    // litLen=4 to produce output for a valid offset.
    // Old code: outPos + matchLen = 4 + PTRDIFF_MIN = wraps, < dstSize →
    // passes. New code: matchLen < 0 → caught.
    uint8_t literals[4]  = {};
    uint64_t litLens[]   = { 4 };
    uint64_t offsets[]   = { 1 };
    uint64_t matchLens[] = { static_cast<uint64_t>(PTRDIFF_MIN) };

    auto src = makeSequences(
            literals, 4, offsets, litLens, matchLens, 1, sizeof(uint64_t));

    uint8_t dst[16] = {};
    EXPECT_EQ(
            ZL_Lz_decode(dst, sizeof(dst), &src),
            ZL_LzError_matchLengthTooLarge);
}

TEST(LzDecodeOverflowTest, LargeLitLenOverflowsLitPosAddition)
{
    // Two sequences: seq 0 advances litPos to 4, seq 1 has litLen =
    // PTRDIFF_MAX. Old code: litPos + litLen = 4 + PTRDIFF_MAX → signed
    // overflow (UB),
    //   wraps negative → negative > numLiterals → false → not caught.
    // New code: litLen > numLiterals - litPos → PTRDIFF_MAX > 4 → caught.
    uint8_t literals[8]  = {};
    uint64_t litLens[]   = { 4, static_cast<uint64_t>(PTRDIFF_MAX) };
    uint64_t offsets[]   = { 1, 1 };
    uint64_t matchLens[] = { 1, 0 };

    auto src = makeSequences(
            literals, 8, offsets, litLens, matchLens, 2, sizeof(uint64_t));

    uint8_t dst[256] = {};
    EXPECT_EQ(
            ZL_Lz_decode(dst, sizeof(dst), &src), ZL_LzError_notEnoughLiterals);
}

TEST(LzDecodeOverflowTest, LargeMatchLenOverflowsOutPosAddition)
{
    // litLen=4 advances outPos to 4, then matchLen = PTRDIFF_MAX.
    // Old code: outPos + matchLen = 4 + PTRDIFF_MAX → signed overflow (UB),
    //   wraps negative → negative > dstSize → false → not caught.
    // New code: matchLen > dstSize - outPos → PTRDIFF_MAX > 252 → caught.
    uint8_t literals[4]  = {};
    uint64_t litLens[]   = { 4 };
    uint64_t offsets[]   = { 1 };
    uint64_t matchLens[] = { static_cast<uint64_t>(PTRDIFF_MAX) };

    auto src = makeSequences(
            literals, 4, offsets, litLens, matchLens, 1, sizeof(uint64_t));

    uint8_t dst[256] = {};
    EXPECT_EQ(
            ZL_Lz_decode(dst, sizeof(dst), &src),
            ZL_LzError_matchLengthTooLarge);
}

TEST(LzDecodeOverflowTest, LargeLitLenOverflowsOutPosAddition)
{
    // Two sequences: seq 0 advances outPos to 5, seq 1 has litLen that
    // overflows outPos + litLen. numLiterals = PTRDIFF_MAX so the
    // notEnoughLiterals check passes (litLen == numLiterals - litPos).
    // Old code: outPos + litLen = 5 + (PTRDIFF_MAX-4) → signed overflow (UB),
    //   wraps negative → negative > dstSize → false → not caught.
    // New code: litLen > dstSize - outPos → (PTRDIFF_MAX-4) > 251 → caught.
    uint8_t literals[4]  = {};
    uint64_t litLens[]   = { 4, static_cast<uint64_t>(PTRDIFF_MAX) - 4 };
    uint64_t offsets[]   = { 1, 1 };
    uint64_t matchLens[] = { 1, 0 };

    auto src = makeSequences(
            literals,
            PTRDIFF_MAX,
            offsets,
            litLens,
            matchLens,
            2,
            sizeof(uint64_t));

    uint8_t dst[256] = {};
    EXPECT_EQ(
            ZL_Lz_decode(dst, sizeof(dst), &src),
            ZL_LzError_literalLengthTooLarge);
}

} // namespace
