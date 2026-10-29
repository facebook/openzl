// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include <cuda_runtime.h>

#include "contrib/gpu/src/common/cuda_error.cuh"

// Grid sizing, occupancy metadata, and checked launches for GPU kernels.

namespace openzl::gpu {

// A kernel's block size and its max active blocks per SM.
struct KernelLaunchInfo {
    int blockSize;
    int maxActiveBlocksPerSM;
};

// 1D grid size that fills the GPU for `kernel` launched at `blockSize` threads.
inline uint32_t fillGpuGrid(const void* kernel, int blockSize)
{
    int maxBlocksPerSM = 0;
    ZL_CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
            &maxBlocksPerSM, kernel, blockSize, 0));
    int dev    = 0;
    int numSMs = 0;
    ZL_CUDA_CHECK(cudaGetDevice(&dev));
    ZL_CUDA_CHECK(cudaDeviceGetAttribute(
            &numSMs, cudaDevAttrMultiProcessorCount, dev));
    const uint32_t grid = (uint32_t)maxBlocksPerSM * (uint32_t)numSMs;
    return grid == 0 ? 1 : grid;
}

// Occupancy metadata for `kernel` at `blockSize`: block size + max active
// blocks per SM.
inline KernelLaunchInfo launchInfoFor(const void* kernel, int blockSize)
{
    int maxActiveBlocksPerSM = 0;
    ZL_CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
            &maxActiveBlocksPerSM, kernel, blockSize, 0));
    return { blockSize, maxActiveBlocksPerSM };
}

// Launches `kernel` on `stream` and returns the status of this launch only. A
// `<<<...>>>` launch reports only through cudaGetLastError, which also returns,
// and clears, an error that the caller left pending.
template <typename... Params, typename... Args>
cudaError_t launchKernel(
        void (*kernel)(Params...),
        dim3 grid,
        dim3 block,
        size_t sharedMemBytes,
        cudaStream_t stream,
        Args&&... args)
{
    cudaLaunchConfig_t config{};
    config.gridDim          = grid;
    config.blockDim         = block;
    config.dynamicSmemBytes = sharedMemBytes;
    config.stream           = stream;
    return cudaLaunchKernelEx(&config, kernel, std::forward<Args>(args)...);
}

} // namespace openzl::gpu
