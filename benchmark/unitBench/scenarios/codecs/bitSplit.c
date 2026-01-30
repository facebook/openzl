// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/unitBench/scenarios/codecs/bitSplit.h"

#include <assert.h>  /* assert */
#include <stdint.h>  /* uint8_t, uint16_t, uint32_t, uint64_t */
#include <stdlib.h>  /* malloc, free, rand */
#include <string.h>  /* memcpy */

#include "openzl/codecs/bitSplit/decode_bitSplit_kernel.h" /* ZS_bitSplitDecode */
#include "openzl/codecs/bitSplit/encode_bitSplit_kernel.h" /* ZS_bitSplitEncode */

/*
 * Static state for each scenario to hold allocated source streams.
 * Prep functions allocate these; wrapper functions use them.
 */

/* ===   Decode state   === */

/* fp32: 3 streams, bitWidths {23, 8, 1}, srcEltWidths {4, 1, 1}, dstEltWidth=4 */
typedef struct {
    void* srcStreams[3];
    size_t nbElts;
} BitSplitDecodeState_fp32;
static BitSplitDecodeState_fp32 g_decState_fp32;

/* bf16: 3 streams, bitWidths {7, 8, 1}, srcEltWidths {1, 1, 1}, dstEltWidth=2 */
typedef struct {
    void* srcStreams[3];
    size_t nbElts;
} BitSplitDecodeState_bf16;
static BitSplitDecodeState_bf16 g_decState_bf16;

/* bounded32: 2 streams, bitWidths {13, 8}, srcEltWidths {2, 1}, dstEltWidth=4 */
typedef struct {
    void* srcStreams[2];
    size_t nbElts;
} BitSplitDecodeState_bounded32;
static BitSplitDecodeState_bounded32 g_decState_bounded32;

/* ===   Encode state   === */

/* fp32: srcEltWidth=4, 3 dst streams with dstEltWidths {4, 1, 1} */
typedef struct {
    void* srcData;
    void* dstStreams[3];
    size_t nbElts;
} BitSplitEncodeState_fp32;
static BitSplitEncodeState_fp32 g_encState_fp32;

/* bf16: srcEltWidth=2, 3 dst streams with dstEltWidths {1, 1, 1} */
typedef struct {
    void* srcData;
    void* dstStreams[3];
    size_t nbElts;
} BitSplitEncodeState_bf16;
static BitSplitEncodeState_bf16 g_encState_bf16;

/* bounded32: srcEltWidth=4, 2 dst streams with dstEltWidths {2, 1} */
typedef struct {
    void* srcData;
    void* dstStreams[2];
    size_t nbElts;
} BitSplitEncodeState_bounded32;
static BitSplitEncodeState_bounded32 g_encState_bounded32;

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
    uint64_t const mask =
            (bitWidth >= 64) ? ~0ULL : (1ULL << bitWidth) - 1;
    uint8_t* p = (uint8_t*)buf;

    for (size_t e = 0; e < nbElts; e++) {
        uint64_t value = ((uint64_t)rand() << 32) | (uint64_t)rand();
        value &= mask;
        /* Store value in little-endian order using srcEltWidth bytes */
        for (size_t b = 0; b < srcEltWidth; b++) {
            p[e * srcEltWidth + b] = (uint8_t)(value >> (b * 8));
        }
    }
}

/* ===   fp32 scenario   === */
/* bitWidths {23, 8, 1}, srcEltWidths {4, 1, 1}, dstEltWidth=4 */

size_t bitSplitDecode_fp32_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)src;
    (void)bp;

    static const size_t dstEltWidth = 4;
    static const size_t srcEltWidths[3] = {4, 1, 1};
    static const unsigned bitWidths[3] = {23, 8, 1};

    size_t const nbElts = srcSize / dstEltWidth;
    if (nbElts == 0) return 0;

    /* Free any previous allocations */
    for (int i = 0; i < 3; i++) {
        free(g_decState_fp32.srcStreams[i]);
        g_decState_fp32.srcStreams[i] = malloc(nbElts * srcEltWidths[i]);
        assert(g_decState_fp32.srcStreams[i] != NULL);
        fillRandomMasked(
                g_decState_fp32.srcStreams[i],
                nbElts,
                srcEltWidths[i],
                bitWidths[i]);
    }
    g_decState_fp32.nbElts = nbElts;

    /* Return srcSize unchanged - we use it to determine nbElts in wrapper */
    return srcSize;
}

