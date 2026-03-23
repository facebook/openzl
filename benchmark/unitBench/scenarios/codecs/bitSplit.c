// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/unitBench/scenarios/codecs/bitSplit.h"

#include <stdint.h> /* uint8_t, uint16_t, uint32_t, uint64_t */
#include <stdlib.h> /* rand */

#include "openzl/codecs/bitSplit/decode_bitSplit_kernel.h" /* ZL_bitSplitDecode */
#include "openzl/codecs/bitSplit/encode_bitSplit_kernel.h" /* ZL_bitSplitEncode */

/*
 * Helper: fill a buffer with random values masked to bitWidth.
 * Each element is srcEltWidth bytes.
 */
static void fillRandomMasked(
        void* buf,
        size_t nbElts,
        size_t srcEltWidth,
        unsigned bitWidth)
{
    uint64_t const mask = (bitWidth >= 64) ? ~0ULL : (1ULL << bitWidth) - 1;
    uint8_t* p          = (uint8_t*)buf;

    for (size_t e = 0; e < nbElts; e++) {
        uint64_t value = ((uint64_t)rand() << 32) | (uint64_t)rand();
        value &= mask;
        /* Store value in little-endian order using srcEltWidth bytes */
        for (size_t b = 0; b < srcEltWidth; b++) {
            p[e * srcEltWidth + b] = (uint8_t)(value >> (b * 8));
        }
    }
}

/*
 * Helper: fill source buffer with random values, top bits zeroed.
 */
static void
fillRandomSrc(void* buf, size_t nbElts, size_t eltWidth, size_t usedBits)
{
    uint64_t const mask = (usedBits >= 64) ? ~0ULL : (1ULL << usedBits) - 1;
    uint8_t* p          = (uint8_t*)buf;

    for (size_t e = 0; e < nbElts; e++) {
        uint64_t value = ((uint64_t)rand() << 32) | (uint64_t)rand();
        value &= mask;
        for (size_t b = 0; b < eltWidth; b++) {
            p[e * eltWidth + b] = (uint8_t)(value >> (b * 8));
        }
    }
}

/* =========================================================================
 *                          DECODE SCENARIOS
 * =========================================================================
 * Prep packs all source streams contiguously into src:
 *   Layout: [stream0 | stream1 | ... | streamN]
 *   where each streamI has nbElts * srcEltWidths[I] bytes.
 *
 * nbElts = srcSize / sum(srcEltWidths)
 * prep returns nbElts * sum(srcEltWidths)  (== srcSize rounded down)
 *
 * Wrapper recomputes stream pointers from offsets into src, decodes into dst.
 * outSize returns nbElts * dstEltWidth.
 */

/* ===   fp32 decode scenario   ===
 * bitWidths {23, 8, 1}, srcEltWidths {4, 1, 1}, dstEltWidth=4
 * sum(srcEltWidths) = 6
 */

size_t
bitSplitDecode_fp32_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)bp;

    static const size_t srcEltWidths[3] = { 4, 1, 1 };
    static const unsigned bitWidths[3]  = { 23, 8, 1 };
    static const size_t sumSrcElt       = 4 + 1 + 1; /* 6 */

    size_t const nbElts = srcSize / sumSrcElt;
    if (nbElts == 0)
        return 0;

    uint8_t* p    = (uint8_t*)src;
    size_t offset = 0;
    for (int i = 0; i < 3; i++) {
        fillRandomMasked(p + offset, nbElts, srcEltWidths[i], bitWidths[i]);
        offset += nbElts * srcEltWidths[i];
    }

    return nbElts * sumSrcElt;
}

size_t bitSplitDecode_fp32_outSize(const void* src, size_t srcSize)
{
    (void)src;
    /* nbElts = srcSize / 6, output = nbElts * 4 */
    return (srcSize / 6) * 4;
}

size_t bitSplitDecode_fp32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)dstCapacity;
    (void)customPayload;

    static const size_t dstEltWidth     = 4;
    static const size_t srcEltWidths[3] = { 4, 1, 1 };
    static const uint8_t bitWidths[3]   = { 23, 8, 1 };
    static const size_t nbWidths        = 3;
    static const size_t sumSrcElt       = 4 + 1 + 1; /* 6 */

    size_t const nbElts = srcSize / sumSrcElt;
    const uint8_t* p    = (const uint8_t*)src;

    size_t offset = 0;
    const void* srcPtrs[3];
    for (int i = 0; i < 3; i++) {
        srcPtrs[i] = p + offset;
        offset += nbElts * srcEltWidths[i];
    }

    ZL_bitSplitDecode(
            dst,
            dstEltWidth,
            nbElts,
            srcPtrs,
            srcEltWidths,
            bitWidths,
            nbWidths);

    return nbElts * dstEltWidth;
}

