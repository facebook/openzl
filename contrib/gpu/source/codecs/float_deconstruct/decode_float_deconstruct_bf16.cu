// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "decode_float_deconstruct_bf16.cuh"

#include <stdexcept>
#include <string>

#include "openzl/dev/contrib/gpu/source/common/cuda_error.cuh"

namespace openzl::gpu {

namespace {

constexpr int kThreads = 256;

// chunk = blockIdx.y; each block's threads grid-stride over that chunk along x.
// One chunk is just numInBatch == 1, so this handles single and multi chunk.
// gridDim.y caps numInBatch at 65535.
__global__ void decodeChunksKernel(
        const FloatDeconChunk* __restrict__ chunks,
        uint32_t numInBatch)
{
    const uint32_t b = blockIdx.y;
    if (b >= numInBatch) {
        return;
    }
    const FloatDeconChunk c = chunks[b];
    const size_t stride     = (size_t)gridDim.x * blockDim.x;
    for (size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x; i < c.nbElts;
         i += stride) {
        c.dst[i] = decodeBf16Elt(c.exponent[i], c.signFrac[i]);
    }
}

// 1D grid size that fills the GPU for a given kernel at kThreads.
uint32_t fillGpuGrid(const void* kernel)
{
    int maxBlocksPerSM = 0;
    ZL_CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
            &maxBlocksPerSM, kernel, kThreads, 0));
    int dev    = 0;
    int numSMs = 0;
    ZL_CUDA_CHECK(cudaGetDevice(&dev));
    ZL_CUDA_CHECK(cudaDeviceGetAttribute(
            &numSMs, cudaDevAttrMultiProcessorCount, dev));
    const uint32_t grid = (uint32_t)maxBlocksPerSM * (uint32_t)numSMs;
    return grid == 0 ? 1 : grid;
}

} // namespace

void bf16DeconDecode(
        uint32_t numInBatch,
        const FloatDeconChunk* chunks_d,
        cudaStream_t stream)
{
    if (numInBatch == 0) {
        return;
    }
    if (numInBatch > kMaxNumInBatch) {
        throw std::runtime_error(
                "bf16DeconDecode: numInBatch " + std::to_string(numInBatch)
                + " exceeds kMaxNumInBatch " + std::to_string(kMaxNumInBatch)
                + " (gridDim.y limit); split the batch across calls");
    }
    // Oversubscribe x so blocksPerChunk * numInBatch roughly fills the GPU;
    // grid-stride in x then covers any per-chunk size.
    const uint32_t target = fillGpuGrid((const void*)decodeChunksKernel);
    const uint32_t blocksPerChunk = (target + numInBatch - 1) / numInBatch;
    const dim3 grid(blocksPerChunk, numInBatch);
    decodeChunksKernel<<<grid, kThreads, 0, stream>>>(chunks_d, numInBatch);
    ZL_CUDA_CHECK_LAST();
}

KernelLaunchInfo bf16DeconDecodeLaunchInfo()
{
    int maxActiveBlocksPerSM = 0;
    ZL_CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
            &maxActiveBlocksPerSM,
            (const void*)decodeChunksKernel,
            kThreads,
            0));
    return { kThreads, maxActiveBlocksPerSM };
}

} // namespace openzl::gpu
