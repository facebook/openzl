// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>

#include <cuda_runtime.h>

namespace openzl::gpu {

// Describes one bf16 float-deconstruct chunk. All pointers are device pointers.
// bf16DeconDecodeVec needs them aligned (see its comment).
struct FloatDeconChunk {
    const uint8_t* exponent;
    const uint8_t* signFrac;
    uint16_t* dst;
    size_t nbElts;
};

// Canonical single element decode
__host__ __device__ __forceinline__ uint16_t
decodeBf16Elt(uint8_t exponent, uint8_t signFrac)
{
    const uint16_t sign  = (uint16_t)(signFrac << 15);
    const uint16_t expnt = (uint16_t)(exponent << 7);
    const uint16_t frac  = (uint16_t)(signFrac >> 1);
    return sign | expnt | frac;
}

// Maximum chunks per bf16DeconDecode call. numInBatch maps to gridDim.y, which
// CUDA caps at 65535; a caller (or binding) with more chunks must split the
// batch across multiple calls.
constexpr uint32_t kMaxNumInBatch = 65535;

// Decodes `numInBatch` bf16 float-deconstruct chunks, writing each chunk's
// result into its own `dst`. Throws std::runtime_error if numInBatch exceeds
// kMaxNumInBatch (the gridDim.y limit).
void bf16DeconDecode(
        uint32_t numInBatch,
        const FloatDeconChunk* chunks_d,
        cudaStream_t stream);

// Occupancy/launch metadata for the main decode kernel, so a benchmark harness
// can report theoretical occupancy without seeing the kernel internals
struct KernelLaunchInfo {
    int blockSize;
    int maxActiveBlocksPerSM;
};
KernelLaunchInfo bf16DeconDecodeLaunchInfo();

// v2 decode: tiled and load-balanced across chunks (fixes the jagged case).
// Same contract as bf16DeconDecode.
void bf16DeconDecodeV2(
        uint32_t numInBatch,
        const FloatDeconChunk* chunks_d,
        cudaStream_t stream);
KernelLaunchInfo bf16DeconDecodeV2LaunchInfo();

// v3 decode: vectorized per-chunk (uchar4 in, ushort4 out). Faster on the
// single/uniform shapes; use v2 for jagged. This reads exponent/signFrac as
// uchar4 and writes dst as ushort4, so exponent/signFrac must be 4-byte aligned
// and dst 8-byte aligned. cudaMalloc'd buffers already are.
void bf16DeconDecodeVec(
        uint32_t numInBatch,
        const FloatDeconChunk* chunks_d,
        cudaStream_t stream);
KernelLaunchInfo bf16DeconDecodeVecLaunchInfo();

} // namespace openzl::gpu