/* ===   bf16 decode scenario   ===
 * bitWidths {7, 8, 1}, srcEltWidths {1, 1, 1}, dstEltWidth=2
 * sum(srcEltWidths) = 3
 */

size_t
bitSplitDecode_bf16_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)bp;

    static const size_t srcEltWidths[3] = { 1, 1, 1 };
    static const unsigned bitWidths[3]  = { 7, 8, 1 };
    static const size_t sumSrcElt       = 1 + 1 + 1; /* 3 */

    size_t const nbElts = srcSize / sumSrcElt;
    if (nbElts == 0)
        return 0;

    uint8_t* p    = (uint8_t*)src;
    size_t offset = 0;
    for (int i = 0; i < 3; i++) {
        fillRandomMasked(p + offset, nbElts, srcEltWidths[i], bitWidths[i]);
        offset += nbElts * srcEltWidths[i];
    }

    return nbElts * sumSrcElt;
}

size_t bitSplitDecode_bf16_outSize(const void* src, size_t srcSize)
{
    (void)src;
    /* nbElts = srcSize / 3, output = nbElts * 2 */
    return (srcSize / 3) * 2;
}

size_t bitSplitDecode_bf16_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)dstCapacity;
    (void)customPayload;

    static const size_t dstEltWidth     = 2;
    static const size_t srcEltWidths[3] = { 1, 1, 1 };
    static const uint8_t bitWidths[3]   = { 7, 8, 1 };
    static const size_t nbWidths        = 3;
    static const size_t sumSrcElt       = 1 + 1 + 1; /* 3 */

    size_t const nbElts = srcSize / sumSrcElt;
    const uint8_t* p    = (const uint8_t*)src;

    size_t offset = 0;
    const void* srcPtrs[3];
    for (int i = 0; i < 3; i++) {
        srcPtrs[i] = p + offset;
        offset += nbElts * srcEltWidths[i];
    }

    ZL_bitSplitDecode(
            dst,
            dstEltWidth,
            nbElts,
            srcPtrs,
            srcEltWidths,
            bitWidths,
            nbWidths);

    return nbElts * dstEltWidth;
}

/* ===   bounded32 decode scenario   ===
 * bitWidths {13, 8}, srcEltWidths {2, 1}, dstEltWidth=4
 * sum(srcEltWidths) = 3
 */

size_t
bitSplitDecode_bounded32_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)bp;

    static const size_t srcEltWidths[2] = { 2, 1 };
    static const unsigned bitWidths[2]  = { 13, 8 };
    static const size_t sumSrcElt       = 2 + 1; /* 3 */

    size_t const nbElts = srcSize / sumSrcElt;
    if (nbElts == 0)
        return 0;

    uint8_t* p    = (uint8_t*)src;
    size_t offset = 0;
    for (int i = 0; i < 2; i++) {
        fillRandomMasked(p + offset, nbElts, srcEltWidths[i], bitWidths[i]);
        offset += nbElts * srcEltWidths[i];
    }

    return nbElts * sumSrcElt;
}

size_t bitSplitDecode_bounded32_outSize(const void* src, size_t srcSize)
{
    (void)src;
    /* nbElts = srcSize / 3, output = nbElts * 4 */
    return (srcSize / 3) * 4;
}

size_t bitSplitDecode_bounded32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)dstCapacity;
    (void)customPayload;

    static const size_t dstEltWidth     = 4;
    static const size_t srcEltWidths[2] = { 2, 1 };
    static const uint8_t bitWidths[2]   = { 13, 8 };
    static const size_t nbWidths        = 2;
    static const size_t sumSrcElt       = 2 + 1; /* 3 */

    size_t const nbElts = srcSize / sumSrcElt;
    const uint8_t* p    = (const uint8_t*)src;

    size_t offset = 0;
    const void* srcPtrs[2];
    for (int i = 0; i < 2; i++) {
        srcPtrs[i] = p + offset;
        offset += nbElts * srcEltWidths[i];
    }

    ZL_bitSplitDecode(
            dst,
            dstEltWidth,
            nbElts,
            srcPtrs,
            srcEltWidths,
            bitWidths,
            nbWidths);

    return nbElts * dstEltWidth;
}

