// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_BENCHMARK_UNITBENCH_SCENARIOS_CODECS_BITSPLIT_H
#define ZSTRONG_BENCHMARK_UNITBENCH_SCENARIOS_CODECS_BITSPLIT_H

#include <stddef.h>                          /* size_t */
#include "benchmark/unitBench/bench_entry.h" /* BenchPayload, BMK_benchFn_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ===   Decode scenarios   === */

/**
 * Preparation function for fp32 decomposition benchmark.
 * Packs 3 source streams with bitWidths {23, 8, 1} contiguously into src.
 */
size_t
bitSplitDecode_fp32_prep(void* src, size_t srcSize, const BenchPayload* bp);

/**
 * Output size for fp32 decode: nbElts * 4, where nbElts = srcSize / 6.
 */
size_t bitSplitDecode_fp32_outSize(const void* src, size_t srcSize);

/**
 * Wrapper function for fp32 decomposition decode benchmark.
 * Decodes 3 streams from src into 32-bit elements in dst.
 */
size_t bitSplitDecode_fp32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

/**
 * Preparation function for bf16 decomposition benchmark.
 * Packs 3 source streams with bitWidths {7, 8, 1} contiguously into src.
 */
size_t
bitSplitDecode_bf16_prep(void* src, size_t srcSize, const BenchPayload* bp);

/**
 * Output size for bf16 decode: nbElts * 2, where nbElts = srcSize / 3.
 */
size_t bitSplitDecode_bf16_outSize(const void* src, size_t srcSize);

/**
 * Wrapper function for bf16 decomposition decode benchmark.
 * Decodes 3 streams from src into 16-bit elements in dst.
 */
size_t bitSplitDecode_bf16_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

/**
 * Preparation function for bounded32 integer benchmark.
 * Packs 2 source streams with bitWidths {13, 8} contiguously into src.
 */
size_t bitSplitDecode_bounded32_prep(
        void* src,
        size_t srcSize,
        const BenchPayload* bp);

/**
 * Output size for bounded32 decode: nbElts * 4, where nbElts = srcSize / 3.
 */
size_t bitSplitDecode_bounded32_outSize(const void* src, size_t srcSize);

/**
 * Wrapper function for bounded32 integer decode benchmark.
 * Decodes 2 streams from src into 32-bit elements in dst.
 */
size_t bitSplitDecode_bounded32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

/* ===   Encode scenarios   === */

/**
 * Preparation function for fp32 encode benchmark.
 * Fills src with random 32-bit values.
 */
size_t
bitSplitEncode_fp32_prep(void* src, size_t srcSize, const BenchPayload* bp);

/**
 * Output size for fp32 encode: nbElts * 6, where nbElts = srcSize / 4.
 */
size_t bitSplitEncode_fp32_outSize(const void* src, size_t srcSize);

/**
 * Wrapper function for fp32 encode benchmark.
 * Encodes 32-bit elements from src into 3 streams in dst.
 */
size_t bitSplitEncode_fp32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

/**
 * Preparation function for bf16 encode benchmark.
 * Fills src with random 16-bit values.
 */
size_t
bitSplitEncode_bf16_prep(void* src, size_t srcSize, const BenchPayload* bp);

/**
 * Output size for bf16 encode: nbElts * 3, where nbElts = srcSize / 2.
 */
size_t bitSplitEncode_bf16_outSize(const void* src, size_t srcSize);

/**
 * Wrapper function for bf16 encode benchmark.
 * Encodes 16-bit elements from src into 3 streams in dst.
 */
size_t bitSplitEncode_bf16_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

/**
 * Preparation function for bounded32 encode benchmark.
 * Fills src with random 32-bit values (top 11 bits zero).
 */
size_t bitSplitEncode_bounded32_prep(
        void* src,
        size_t srcSize,
        const BenchPayload* bp);

/**
 * Output size for bounded32 encode: nbElts * 3, where nbElts = srcSize / 4.
 */
size_t bitSplitEncode_bounded32_outSize(const void* src, size_t srcSize);

/**
 * Wrapper function for bounded32 encode benchmark.
 * Encodes 32-bit elements from src into 2 streams in dst.
 */
size_t bitSplitEncode_bounded32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

#ifdef __cplusplus
}
#endif

#endif // ZSTRONG_BENCHMARK_UNITBENCH_SCENARIOS_CODECS_BITSPLIT_H
