// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <cuda_runtime.h>

#include "contrib/gpu/src/codecs/float_deconstruct/decode_float_deconstruct_bf16.cuh"
#include "contrib/gpu/src/common/cuda_error.cuh"
#include "contrib/gpu/src/common/cuda_raii.cuh"

// float_deconstruct-specific host-side staging for driving bf16DeconDecode from
// the benchmark and the differential test: stages a chunk batch on the device
// and owns the device memory. Header-only; both consumers are compiled by nvcc.
// Generic CUDA RAII helpers (DevicePtr, deviceAlloc, CudaEvent) live in
// common/cuda_raii.cuh.

namespace openzl::gpu {

// Non-owning view of one chunk's two host source streams; `exponent` and
// `signFrac` each point to `nbElts` bytes owned by the caller
struct HostChunk {
    const uint8_t* exponent;
    const uint8_t* signFrac;
    size_t nbElts;
};

// Owning host storage for one chunk's two source streams; pairs with the
// non-owning HostChunk view. bf16 is 1 byte/elt, so nbElts == exponent.size().
struct OwnedHostChunk {
    std::vector<uint8_t> exponent;
    std::vector<uint8_t> signFrac;
    HostChunk view() const
    {
        // Both streams must have one byte per element; DeviceChunkSet copies
        // nbElts bytes from each, so a mismatch would read past signFrac.
        assert(exponent.size() == signFrac.size());
        return { exponent.data(), signFrac.data(), exponent.size() };
    }
};

// Builds the non-owning view array DeviceChunkSet consumes from owning chunks.
inline std::vector<HostChunk> toHostChunks(
        const std::vector<OwnedHostChunk>& chunks)
{
    std::vector<HostChunk> views;
    views.reserve(chunks.size());
    for (const OwnedHostChunk& c : chunks) {
        views.push_back(c.view());
    }
    return views;
}

// Stages a batch of chunks onto the device and owns all the device memory:
// per-chunk exponent/signFrac/dst buffers plus the FloatDeconChunk descriptor
// array that bf16DeconDecode consumes. Frees everything on destruction
class DeviceChunkSet {
   public:
    explicit DeviceChunkSet(const std::vector<HostChunk>& chunks)
    {
        numInBatch_ = (uint32_t)chunks.size();
        sizes_.resize(numInBatch_);
        dExp_.resize(numInBatch_);
        dSf_.resize(numInBatch_);
        dDst_.resize(numInBatch_);

        hostChunks_.resize(numInBatch_);
        for (uint32_t c = 0; c < numInBatch_; ++c) {
            const size_t nb = chunks[c].nbElts;
            sizes_[c]       = nb;
            if (nb) {
                dExp_[c] = deviceAlloc<uint8_t>(nb);
                dSf_[c]  = deviceAlloc<uint8_t>(nb);
                dDst_[c] = deviceAlloc<uint16_t>(nb);
                ZL_CUDA_CHECK(cudaMemcpy(
                        dExp_[c].get(),
                        chunks[c].exponent,
                        nb,
                        cudaMemcpyHostToDevice));
                ZL_CUDA_CHECK(cudaMemcpy(
                        dSf_[c].get(),
                        chunks[c].signFrac,
                        nb,
                        cudaMemcpyHostToDevice));
            }
            hostChunks_[c] = {
                dExp_[c].get(), dSf_[c].get(), dDst_[c].get(), nb
            };
        }
        if (numInBatch_) {
            dChunks_ = deviceAlloc<FloatDeconChunk>(numInBatch_);
            ZL_CUDA_CHECK(cudaMemcpy(
                    dChunks_.get(),
                    hostChunks_.data(),
                    numInBatch_ * sizeof(FloatDeconChunk),
                    cudaMemcpyHostToDevice));
        }
    }

    DeviceChunkSet(const DeviceChunkSet&)            = delete;
    DeviceChunkSet& operator=(const DeviceChunkSet&) = delete;
    DeviceChunkSet(DeviceChunkSet&&)                 = delete;
    DeviceChunkSet& operator=(DeviceChunkSet&&)      = delete;

    uint32_t numInBatch() const
    {
        return numInBatch_;
    }
    const FloatDeconChunk* deviceChunks() const
    {
        return dChunks_.get();
    }

    // Host-side descriptor array (device pointers + sizes), for launchers that
    // partition on the host such as bf16DeconDecodeUnified.
    const std::vector<FloatDeconChunk>& hostChunks() const
    {
        return hostChunks_;
    }

    // Copies chunk c's decoded output back to host. The caller must have
    // synchronized the stream the decode launched on (the copy runs on the
    // default stream); otherwise the result may be stale or partial.
    std::vector<uint16_t> download(uint32_t c) const
    {
        std::vector<uint16_t> out(sizes_[c]);
        if (sizes_[c]) {
            ZL_CUDA_CHECK(cudaMemcpy(
                    out.data(),
                    dDst_[c].get(),
                    sizes_[c] * sizeof(uint16_t),
                    cudaMemcpyDeviceToHost));
        }
        return out;
    }

   private:
    uint32_t numInBatch_ = 0;
    std::vector<size_t> sizes_;
    std::vector<DevicePtr<uint8_t>> dExp_, dSf_;
    std::vector<DevicePtr<uint16_t>> dDst_;
    std::vector<FloatDeconChunk> hostChunks_;
    DevicePtr<FloatDeconChunk> dChunks_;
};

} // namespace openzl::gpu