size_t bitSplitDecode_fp32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)src;
    (void)srcSize;
    (void)dstCapacity;
    (void)customPayload;

    static const size_t dstEltWidth = 4;
    static const size_t srcEltWidths[3] = {4, 1, 1};
    static const uint8_t bitWidths[3] = {23, 8, 1};
    static const size_t nbWidths = 3;

    size_t const nbElts = g_decState_fp32.nbElts;
    const void* srcPtrs[3] = {
            g_decState_fp32.srcStreams[0],
            g_decState_fp32.srcStreams[1],
            g_decState_fp32.srcStreams[2]};

    ZS_bitSplitDecode(
            dst,
            dstEltWidth,
            nbElts,
            srcPtrs,
            srcEltWidths,
            bitWidths,
            nbWidths);

    return nbElts * dstEltWidth;
}

/* ========================================================================= */
/*                           ENCODE SCENARIOS                                */
/* ========================================================================= */

/*
 * Helper: fill source buffer with random values, top bits zeroed.
 */
static void fillRandomSrc(void* buf, size_t nbElts, size_t eltWidth, size_t usedBits)
{
    uint64_t const mask = (usedBits >= 64) ? ~0ULL : (1ULL << usedBits) - 1;
    uint8_t* p = (uint8_t*)buf;

    for (size_t e = 0; e < nbElts; e++) {
        uint64_t value = ((uint64_t)rand() << 32) | (uint64_t)rand();
        value &= mask;
        for (size_t b = 0; b < eltWidth; b++) {
            p[e * eltWidth + b] = (uint8_t)(value >> (b * 8));
        }
    }
}

/* ===   fp32 encode scenario   === */
/* srcEltWidth=4, bitWidths {23, 8, 1}, dstEltWidths {4, 1, 1} */

size_t bitSplitEncode_fp32_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)src;
    (void)bp;

    static const size_t srcEltWidth = 4;
    static const size_t dstEltWidths[3] = {4, 1, 1};
    static const size_t sumBits = 23 + 8 + 1; /* 32 bits total */

    size_t const nbElts = srcSize / srcEltWidth;
    if (nbElts == 0) return 0;

    /* Allocate source */
    free(g_encState_fp32.srcData);
    g_encState_fp32.srcData = malloc(nbElts * srcEltWidth);
    assert(g_encState_fp32.srcData != NULL);
    fillRandomSrc(g_encState_fp32.srcData, nbElts, srcEltWidth, sumBits);

    /* Allocate destination streams */
    for (int i = 0; i < 3; i++) {
        free(g_encState_fp32.dstStreams[i]);
        g_encState_fp32.dstStreams[i] = malloc(nbElts * dstEltWidths[i]);
        assert(g_encState_fp32.dstStreams[i] != NULL);
    }
    g_encState_fp32.nbElts = nbElts;

    return srcSize;
}

size_t bitSplitEncode_fp32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)src;
    (void)srcSize;
    (void)dst;
    (void)dstCapacity;
    (void)customPayload;

    static const size_t srcEltWidth = 4;
    static const size_t dstEltWidths[3] = {4, 1, 1};
    static const uint8_t bitWidths[3] = {23, 8, 1};
    static const size_t nbWidths = 3;

    size_t const nbElts = g_encState_fp32.nbElts;
    void* dstPtrs[3] = {
            g_encState_fp32.dstStreams[0],
            g_encState_fp32.dstStreams[1],
            g_encState_fp32.dstStreams[2]};

    ZS_bitSplitEncode(
            dstPtrs,
            dstEltWidths,
            nbElts,
            g_encState_fp32.srcData,
            srcEltWidth,
            bitWidths,
            nbWidths);

    return nbElts * srcEltWidth;
}

/* ===   bf16 encode scenario   === */
/* srcEltWidth=2, bitWidths {7, 8, 1}, dstEltWidths {1, 1, 1} */

