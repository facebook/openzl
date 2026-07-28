// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "contrib/gpu/src/common/cuda_error.cuh"
#include "contrib/gpu/src/common/cuda_raii.cuh"
#include "contrib/gpu/src/decompress/gpu_decompress.hpp"
#include "contrib/gpu/testkit/multichunk_frame.h"
#include "openzl/openzl.hpp"

namespace openzl {
namespace tests {
namespace {

constexpr size_t kEltsPerChunk = 20000;

std::string makeTwoChunkFrame()
{
    std::vector<int32_t> values(2 * kEltsPerChunk);
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<int32_t>(i % 8);
    }

    const Input input =
            Input::refNumeric<int32_t>(values.data(), values.size());
    Compressor compressor;
    const GraphID bitpack = graphs::Bitpack{}();
    return gpu::testkit::makeMultiChunkFrame(
            compressor,
            {
                    gpu::testkit::ChunkSpec{ kEltsPerChunk, bitpack },
                    gpu::testkit::ChunkSpec{ kEltsPerChunk, bitpack },
            },
            input);
}

std::span<const std::byte> bytes(const std::string& frame)
{
    return {
        reinterpret_cast<const std::byte*>(frame.data()),
        frame.size(),
    };
}

TEST(GPUDecompressFullCopyTest, MatchesDirectHostEnumeration)
{
    // This test verifies the temporary provider copies a complete device frame
    // to host before parsing it; it fails if D2H staging changes chunk count,
    // sizes, frame associations, or device-relative pointers.
    const std::string frame = makeTwoChunkFrame();
    gpu::DevicePtr<std::byte> deviceFrame_d =
            gpu::deviceAlloc<std::byte>(frame.size());
    ZL_CUDA_CHECK(cudaMemcpy(
            deviceFrame_d.get(),
            frame.data(),
            frame.size(),
            cudaMemcpyHostToDevice));

    std::vector<gpu::GPUChunk> expected;
    std::vector<gpu::GPUFrameHeaderForChunks> expectedFrameHeaders;
    ZL_Report const expectedResult = gpu::collectGPUChunksFromFrame(
            deviceFrame_d.get(), bytes(frame), expected, expectedFrameHeaders);
    ASSERT_FALSE(ZL_isError(expectedResult));

    std::vector<gpu::GPUChunk> actual;
    std::vector<gpu::GPUFrameHeaderForChunks> actualFrameHeaders;
    ZL_Report const actualResult = gpu::collectGPUChunks(
            { deviceFrame_d.get(), frame.size() },
            nullptr,
            actual,
            actualFrameHeaders);
    ASSERT_FALSE(ZL_isError(actualResult));
    EXPECT_EQ(ZL_validResult(actualResult), ZL_validResult(expectedResult));
    ASSERT_EQ(actualFrameHeaders.size(), expectedFrameHeaders.size());
    ASSERT_EQ(actualFrameHeaders.size(), 1);
    EXPECT_EQ(
            actualFrameHeaders[0].frameHeader_d,
            expectedFrameHeaders[0].frameHeader_d);
    EXPECT_EQ(
            actualFrameHeaders[0].frameHeaderSize,
            expectedFrameHeaders[0].frameHeaderSize);
    ASSERT_EQ(actual.size(), expected.size());

    for (size_t i = 0; i < actual.size(); ++i) {
        EXPECT_EQ(actual[i].frameHeaderIdx, expected[i].frameHeaderIdx);
        EXPECT_EQ(actual[i].chunk_d, expected[i].chunk_d);
        EXPECT_EQ(actual[i].chunkSize, expected[i].chunkSize);
        EXPECT_EQ(actual[i].chunkHeaderSize, expected[i].chunkHeaderSize);
    }
}

} // namespace
} // namespace tests
} // namespace openzl
