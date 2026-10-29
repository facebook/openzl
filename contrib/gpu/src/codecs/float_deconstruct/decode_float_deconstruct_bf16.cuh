// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>

#include <cuda_runtime.h>

#include "contrib/gpu/src/codecs/float_deconstruct/float_deconstruct_bf16.hpp"
#include "contrib/gpu/src/common/cuda_launch.cuh"
#include "contrib/gpu/src/common/segment_plan.cuh"

namespace openzl::gpu {

// Default segment size, in elements. From the benchmark's granularity sweep,
// kernel throughput keeps improving down to ~4Ki-element segments; 16Ki stays
// within a few percent of that peak on every shape while producing 4x fewer
// segments (less descriptor upload for the one-shot path). Tunable; must be a
// positive multiple of 4 (see Bf16DecodePlan).
constexpr size_t kBf16DefaultMaxSegElts = 16384;

// Prepared bf16 float_deconstruct decode. Construction splits every chunk into
// segments of at most maxSegElts on the host (advancing device pointers, zero
// copy) and stages the segment descriptors on the device once; launch() then
// runs one kernel over them. Equal-sized segments balance the work across
// chunks of any size. Splitting once and launching repeatedly keeps the host
// split and the descriptor upload out of the per-launch cost.
//
// Takes HOST-side descriptors `chunks_h` (device pointers + sizes), since the
// split runs on the host. Each dst must be 2-byte aligned. The kernel decodes
// a segment with vector loads and stores (uchar4 in, ushort4 out) when its
// exponent and signFrac are 4-byte aligned and its dst is 8-byte aligned, and
// with element loads and stores otherwise. maxSegElts must be a positive
// multiple of 4, so every segment of a chunk has the alignment of the chunk's
// start; the constructor throws otherwise.
class Bf16DecodePlan {
   public:
    Bf16DecodePlan(
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

// One-shot convenience: prepare a Bf16DecodePlan and launch it on `stream`.
// Prefer Bf16DecodePlan directly when decoding the same shape repeatedly.
void bf16DeconDecode(
        const FloatDeconChunk* chunks_h,
        uint32_t numInBatch,
        size_t maxSegElts,
        cudaStream_t stream);

// Occupancy/launch metadata for the decode kernel, so a benchmark harness can
// report theoretical occupancy without seeing the kernel internals.
// KernelLaunchInfo is the shared descriptor from common/cuda_launch.cuh.
KernelLaunchInfo bf16DeconDecodeLaunchInfo();

} // namespace openzl::gpu
