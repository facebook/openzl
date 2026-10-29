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

// Where DeviceChunkSet places each chunk's buffers inside their allocations,
// so that tests and benchmarks can decode misaligned addresses. Zero offsets
// give the alignment of cudaMalloc.
struct ChunkOffsets {
    size_t exponentBytes = 0;
    size_t signFracBytes = 0;
    size_t dstElts       = 0;
};

// Stages a batch of chunks onto the device and owns all the device memory: the
// per-chunk exponent/signFrac/dst buffers that the FloatDeconChunk descriptors
// point into. Frees everything on destruction.
class DeviceChunkSet {
   public:
    explicit DeviceChunkSet(
            const std::vector<HostChunk>& chunks,
            ChunkOffsets offsets = {})
            : offsets_(offsets)
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
                dExp_[c] = deviceAlloc<uint8_t>(nb + offsets.exponentBytes);
                dSf_[c]  = deviceAlloc<uint8_t>(nb + offsets.signFracBytes);
                dDst_[c] = deviceAlloc<uint16_t>(nb + offsets.dstElts);
                ZL_CUDA_CHECK(cudaMemcpy(
                        dExp_[c].get() + offsets.exponentBytes,
                        chunks[c].exponent,
                        nb,
                        cudaMemcpyHostToDevice));
                ZL_CUDA_CHECK(cudaMemcpy(
                        dSf_[c].get() + offsets.signFracBytes,
                        chunks[c].signFrac,
                        nb,
                        cudaMemcpyHostToDevice));
                hostChunks_[c] = { dExp_[c].get() + offsets.exponentBytes,
                                   dSf_[c].get() + offsets.signFracBytes,
                                   dDst_[c].get() + offsets.dstElts,
                                   nb };
            } else {
                hostChunks_[c] = { nullptr, nullptr, nullptr, 0 };
            }
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

    // Host-side descriptor array (device pointers + sizes), which
    // bf16DeconDecode and Bf16DecodePlan split into segments on the host.
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
                    dDst_[c].get() + offsets_.dstElts,
                    sizes_[c] * sizeof(uint16_t),
                    cudaMemcpyDeviceToHost));
        }
        return out;
    }

   private:
    ChunkOffsets offsets_;
    uint32_t numInBatch_ = 0;
    std::vector<size_t> sizes_;
    std::vector<DevicePtr<uint8_t>> dExp_, dSf_;
    std::vector<DevicePtr<uint16_t>> dDst_;
    std::vector<FloatDeconChunk> hostChunks_;
};

} // namespace openzl::gpu
