// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_BENCHMARK_UNITBENCH_SCENARIOS_CODECS_BITSPLIT_H
#define ZSTRONG_BENCHMARK_UNITBENCH_SCENARIOS_CODECS_BITSPLIT_H

#include <stddef.h> /* size_t */
#include "benchmark/unitBench/bench_entry.h" /* BenchPayload, BMK_benchFn_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ===   Decode scenarios   === */

/**
 * Preparation function for fp32 decomposition benchmark.
 * Generates 3 source streams with bitWidths {23, 8, 1} and srcEltWidths {4, 1, 1}.
 */
size_t bitSplitDecode_fp32_prep(void* src, size_t srcSize, const BenchPayload* bp);

/**
 * Preparation function for bf16 decomposition benchmark.
 * Generates 3 source streams with bitWidths {7, 8, 1} and srcEltWidths {1, 1, 1}.
 */
size_t bitSplitDecode_bf16_prep(void* src, size_t srcSize, const BenchPayload* bp);

/**
 * Preparation function for bounded32 integer benchmark.
 * Generates 2 source streams with bitWidths {13, 8} and srcEltWidths {2, 1}.
 */
size_t bitSplitDecode_bounded32_prep(void* src, size_t srcSize, const BenchPayload* bp);

/**
 * Wrapper function for fp32 decomposition decode benchmark.
 * Decodes 3 streams into 32-bit elements.
 */
size_t bitSplitDecode_fp32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

/**
 * Wrapper function for bf16 decomposition decode benchmark.
 * Decodes 3 streams into 16-bit elements.
 */
size_t bitSplitDecode_bf16_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

/**
 * Wrapper function for bounded32 integer decode benchmark.
 * Decodes 2 streams into 32-bit elements.
 */
size_t bitSplitDecode_bounded32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

/**
 * Output size function for bf16 decode scenario.
 * Returns nbElts * 2 (16-bit destination elements).
 */
size_t bitSplitDecode_bf16_outSize(const void* src, size_t srcSize);

/* ===   Encode scenarios   === */

/**
 * Preparation function for fp32 encode benchmark.
 * Generates source array with random 32-bit values (top bits zero).
 */
size_t bitSplitEncode_fp32_prep(void* src, size_t srcSize, const BenchPayload* bp);

/**
 * Preparation function for bf16 encode benchmark.
 * Generates source array with random 16-bit values.
 */
size_t bitSplitEncode_bf16_prep(void* src, size_t srcSize, const BenchPayload* bp);

/**
 * Preparation function for bounded32 encode benchmark.
 * Generates source array with random 32-bit values (top 11 bits zero).
 */
size_t bitSplitEncode_bounded32_prep(void* src, size_t srcSize, const BenchPayload* bp);

/**
 * Wrapper function for fp32 encode benchmark.
 * Encodes 32-bit elements into 3 streams.
 */
size_t bitSplitEncode_fp32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

/**
 * Wrapper function for bf16 encode benchmark.
 * Encodes 16-bit elements into 3 streams.
 */
size_t bitSplitEncode_bf16_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

/**
 * Wrapper function for bounded32 encode benchmark.
 * Encodes 32-bit elements into 2 streams.
 */
size_t bitSplitEncode_bounded32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

/**
 * Output size function for bf16 encode scenario.
 * Returns nbElts * 2 (source is 16-bit elements).
 */
size_t bitSplitEncode_bf16_outSize(const void* src, size_t srcSize);

#ifdef __cplusplus
}
#endif

#endif // ZSTRONG_BENCHMARK_UNITBENCH_SCENARIOS_CODECS_BITSPLIT_H
