// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>

#include <cuda_runtime.h>

#include "contrib/gpu/src/common/cuda_launch.cuh"
#include "contrib/gpu/src/common/segment_plan.cuh"

namespace openzl::gpu {

// Describes one bf16 float-deconstruct chunk. All pointers are device pointers.
// The vectorized paths (bf16DeconDecodeVec and the unified decode) require
// exponent and signFrac 4-byte aligned and dst 8-byte aligned; cudaMalloc'd
// buffers and unified segment starts satisfy this by construction.
struct FloatDeconChunk {
    const uint8_t* exponent;
    const uint8_t* signFrac;
    uint16_t* dst;
    size_t nbElts;

    // Peels the first min(n, nbElts) elements off as a new segment and advances
    // *this past them, advancing device pointers only (no data copy).
    FloatDeconChunk peel(size_t n)
    {
        n                   = n < nbElts ? n : nbElts;
        FloatDeconChunk seg = *this;
        seg.nbElts          = n;
        exponent += n;
        signFrac += n;
        dst += n;
        nbElts -= n;
        return seg;
    }
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
// can report theoretical occupancy without seeing the kernel internals.
// KernelLaunchInfo is the shared descriptor from common/cuda_launch.cuh.
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

// Default segment size for the unified decode, in elements. From the
// benchmark's granularity sweep, kernel throughput keeps improving down to
// ~4Ki-element segments; 16Ki stays within a few percent of that peak on every
// shape while producing 4x fewer segments (less descriptor upload for the
// one-shot path). Tunable; must be a positive multiple of 4 (see the alignment
// note below).
constexpr size_t kUnifiedDefaultMaxSegElts = 16384;

// Prepared unified decode: element-balanced (like v2) AND vectorized (like v3),
// good across oneLarge/batched/jagged from one kernel. Construction splits
// every chunk into segments of at most maxSegElts on the host (advancing device
// pointers, zero copy) and stages the segment descriptors on the device once;
// launch() then runs a 1D-grid vectorized kernel over them. Equal-sized
// segments balance the work and the wide transactions recover bandwidth.
// Splitting once and launching repeatedly keeps the host split and the
// descriptor upload out of the per-launch cost.
//
// Takes HOST-side descriptors `chunks_h` (device pointers + sizes), since the
// split runs on the host. Uses a 1D grid, so there is NO kMaxNumInBatch cap.
// maxSegElts must be a positive multiple of 4 (segment starts are then
// 4/8-aligned by construction); the constructor throws otherwise.
class UnifiedDecodePlan {
   public:
    UnifiedDecodePlan(
            const FloatDeconChunk* chunks_h,
            uint32_t numInBatch,
            size_t maxSegElts);

    // Launches the decode on `stream`. This object must stay alive until the
    // stream work completes (it owns the device segment descriptors).
    void launch(cudaStream_t stream) const;

    uint32_t numSegments() const
    {
        return plan_.numSegs();
    }

   private:
    SegmentPlan<FloatDeconChunk> plan_;
};

// One-shot convenience: prepare a UnifiedDecodePlan and launch it on `stream`.
// Prefer UnifiedDecodePlan directly when decoding the same shape repeatedly.
void bf16DeconDecodeUnified(
        const FloatDeconChunk* chunks_h,
        uint32_t numInBatch,
        size_t maxSegElts,
        cudaStream_t stream);

KernelLaunchInfo bf16DeconDecodeUnifiedLaunchInfo();

} // namespace openzl::gpu
