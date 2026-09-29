// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "contrib/gpu/src/common/cuda_error.cuh"
#include "contrib/gpu/src/common/cuda_raii.cuh"

// Splits a batch of chunks into equal-work segments on the host (advancing
// device pointers only, no copy) and stages the segment descriptors on the
// device once; the codec launches its own kernel over deviceSegments().
//
// A Chunk must expose:
//   size_t nbElts;         // elements left in this chunk
//   Chunk  peel(size_t n); // take the first min(n, nbElts) elements as a new
//                          // segment and advance *this past them (device
//                          // pointers only, no copy)

namespace openzl::gpu {

// Splits each chunk into segments of at most maxSegElts.
template <typename Chunk>
std::vector<Chunk>
rechunk(const Chunk* chunks_h, uint32_t numInBatch, size_t maxSegElts)
{
    size_t numSegs = 0;
    for (uint32_t c = 0; c < numInBatch; ++c) {
        const size_t nb = chunks_h[c].nbElts;
        numSegs += (nb + maxSegElts - 1) / maxSegElts;
    }
    if (numSegs > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(
                "SegmentPlan: segment count " + std::to_string(numSegs)
                + " exceeds uint32_t limit; use a larger maxSegElts");
    }
    std::vector<Chunk> out;
    out.reserve(numSegs);
    for (uint32_t c = 0; c < numInBatch; ++c) {
        Chunk cur = chunks_h[c];
        while (cur.nbElts > 0) {
            out.push_back(cur.peel(maxSegElts));
        }
    }
    return out;
}

// Splits `chunks_h` into segments and stages the descriptors on the device
// once; owns that memory. The codec launches its kernel over
// deviceSegments()/numSegs().
template <typename Chunk>
class SegmentPlan {
   public:
    // segAlignElts: segment sizes must be a positive multiple of this (e.g. the
    // codec's vector width, so segment starts stay aligned). 1 = no constraint.
    SegmentPlan(
            const Chunk* chunks_h,
            uint32_t numInBatch,
            size_t maxSegElts,
            size_t segAlignElts = 1)
    {
        if (maxSegElts == 0 || segAlignElts == 0
            || maxSegElts % segAlignElts != 0) {
            throw std::runtime_error(
                    "SegmentPlan: maxSegElts " + std::to_string(maxSegElts)
                    + " must be a positive multiple of "
                    + std::to_string(segAlignElts));
        }
        if (numInBatch == 0) {
            return;
        }
        const std::vector<Chunk> segs =
                rechunk(chunks_h, numInBatch, maxSegElts);
        if (segs.empty()) {
            return;
        }
        numSegs_ = (uint32_t)segs.size();
        segs_d_  = deviceAlloc<Chunk>(numSegs_);
        ZL_CUDA_CHECK(cudaMemcpy(
                segs_d_.get(),
                segs.data(),
                (size_t)numSegs_ * sizeof(Chunk),
                cudaMemcpyHostToDevice));
    }

    // Device array of segment descriptors, numSegs() long. Valid until *this is
    // destroyed.
    const Chunk* deviceSegments() const
    {
        return segs_d_.get();
    }
    uint32_t numSegs() const
    {
        return numSegs_;
    }

   private:
    DevicePtr<Chunk> segs_d_;
    uint32_t numSegs_ = 0;
};

} // namespace openzl::gpu