/* =========================================================================
 *                          ENCODE SCENARIOS
 * =========================================================================
 * Prep fills src with random source data, returns srcSize.
 *
 * Wrapper reads src, writes output streams contiguously into dst:
 *   Layout in dst: [stream0 | stream1 | ... | streamN]
 *   where each streamI has nbElts * dstEltWidths[I] bytes.
 *
 * nbElts = srcSize / srcEltWidth
 * outSize returns nbElts * sum(dstEltWidths).
 */

/* ===   fp32 encode scenario   ===
 * srcEltWidth=4, bitWidths {23, 8, 1}, dstEltWidths {4, 1, 1}
 * sum(dstEltWidths) = 6
 */

size_t
bitSplitEncode_fp32_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)bp;

    static const size_t srcEltWidth = 4;
    static const size_t sumBits     = 23 + 8 + 1; /* 32 bits total */

    size_t const nbElts = srcSize / srcEltWidth;
    if (nbElts == 0)
        return 0;

    fillRandomSrc(src, nbElts, srcEltWidth, sumBits);

    return nbElts * srcEltWidth;
}

size_t bitSplitEncode_fp32_outSize(const void* src, size_t srcSize)
{
    (void)src;
    /* nbElts = srcSize / 4, output = nbElts * 6 */
    return (srcSize / 4) * 6;
}

size_t bitSplitEncode_fp32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)dstCapacity;
    (void)customPayload;

    static const size_t srcEltWidth     = 4;
    static const size_t dstEltWidths[3] = { 4, 1, 1 };
    static const uint8_t bitWidths[3]   = { 23, 8, 1 };
    static const size_t nbWidths        = 3;
    static const size_t sumDstElt       = 4 + 1 + 1; /* 6 */

    size_t const nbElts = srcSize / srcEltWidth;
    uint8_t* p          = (uint8_t*)dst;

    size_t offset = 0;
    void* dstPtrs[3];
    for (int i = 0; i < 3; i++) {
        dstPtrs[i] = p + offset;
        offset += nbElts * dstEltWidths[i];
    }

    ZL_bitSplitEncode(
            dstPtrs,
            dstEltWidths,
            nbElts,
            src,
            srcEltWidth,
            bitWidths,
            nbWidths);

    return nbElts * sumDstElt;
}

/* ===   bf16 encode scenario   ===
 * srcEltWidth=2, bitWidths {7, 8, 1}, dstEltWidths {1, 1, 1}
 * sum(dstEltWidths) = 3
 */

size_t
bitSplitEncode_bf16_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)bp;

    static const size_t srcEltWidth = 2;
    static const size_t sumBits     = 7 + 8 + 1; /* 16 bits total */

    size_t const nbElts = srcSize / srcEltWidth;
    if (nbElts == 0)
        return 0;

    fillRandomSrc(src, nbElts, srcEltWidth, sumBits);

    return nbElts * srcEltWidth;
}

size_t bitSplitEncode_bf16_outSize(const void* src, size_t srcSize)
{
    (void)src;
    /* nbElts = srcSize / 2, output = nbElts * 3 */
    return (srcSize / 2) * 3;
}

size_t bitSplitEncode_bf16_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)dstCapacity;
    (void)customPayload;

    static const size_t srcEltWidth     = 2;
    static const size_t dstEltWidths[3] = { 1, 1, 1 };
    static const uint8_t bitWidths[3]   = { 7, 8, 1 };
    static const size_t nbWidths        = 3;
    static const size_t sumDstElt       = 1 + 1 + 1; /* 3 */

    size_t const nbElts = srcSize / srcEltWidth;
    uint8_t* p          = (uint8_t*)dst;

    size_t offset = 0;
    void* dstPtrs[3];
    for (int i = 0; i < 3; i++) {
        dstPtrs[i] = p + offset;
        offset += nbElts * dstEltWidths[i];
    }

    ZL_bitSplitEncode(
            dstPtrs,
            dstEltWidths,
            nbElts,
            src,
            srcEltWidth,
            bitWidths,
            nbWidths);

    return nbElts * sumDstElt;
}

/* ===   bounded32 encode scenario   ===
 * srcEltWidth=4, bitWidths {13, 8}, dstEltWidths {2, 1}
 * sum(dstEltWidths) = 3
 */

size_t
bitSplitEncode_bounded32_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)bp;

    static const size_t srcEltWidth = 4;
    static const size_t sumBits     = 13 + 8; /* 21 bits total */

    size_t const nbElts = srcSize / srcEltWidth;
    if (nbElts == 0)
        return 0;

    fillRandomSrc(src, nbElts, srcEltWidth, sumBits);

    return nbElts * srcEltWidth;
}

size_t bitSplitEncode_bounded32_outSize(const void* src, size_t srcSize)
{
    (void)src;
    /* nbElts = srcSize / 4, output = nbElts * 3 */
    return (srcSize / 4) * 3;
}

