// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <memory>

#include <cuda_runtime.h>

#include "openzl/dev/contrib/gpu/source/common/cuda_error.cuh"

// Generic CUDA RAII helpers for GPU host code: owning device allocations and a
// timing event. Header-only, codec-agnostic; shared by the device-staging
// harness and the benchmark driver.

namespace openzl::gpu {

// Deleter for owning device allocations via std::unique_ptr
struct CudaFreeDeleter {
    void operator()(void* p) const noexcept
    {
        cudaFree(p);
    }
};

template <typename T>
using DevicePtr = std::unique_ptr<T, CudaFreeDeleter>;

// Allocates `n` T's on the device, returning an owning pointer so the memory is
// freed even if a later allocation in the same scope throws
template <typename T>
inline DevicePtr<T> deviceAlloc(size_t n)
{
    T* p = nullptr;
    ZL_CUDA_CHECK(cudaMalloc(&p, n * sizeof(T)));
    return DevicePtr<T>(p);
}

// A CUDA timing event. Record one at the start and one at the end of a stream
// region, then call elapsedMsSince to read the elapsed time in milliseconds.
class CudaEvent {
   public:
    CudaEvent()
    {
        ZL_CUDA_CHECK(cudaEventCreate(&ev_));
    }
    ~CudaEvent()
    {
        cudaEventDestroy(ev_);
    }

    CudaEvent(const CudaEvent&)            = delete;
    CudaEvent& operator=(const CudaEvent&) = delete;

    void record(cudaStream_t stream = 0)
    {
        ZL_CUDA_CHECK(cudaEventRecord(ev_, stream));
    }

    // Milliseconds from `from` to this event (both must have been recorded).
    float elapsedMsSince(const CudaEvent& from) const
    {
        float ms = 0.f;
        ZL_CUDA_CHECK(cudaEventElapsedTime(&ms, from.ev_, ev_));
        return ms;
    }

   private:
    cudaEvent_t ev_{};
};

} // namespace openzl::gpu