size_t bitSplitEncode_bf16_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)src;
    (void)bp;

    static const size_t srcEltWidth = 2;
    static const size_t dstEltWidths[3] = {1, 1, 1};
    static const size_t sumBits = 7 + 8 + 1; /* 16 bits total */

    /* srcSize is based on input file, compute nbElts from it */
    size_t const nbElts = srcSize / 4; /* Same as decode to get consistent nbElts */
    if (nbElts == 0) return 0;

    /* Allocate source */
    free(g_encState_bf16.srcData);
    g_encState_bf16.srcData = malloc(nbElts * srcEltWidth);
    assert(g_encState_bf16.srcData != NULL);
    fillRandomSrc(g_encState_bf16.srcData, nbElts, srcEltWidth, sumBits);

    /* Allocate destination streams */
    for (int i = 0; i < 3; i++) {
        free(g_encState_bf16.dstStreams[i]);
        g_encState_bf16.dstStreams[i] = malloc(nbElts * dstEltWidths[i]);
        assert(g_encState_bf16.dstStreams[i] != NULL);
    }
    g_encState_bf16.nbElts = nbElts;

    return srcSize;
}

size_t bitSplitEncode_bf16_outSize(const void* src, size_t srcSize)
{
    (void)src;
    /* srcSize is the original input size, nbElts = srcSize / 4 */
    /* Output reports source bytes processed: nbElts * 2 */
    size_t const nbElts = srcSize / 4;
    return nbElts * 2;
}

size_t bitSplitEncode_bf16_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)src;
    (void)srcSize;
    (void)dst;
    (void)dstCapacity;
    (void)customPayload;

    static const size_t srcEltWidth = 2;
    static const size_t dstEltWidths[3] = {1, 1, 1};
    static const uint8_t bitWidths[3] = {7, 8, 1};
    static const size_t nbWidths = 3;

    size_t const nbElts = g_encState_bf16.nbElts;
    void* dstPtrs[3] = {
            g_encState_bf16.dstStreams[0],
            g_encState_bf16.dstStreams[1],
            g_encState_bf16.dstStreams[2]};

    ZS_bitSplitEncode(
            dstPtrs,
            dstEltWidths,
            nbElts,
            g_encState_bf16.srcData,
            srcEltWidth,
            bitWidths,
            nbWidths);

    return nbElts * srcEltWidth;
}

/* ===   bounded32 encode scenario   === */
/* srcEltWidth=4, bitWidths {13, 8}, dstEltWidths {2, 1} */

size_t bitSplitEncode_bounded32_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)src;
    (void)bp;

    static const size_t srcEltWidth = 4;
    static const size_t dstEltWidths[2] = {2, 1};
    static const size_t sumBits = 13 + 8; /* 21 bits total */

    size_t const nbElts = srcSize / srcEltWidth;
    if (nbElts == 0) return 0;

    /* Allocate source */
    free(g_encState_bounded32.srcData);
    g_encState_bounded32.srcData = malloc(nbElts * srcEltWidth);
    assert(g_encState_bounded32.srcData != NULL);
    fillRandomSrc(g_encState_bounded32.srcData, nbElts, srcEltWidth, sumBits);

    /* Allocate destination streams */
    for (int i = 0; i < 2; i++) {
        free(g_encState_bounded32.dstStreams[i]);
        g_encState_bounded32.dstStreams[i] = malloc(nbElts * dstEltWidths[i]);
        assert(g_encState_bounded32.dstStreams[i] != NULL);
    }
    g_encState_bounded32.nbElts = nbElts;

    return srcSize;
}

size_t bitSplitEncode_bounded32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)src;
    (void)srcSize;
    (void)dst;
    (void)dstCapacity;
    (void)customPayload;

    static const size_t srcEltWidth = 4;
    static const size_t dstEltWidths[2] = {2, 1};
    static const uint8_t bitWidths[2] = {13, 8};
    static const size_t nbWidths = 2;

    size_t const nbElts = g_encState_bounded32.nbElts;
    void* dstPtrs[2] = {
            g_encState_bounded32.dstStreams[0],
            g_encState_bounded32.dstStreams[1]};

    ZS_bitSplitEncode(
            dstPtrs,
            dstEltWidths,
            nbElts,
            g_encState_bounded32.srcData,
            srcEltWidth,
            bitWidths,
            nbWidths);

    return nbElts * srcEltWidth;
}

