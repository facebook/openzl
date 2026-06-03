// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_BENCHMARK_UNITBENCH_SPARSE_NUM_H
#define ZSTRONG_BENCHMARK_UNITBENCH_SPARSE_NUM_H

#include "benchmark/unitBench/bench_entry.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t sparseNumEncode8_outSize(const void* src, size_t srcSize);
size_t sparseNumEncode16_outSize(const void* src, size_t srcSize);
size_t sparseNumEncode32_outSize(const void* src, size_t srcSize);
size_t sparseNumEncode64_outSize(const void* src, size_t srcSize);

size_t sparseNumEncode8_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);
size_t sparseNumEncode16_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);
size_t sparseNumEncode32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);
size_t sparseNumEncode64_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

size_t sparseNumDecode8_d8_outSize(const void* src, size_t srcSize);
size_t sparseNumDecode16_d8_outSize(const void* src, size_t srcSize);
size_t sparseNumDecode32_d8_outSize(const void* src, size_t srcSize);
size_t sparseNumDecode64_d8_outSize(const void* src, size_t srcSize);

size_t sparseNumDecode8_d8_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);
size_t sparseNumDecode16_d8_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);
size_t sparseNumDecode32_d8_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);
size_t sparseNumDecode64_d8_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ZSTRONG_BENCHMARK_UNITBENCH_SPARSE_NUM_H
