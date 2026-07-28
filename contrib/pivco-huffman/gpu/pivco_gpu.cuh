// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <cuda_runtime.h>

#include "contrib/pivco-huffman/gpu/pivco_gpu.h"

struct PivCoGpuStatus {
    unsigned code;
    uint64_t detail;
};

enum PivCoGpuStatusCode : unsigned {
    PIVCO_GPU_STATUS_OK             = 0,
    PIVCO_GPU_STATUS_PARAMETER      = 1,
    PIVCO_GPU_STATUS_CORRUPTION     = 2,
    PIVCO_GPU_STATUS_CAPACITY       = 3,
    PIVCO_GPU_STATUS_MISSING_SYMBOL = 4,
};

cudaError_t pivcoGpuEncodeAsync(
        const PivCoGpuContext* context,
        void* dst_d,
        size_t dstCapacity,
        uint64_t* offsets_d,
        size_t offsetsCapacity,
        const void* src_d,
        size_t srcSize,
        size_t blockSize,
        void* workspace_d,
        size_t workspaceBytes,
        PivCoGpuStatus* status_d,
        uint64_t* totalSize_d,
        cudaStream_t stream);

cudaError_t pivcoGpuDecodeAsync(
        const PivCoGpuContext* context,
        void* dst_d,
        size_t dstSize,
        const void* bitstream_d,
        size_t bitstreamSize,
        const uint64_t* offsets_d,
        size_t offsetsCount,
        size_t blockSize,
        void* workspace_d,
        size_t workspaceBytes,
        PivCoGpuStatus* status_d,
        cudaStream_t stream);