/* ===   bf16 scenario   === */
/* bitWidths {7, 8, 1}, srcEltWidths {1, 1, 1}, dstEltWidth=2 */

size_t bitSplitDecode_bf16_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)src;
    (void)bp;

    static const size_t srcEltWidths[3] = {1, 1, 1};
    static const unsigned bitWidths[3] = {7, 8, 1};

    /* srcSize represents the destination buffer size (nbElts * 4 in original) */
    /* For bf16, we want to benchmark decoding into 16-bit elements */
    /* The input srcSize is based on the input file, we compute nbElts from it */
    size_t const nbElts = srcSize / 4; /* Use 4 to get same nbElts as fp32 scenario */
    if (nbElts == 0) return 0;

    /* Free any previous allocations */
    for (int i = 0; i < 3; i++) {
        free(g_decState_bf16.srcStreams[i]);
        g_decState_bf16.srcStreams[i] = malloc(nbElts * srcEltWidths[i]);
        assert(g_decState_bf16.srcStreams[i] != NULL);
        fillRandomMasked(
                g_decState_bf16.srcStreams[i],
                nbElts,
                srcEltWidths[i],
                bitWidths[i]);
    }
    g_decState_bf16.nbElts = nbElts;

    return srcSize;
}

size_t bitSplitDecode_bf16_outSize(const void* src, size_t srcSize)
{
    (void)src;
    /* srcSize is the original input size, nbElts = srcSize / 4 */
    /* Output is nbElts * 2 (16-bit destination elements) */
    size_t const nbElts = srcSize / 4;
    return nbElts * 2;
}

size_t bitSplitDecode_bf16_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)src;
    (void)srcSize;
    (void)dstCapacity;
    (void)customPayload;

    static const size_t dstEltWidth = 2;
    static const size_t srcEltWidths[3] = {1, 1, 1};
    static const uint8_t bitWidths[3] = {7, 8, 1};
    static const size_t nbWidths = 3;

    size_t const nbElts = g_decState_bf16.nbElts;
    const void* srcPtrs[3] = {
            g_decState_bf16.srcStreams[0],
            g_decState_bf16.srcStreams[1],
            g_decState_bf16.srcStreams[2]};

    ZS_bitSplitDecode(
            dst,
            dstEltWidth,
            nbElts,
            srcPtrs,
            srcEltWidths,
            bitWidths,
            nbWidths);

    return nbElts * dstEltWidth;
}

/* ===   bounded32 scenario   === */
/* bitWidths {13, 8}, srcEltWidths {2, 1}, dstEltWidth=4 */

size_t bitSplitDecode_bounded32_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)src;
    (void)bp;

    static const size_t dstEltWidth = 4;
    static const size_t srcEltWidths[2] = {2, 1};
    static const unsigned bitWidths[2] = {13, 8};

    size_t const nbElts = srcSize / dstEltWidth;
    if (nbElts == 0) return 0;

    /* Free any previous allocations */
    for (int i = 0; i < 2; i++) {
        free(g_decState_bounded32.srcStreams[i]);
        g_decState_bounded32.srcStreams[i] = malloc(nbElts * srcEltWidths[i]);
        assert(g_decState_bounded32.srcStreams[i] != NULL);
        fillRandomMasked(
                g_decState_bounded32.srcStreams[i],
                nbElts,
                srcEltWidths[i],
                bitWidths[i]);
    }
    g_decState_bounded32.nbElts = nbElts;

    return srcSize;
}

size_t bitSplitDecode_bounded32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)src;
    (void)srcSize;
    (void)dstCapacity;
    (void)customPayload;

    static const size_t dstEltWidth = 4;
    static const size_t srcEltWidths[2] = {2, 1};
    static const uint8_t bitWidths[2] = {13, 8};
    static const size_t nbWidths = 2;

    size_t const nbElts = g_decState_bounded32.nbElts;
    const void* srcPtrs[2] = {
            g_decState_bounded32.srcStreams[0],
            g_decState_bounded32.srcStreams[1]};

    ZS_bitSplitDecode(
            dst,
            dstEltWidth,
            nbElts,
            srcPtrs,
            srcEltWidths,
            bitWidths,
            nbWidths);

    return nbElts * dstEltWidth;
}