size_t bitSplitEncode_bounded32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)dstCapacity;
    (void)customPayload;

    static const size_t srcEltWidth     = 4;
    static const size_t dstEltWidths[2] = { 2, 1 };
    static const uint8_t bitWidths[2]   = { 13, 8 };
    static const size_t nbWidths        = 2;
    static const size_t sumDstElt       = 2 + 1; /* 3 */

    size_t const nbElts = srcSize / srcEltWidth;
    uint8_t* p          = (uint8_t*)dst;

    size_t offset = 0;
    void* dstPtrs[2];
    for (int i = 0; i < 2; i++) {
        dstPtrs[i] = p + offset;
        offset += nbElts * dstEltWidths[i];
    }

    ZL_bitSplitEncode(
            dstPtrs,
            dstEltWidths,
            nbElts,
            src,
            srcEltWidth,
            bitWidths,
            nbWidths);

    return nbElts * sumDstElt;
}

/* ===   fp64 encode scenario   ===
 * srcEltWidth=8, bitWidths {52, 11, 1}, dstEltWidths {8, 2, 1}
 * sum(dstEltWidths) = 11
 */

size_t
bitSplitEncode_fp64_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)bp;

    static const size_t srcEltWidth = 8;
    static const size_t sumBits     = 52 + 11 + 1; /* 64 bits total */

    size_t const nbElts = srcSize / srcEltWidth;
    if (nbElts == 0)
        return 0;

    fillRandomSrc(src, nbElts, srcEltWidth, sumBits);

    return nbElts * srcEltWidth;
}

size_t bitSplitEncode_fp64_outSize(const void* src, size_t srcSize)
{
    (void)src;
    /* nbElts = srcSize / 8, output = nbElts * 11 */
    return (srcSize / 8) * 11;
}

size_t bitSplitEncode_fp64_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)dstCapacity;
    (void)customPayload;

    static const size_t srcEltWidth     = 8;
    static const size_t dstEltWidths[3] = { 8, 2, 1 };
    static const uint8_t bitWidths[3]   = { 52, 11, 1 };
    static const size_t nbWidths        = 3;
    static const size_t sumDstElt       = 8 + 2 + 1; /* 11 */

    size_t const nbElts = srcSize / srcEltWidth;
    uint8_t* p          = (uint8_t*)dst;

    size_t offset = 0;
    void* dstPtrs[3];
    for (int i = 0; i < 3; i++) {
        dstPtrs[i] = p + offset;
        offset += nbElts * dstEltWidths[i];
    }

    ZL_bitSplitEncode(
            dstPtrs,
            dstEltWidths,
            nbElts,
            src,
            srcEltWidth,
            bitWidths,
            nbWidths);

    return nbElts * sumDstElt;
}

/* ===   fp16 encode scenario   ===
 * srcEltWidth=2, bitWidths {10, 5, 1}, dstEltWidths {2, 1, 1}
 * sum(dstEltWidths) = 4
 */

size_t
bitSplitEncode_fp16_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)bp;

    static const size_t srcEltWidth = 2;
    static const size_t sumBits     = 10 + 5 + 1; /* 16 bits total */

    size_t const nbElts = srcSize / srcEltWidth;
    if (nbElts == 0)
        return 0;

    fillRandomSrc(src, nbElts, srcEltWidth, sumBits);

    return nbElts * srcEltWidth;
}

size_t bitSplitEncode_fp16_outSize(const void* src, size_t srcSize)
{
    (void)src;
    /* nbElts = srcSize / 2, output = nbElts * 4 */
    return (srcSize / 2) * 4;
}

size_t bitSplitEncode_fp16_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)dstCapacity;
    (void)customPayload;

    static const size_t srcEltWidth     = 2;
    static const size_t dstEltWidths[3] = { 2, 1, 1 };
    static const uint8_t bitWidths[3]   = { 10, 5, 1 };
    static const size_t nbWidths        = 3;
    static const size_t sumDstElt       = 2 + 1 + 1; /* 4 */

    size_t const nbElts = srcSize / srcEltWidth;
    uint8_t* p          = (uint8_t*)dst;

    size_t offset = 0;
    void* dstPtrs[3];
    for (int i = 0; i < 3; i++) {
        dstPtrs[i] = p + offset;
        offset += nbElts * dstEltWidths[i];
    }

    ZL_bitSplitEncode(
            dstPtrs,
            dstEltWidths,
            nbElts,
            src,
            srcEltWidth,
            bitWidths,
            nbWidths);

    return nbElts * sumDstElt;
}
