// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <cuda_runtime.h>

#include "contrib/gpu/src/common/cuda_error.cuh"
#include "contrib/gpu/src/common/cuda_raii.cuh"
#include "contrib/gpu/src/device/cuda_device.hpp"

namespace openzl::gpu {
namespace {

struct PinnedFree {
    void operator()(std::byte* ptr_h) const noexcept
    {
        (void)cudaFreeHost(ptr_h);
    }
};

struct StreamDestroy {
    void operator()(cudaStream_t stream) const noexcept
    {
        (void)cudaStreamDestroy(stream);
    }
};

/// Keeps a stream busy, so work enqueued behind it has not run when the call
/// that enqueued it returns.
void CUDART_CB holdStream(void*)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

int currentDevice()
{
    int device = 0;
    ZL_CUDA_CHECK(cudaGetDevice(&device));
    return device;
}

TEST(CudaDeviceTest, CopiesEveryNonEmptyRegion)
{
    // The destination is pinned and the stream is busy, so each copy runs
    // after the call enqueues it. This fails if a region is skipped or copied
    // from the wrong offset, or if the call returns before the copies finish.
    const std::vector<std::byte> expected{
        std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 }
    };
    const DevicePtr<std::byte> source_d = deviceAlloc<std::byte>(4);
    ZL_CUDA_CHECK(cudaMemcpy(
            source_d.get(), expected.data(), 4, cudaMemcpyHostToDevice));
    std::byte* pinned_h = nullptr;
    ZL_CUDA_CHECK(cudaMallocHost(&pinned_h, 4));
    const std::unique_ptr<std::byte, PinnedFree> actual_h{ pinned_h };
    std::memset(actual_h.get(), 0, 4);
    const poly::span<std::byte> actual{ actual_h.get(), 4 };
    const std::vector<DeviceToHostCopy> copies{
        { .dst_h = actual.subspan(0, 1), .src_d = source_d.get() },
        { .dst_h = {}, .src_d = nullptr },
        { .dst_h = actual.subspan(1), .src_d = source_d.get() + 1 },
    };
    cudaStream_t native = nullptr;
    ZL_CUDA_CHECK(cudaStreamCreateWithFlags(&native, cudaStreamNonBlocking));
    const std::unique_ptr<CUstream_st, StreamDestroy> streamOwner{ native };
    const std::unique_ptr<Device> device = makeCurrentCudaDevice();
    ASSERT_NE(device, nullptr);
    std::optional<Stream> stream;
    ASSERT_FALSE(ZL_isError(device->wrapStream(native, stream)));
    ZL_CUDA_CHECK(cudaLaunchHostFunc(native, holdStream, nullptr));

    ZL_Report const result = stream->copyToHostAndSynchronize(copies);

    ASSERT_FALSE(ZL_isError(result));
    EXPECT_EQ(std::vector<std::byte>(actual.begin(), actual.end()), expected);
}

TEST(CudaDeviceTest, RejectsCodecsThatItDoesNotImplement)
{
    // No runtime implements custom transforms, and the CUDA device has no
    // constant decoder. This fails if the device chooses a kernel for a run
    // that it cannot decode, or changes the caller's current device.
    const int callerDevice               = currentDevice();
    const std::unique_ptr<Device> device = makeCurrentCudaDevice();
    ASSERT_NE(device, nullptr);
    for (const PublicTransformInfo transform :
         { PublicTransformInfo{ trt_custom, 0 },
           PublicTransformInfo{ trt_standard,
                                ZL_StandardTransformID_constant_serial } }) {
        CodecRunInfo run;
        run.transform = transform;
        std::vector<DecodeBatch> batches;

        ZL_Report const result = device->chooseDecodeBatches(
                poly::span<const CodecRunInfo>{ &run, 1 }, batches);

        EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_temporaryLibraryLimitation)
                << transform.trid;
    }
    EXPECT_EQ(currentDevice(), callerDevice);
}

ZL_Report failWithoutCudaCall(poly::span<const BoundCodecRun>, void*)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    ZL_ERR(transform_executionFailure, "injected failure");
}

TEST(CudaDeviceTest, KeepsAnErrorThatTheCallerLeftPending)
{
    // The kernel fails without a CUDA call. This fails if the device clears an
    // error that it did not cause. A dev build links a separate CUDA runtime,
    // with its own pending error, into each library, so only a build with one
    // runtime, such as opt, can catch that.
    const DecodeKernel failing{ "failing", nullptr, failWithoutCudaCall };
    const BoundCodecRun run{};
    const std::unique_ptr<Device> device = makeCurrentCudaDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_EQ(cudaSetDevice(-1), cudaErrorInvalidDevice);

    ZL_Report const result = device->defaultStream().decodeBatch(
            failing, poly::span<const BoundCodecRun>{ &run, 1 });

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_transform_executionFailure);
    EXPECT_EQ(cudaGetLastError(), cudaErrorInvalidDevice);
}

TEST(CudaDeviceTest, WrapsAStreamOfTheCurrentDevice)
{
    // This fails if the device rejects a stream that belongs to the current
    // device, or wraps a different handle.
    const std::unique_ptr<Device> device = makeCurrentCudaDevice();
    ASSERT_NE(device, nullptr);
    cudaStream_t native = nullptr;
    ZL_CUDA_CHECK(cudaStreamCreate(&native));
    std::optional<Stream> stream;

    ZL_Report const result = device->wrapStream(native, stream);

    EXPECT_FALSE(ZL_isError(result));
    ASSERT_TRUE(stream.has_value());
    EXPECT_EQ(stream->native(), static_cast<void*>(native));
    EXPECT_EQ(&stream->device(), device.get());
    ZL_CUDA_CHECK(cudaStreamDestroy(native));
}

TEST(CudaDeviceTest, WrapsTheSpecialDefaultStreams)
{
    // Callers may pass the per-thread or the legacy default stream. This fails
    // if the device check rejects either handle.
    const std::unique_ptr<Device> device = makeCurrentCudaDevice();
    ASSERT_NE(device, nullptr);

    for (cudaStream_t const native :
         { cudaStreamPerThread, cudaStreamLegacy }) {
        std::optional<Stream> stream;
        ZL_Report const result = device->wrapStream(native, stream);
        EXPECT_FALSE(ZL_isError(result))
                << ZL_ErrorCode_toString(ZL_errorCode(result));
        EXPECT_TRUE(stream.has_value());
    }
}

} // namespace
} // namespace openzl::gpu
