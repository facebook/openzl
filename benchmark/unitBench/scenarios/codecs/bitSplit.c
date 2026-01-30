// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/unitBench/scenarios/codecs/bitSplit.h"

#include <assert.h>  /* assert */
#include <stdint.h>  /* uint8_t, uint16_t, uint32_t, uint64_t */
#include <stdlib.h>  /* malloc, free, rand */
#include <string.h>  /* memcpy */

#include "openzl/codecs/bitSplit/decode_bitSplit_kernel.h" /* ZS_bitSplitDecode */

/*
 * Static state for each scenario to hold allocated source streams.
 * Prep functions allocate these; wrapper functions use them.
 */

/* fp32: 3 streams, bitWidths {23, 8, 1}, srcEltWidths {4, 1, 1}, dstEltWidth=4 */
typedef struct {
    void* srcStreams[3];
    size_t nbElts;
} BitSplitState_fp32;
static BitSplitState_fp32 g_state_fp32;

/* bf16: 3 streams, bitWidths {7, 8, 1}, srcEltWidths {1, 1, 1}, dstEltWidth=2 */
typedef struct {
    void* srcStreams[3];
    size_t nbElts;
} BitSplitState_bf16;
static BitSplitState_bf16 g_state_bf16;

/* bounded32: 2 streams, bitWidths {13, 8}, srcEltWidths {2, 1}, dstEltWidth=4 */
typedef struct {
    void* srcStreams[2];
    size_t nbElts;
} BitSplitState_bounded32;
static BitSplitState_bounded32 g_state_bounded32;

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
        free(g_state_fp32.srcStreams[i]);
        g_state_fp32.srcStreams[i] = malloc(nbElts * srcEltWidths[i]);
        assert(g_state_fp32.srcStreams[i] != NULL);
        fillRandomMasked(
                g_state_fp32.srcStreams[i],
                nbElts,
                srcEltWidths[i],
                bitWidths[i]);
    }
    g_state_fp32.nbElts = nbElts;

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

    size_t const nbElts = g_state_fp32.nbElts;
    const void* srcPtrs[3] = {
            g_state_fp32.srcStreams[0],
            g_state_fp32.srcStreams[1],
            g_state_fp32.srcStreams[2]};

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
        free(g_state_bf16.srcStreams[i]);
        g_state_bf16.srcStreams[i] = malloc(nbElts * srcEltWidths[i]);
        assert(g_state_bf16.srcStreams[i] != NULL);
        fillRandomMasked(
                g_state_bf16.srcStreams[i],
                nbElts,
                srcEltWidths[i],
                bitWidths[i]);
    }
    g_state_bf16.nbElts = nbElts;

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

    size_t const nbElts = g_state_bf16.nbElts;
    const void* srcPtrs[3] = {
            g_state_bf16.srcStreams[0],
            g_state_bf16.srcStreams[1],
            g_state_bf16.srcStreams[2]};

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
        free(g_state_bounded32.srcStreams[i]);
        g_state_bounded32.srcStreams[i] = malloc(nbElts * srcEltWidths[i]);
        assert(g_state_bounded32.srcStreams[i] != NULL);
        fillRandomMasked(
                g_state_bounded32.srcStreams[i],
                nbElts,
                srcEltWidths[i],
                bitWidths[i]);
    }
    g_state_bounded32.nbElts = nbElts;

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

    size_t const nbElts = g_state_bounded32.nbElts;
    const void* srcPtrs[2] = {
            g_state_bounded32.srcStreams[0],
            g_state_bounded32.srcStreams[1]};

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
