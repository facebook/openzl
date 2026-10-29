// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/device/cuda_device.hpp"

#include <cuda_runtime_api.h>

#include <memory>
#include <new>

#include "contrib/gpu/src/device/cuda_kernel_table.hpp"
#include "openzl/common/errors_internal.h"

static_assert(
        CUDART_VERSION >= 12080,
        "The CUDA device requires CUDA 12.8 or newer");

namespace openzl::gpu {
namespace {

/**
 * Clears the runtime's per-thread last error after a failed call. The failure
 * reaches the caller through a @c ZL_Report, and a stale last error would make
 * the caller's own @c cudaGetLastError check report it again.
 */
void clearLastError()
{
    (void)cudaGetLastError();
}

ZL_Report copyToHostOnCurrentDevice(
        poly::span<const DeviceToHostCopy> copies,
        cudaStream_t stream)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);

    bool copyEnqueued = false;
    for (const DeviceToHostCopy& copy : copies) {
        if (copy.dst_h.empty()) {
            continue;
        }
        cudaError_t const result = cudaMemcpyAsync(
                copy.dst_h.data(),
                copy.src_d,
                copy.dst_h.size(),
                cudaMemcpyDeviceToHost,
                stream);
        if (result != cudaSuccess) {
            if (copyEnqueued) {
                // Destinations must outlive any copies queued before failure.
                (void)cudaStreamSynchronize(stream);
            }
            clearLastError();
            ZL_ERR(GENERIC,
                   "cudaMemcpyAsync failed: %s",
                   cudaGetErrorString(result));
        }
        copyEnqueued = true;
    }
    if (copyEnqueued) {
        cudaError_t const result = cudaStreamSynchronize(stream);
        if (result != cudaSuccess) {
            clearLastError();
            ZL_ERR(GENERIC,
                   "cudaStreamSynchronize failed: %s",
                   cudaGetErrorString(result));
        }
    }
    return ZL_returnSuccess();
}

class CudaDevice final : public Device {
   public:
    CudaDevice() : Device(cudaKernelTable()) {}

   protected:
    ZL_Report doCheckStream(void* native) override
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
        int streamDevice         = 0;
        int currentDevice        = 0;
        cudaError_t const result = cudaStreamGetDevice(
                static_cast<cudaStream_t>(native), &streamDevice);
        if (result != cudaSuccess) {
            clearLastError();
            // Other failures, such as a driver older than CUDA 12.8, are not
            // the caller's stream.
            ZL_ERR_IF(
                    result == cudaErrorInvalidResourceHandle
                            || result == cudaErrorInvalidValue,
                    parameter_invalid,
                    "The stream is not a valid CUDA stream: %s",
                    cudaGetErrorString(result));
            ZL_ERR(GENERIC,
                   "cudaStreamGetDevice failed: %s",
                   cudaGetErrorString(result));
        }
        if (cudaGetDevice(&currentDevice) != cudaSuccess) {
            clearLastError();
            ZL_ERR(GENERIC, "cudaGetDevice failed");
        }
        ZL_ERR_IF_NE(
                streamDevice,
                currentDevice,
                parameter_invalid,
                "The stream belongs to device %d, but device %d is current",
                streamDevice,
                currentDevice);
        return ZL_returnSuccess();
    }

    ZL_Report doCopyToHostAndSynchronize(
            poly::span<const DeviceToHostCopy> copies,
            void* native) override
    {
        return copyToHostOnCurrentDevice(
                copies, static_cast<cudaStream_t>(native));
    }

    ZL_Report doDecodeBatch(
            const DecodeKernel& kernel,
            poly::span<const BoundCodecRun> runs,
            void* native) override
    {
        // Clears only an error that the batch set. An error that the caller
        // left pending before the call stays for the caller.
        cudaError_t const pending = cudaPeekAtLastError();
        ZL_Report const result    = Device::doDecodeBatch(kernel, runs, native);
        if (cudaPeekAtLastError() != pending) {
            clearLastError();
        }
        return result;
    }
};

} // namespace

std::unique_ptr<Device> makeCurrentCudaDevice()
{
    // The constructor builds the kernel table, which can throw.
    try {
        return std::make_unique<CudaDevice>();
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

} // namespace openzl::gpu
