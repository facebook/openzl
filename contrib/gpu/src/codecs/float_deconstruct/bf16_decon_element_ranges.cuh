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

// Staging for the bf16 benchmark and the differential test, which call
// bf16DeconDecode directly. DeviceBf16DeconElementRanges copies host element
// ranges to the device, owns the device memory, and gives one
// Bf16DeconElementRange for each. Header-only; nvcc compiles both consumers.
// Generic CUDA RAII helpers (DevicePtr, deviceAlloc, CudaEvent) live in
// common/cuda_raii.cuh.

namespace openzl::gpu {

// Non-owning host element range: `exponent` and `signFrac` each point to
// `nbElts` bytes owned by the caller.
struct HostBf16DeconElementRange {
    const uint8_t* exponent;
    const uint8_t* signFrac;
    size_t nbElts;
};

// Owning host element range; pairs with the non-owning
// HostBf16DeconElementRange view. bf16 is 1 byte/elt, so nbElts ==
// exponent.size().
struct OwnedHostBf16DeconElementRange {
    std::vector<uint8_t> exponent;
    std::vector<uint8_t> signFrac;
    HostBf16DeconElementRange view() const
    {
        // Both streams must have one byte per element;
        // DeviceBf16DeconElementRanges copies nbElts bytes from each, so a
        // mismatch would read past signFrac.
        assert(exponent.size() == signFrac.size());
        return { exponent.data(), signFrac.data(), exponent.size() };
    }
};

// Builds the non-owning view array DeviceBf16DeconElementRanges consumes from
// owning ranges.
inline std::vector<HostBf16DeconElementRange> toHostElementRanges(
        const std::vector<OwnedHostBf16DeconElementRange>& ranges)
{
    std::vector<HostBf16DeconElementRange> views;
    views.reserve(ranges.size());
    for (const OwnedHostBf16DeconElementRange& c : ranges) {
        views.push_back(c.view());
    }
    return views;
}

// Where DeviceBf16DeconElementRanges places each range's buffers inside their
// allocations, so that tests and benchmarks can decode misaligned addresses.
// Zero offsets give the alignment of cudaMalloc.
struct ElementRangeOffsets {
    size_t exponentBytes = 0;
    size_t signFracBytes = 0;
    size_t dstElts       = 0;
};

// Stages a batch of ranges onto the device and owns all the device memory: the
// per-range exponent/signFrac/dst buffers that the Bf16DeconElementRange
// descriptors point into. Frees everything on destruction.
class DeviceBf16DeconElementRanges {
   public:
    explicit DeviceBf16DeconElementRanges(
            const std::vector<HostBf16DeconElementRange>& ranges,
            ElementRangeOffsets offsets = {})
            : offsets_(offsets)
    {
        numRanges_ = (uint32_t)ranges.size();
        sizes_.resize(numRanges_);
        dExp_.resize(numRanges_);
        dSf_.resize(numRanges_);
        dDst_.resize(numRanges_);

        ranges_.resize(numRanges_);
        for (uint32_t c = 0; c < numRanges_; ++c) {
            const size_t nb = ranges[c].nbElts;
            sizes_[c]       = nb;
            if (nb) {
                dExp_[c] = deviceAlloc<uint8_t>(nb + offsets.exponentBytes);
                dSf_[c]  = deviceAlloc<uint8_t>(nb + offsets.signFracBytes);
                dDst_[c] = deviceAlloc<uint16_t>(nb + offsets.dstElts);
                ZL_CUDA_CHECK(cudaMemcpy(
                        dExp_[c].get() + offsets.exponentBytes,
                        ranges[c].exponent,
                        nb,
                        cudaMemcpyHostToDevice));
                ZL_CUDA_CHECK(cudaMemcpy(
                        dSf_[c].get() + offsets.signFracBytes,
                        ranges[c].signFrac,
                        nb,
                        cudaMemcpyHostToDevice));
                ranges_[c] = { dExp_[c].get() + offsets.exponentBytes,
                               dSf_[c].get() + offsets.signFracBytes,
                               dDst_[c].get() + offsets.dstElts,
                               nb };
            } else {
                ranges_[c] = { nullptr, nullptr, nullptr, 0 };
            }
        }
    }

    DeviceBf16DeconElementRanges(const DeviceBf16DeconElementRanges&) = delete;
    DeviceBf16DeconElementRanges& operator=(
            const DeviceBf16DeconElementRanges&)                 = delete;
    DeviceBf16DeconElementRanges(DeviceBf16DeconElementRanges&&) = delete;
    DeviceBf16DeconElementRanges& operator=(DeviceBf16DeconElementRanges&&) =
            delete;

    uint32_t numRanges() const
    {
        return numRanges_;
    }

    // Host-side descriptor array (device pointers + sizes), which
    // bf16DeconDecode and Bf16DecodePlan split into slices on the host.
    const std::vector<Bf16DeconElementRange>& ranges() const
    {
        return ranges_;
    }

    // Copies range c's decoded output back to host. The caller must have
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
    ElementRangeOffsets offsets_;
    uint32_t numRanges_ = 0;
    std::vector<size_t> sizes_;
    std::vector<DevicePtr<uint8_t>> dExp_, dSf_;
    std::vector<DevicePtr<uint16_t>> dDst_;
    std::vector<Bf16DeconElementRange> ranges_;
};

} // namespace openzl::gpu
