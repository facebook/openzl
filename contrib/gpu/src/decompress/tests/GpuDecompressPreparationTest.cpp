// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "contrib/gpu/src/common/cuda_error.cuh"
#include "contrib/gpu/src/common/cuda_raii.cuh"
#include "contrib/gpu/src/decompress/gpu_decompress.hpp"
#include "contrib/gpu/testkit/frame_verifier.h"
#include "contrib/gpu/testkit/multichunk_frame.h"
#include "openzl/common/errors_internal.h"
#include "openzl/cpp/poly/Span.hpp"
#include "openzl/decompress/dctx2.h"
#include "openzl/openzl.hpp"

namespace openzl {
namespace tests {
namespace {

constexpr size_t kEltsPerChunk = 20000;

std::string makeTwoChunkFrame()
{
    std::vector<int32_t> values(2 * kEltsPerChunk);
    for (size_t i = 0; i < kEltsPerChunk; ++i) {
        values[i]                 = static_cast<int32_t>(i % 8);
        values[kEltsPerChunk + i] = static_cast<int32_t>(i) * 3;
    }

    const Input input =
            Input::refNumeric<int32_t>(values.data(), values.size());
    Compressor compressor;
    const GraphID bitpack      = graphs::Bitpack{}();
    const GraphID deltaToStore = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor.get(), nodes::DeltaInt::node, graphs::Store{}());
    return gpu::testkit::makeMultiChunkFrame(
            compressor,
            {
                    gpu::testkit::ChunkSpec{ kEltsPerChunk, bitpack },
                    gpu::testkit::ChunkSpec{ kEltsPerChunk, deltaToStore },
            },
            input);
}

poly::span<const std::byte> bytes(const std::string& frame)
{
    return {
        reinterpret_cast<const std::byte*>(frame.data()),
        frame.size(),
    };
}

TEST(GPUDecompressPreparationTest, BuildsContextsWithoutReadingPayload)
{
    // This fails if preparation reads payload/checksum bytes on the CPU, loses
    // decoded graph metadata, stages the wrong transform-header bytes, or binds
    // stored streams outside its device chunk.
    const std::string frame = makeTwoChunkFrame();
    const std::vector<std::vector<uint32_t>> codecsPerChunk =
            gpu::testkit::standardCodecsPerChunk(frame);
    gpu::DevicePtr<std::byte> deviceFrame_d =
            gpu::deviceAlloc<std::byte>(frame.size());
    ZL_CUDA_CHECK(cudaMemcpy(
            deviceFrame_d.get(),
            frame.data(),
            frame.size(),
            cudaMemcpyHostToDevice));

    std::vector<gpu::GPUChunk> chunks;
    std::vector<gpu::GPUFrameHeaderForChunks> frameHeaders;
    ZL_Report const directoryResult = gpu::collectGPUChunksFromFrame(
            deviceFrame_d.get(), bytes(frame), chunks, frameHeaders);
    ASSERT_FALSE(ZL_isError(directoryResult));
    ASSERT_EQ(chunks.size(), codecsPerChunk.size());
    ASSERT_FALSE(chunks.empty());
    ASSERT_LT(chunks[0].chunkHeaderSize, chunks[0].chunkSize);

    auto* const payloadOrChecksum_d =
            static_cast<std::byte*>(const_cast<void*>(chunks[0].chunk_d))
            + chunks[0].chunkSize - 1;
    ZL_CUDA_CHECK(cudaMemset(payloadOrChecksum_d, 0xA5, 1));

    const gpu::Result<std::vector<gpu::PreparedGPUChunk>> prepareResult =
            gpu::prepareChunksForPlanning(frameHeaders, chunks, nullptr);
    ASSERT_FALSE(prepareResult.isError());
    const std::vector<gpu::PreparedGPUChunk>& preparedChunks =
            prepareResult.value();
    ASSERT_EQ(preparedChunks.size(), chunks.size());

    for (size_t i = 0; i < preparedChunks.size(); ++i) {
        const gpu::PreparedGPUChunk& prepared = preparedChunks[i];
        EXPECT_EQ(prepared.chunk.frameHeaderIdx, chunks[i].frameHeaderIdx);
        EXPECT_EQ(prepared.chunk.chunk_d, chunks[i].chunk_d);
        EXPECT_EQ(prepared.chunk.chunkSize, chunks[i].chunkSize);
        EXPECT_EQ(prepared.chunk.chunkHeaderSize, chunks[i].chunkHeaderSize);
        EXPECT_EQ(
                ZL_DCtx_getParameter(
                        prepared.dctx.get(), ZL_DParam_enableCodecFusion),
                ZL_TernaryParam_disable);

        const DFH_Struct* const frameHeader =
                DCtx_getFrameHeader(prepared.dctx.get());
        ASSERT_NE(frameHeader, nullptr);
        EXPECT_EQ(VECTOR_SIZE(frameHeader->nodes), codecsPerChunk[i].size());

        const uintptr_t deviceFrameBegin_d =
                reinterpret_cast<uintptr_t>(deviceFrame_d.get());
        const uintptr_t chunkBegin_d =
                reinterpret_cast<uintptr_t>(prepared.chunk.chunk_d);
        ASSERT_GE(chunkBegin_d, deviceFrameBegin_d);
        const size_t chunkOffset = chunkBegin_d - deviceFrameBegin_d;
        const size_t transformHeaderOffset =
                chunkOffset + prepared.chunk.chunkHeaderSize;
        ASSERT_LE(transformHeaderOffset, frame.size());
        ASSERT_LE(
                frameHeader->totalTHSize, frame.size() - transformHeaderOffset);
        const auto* const frameBytes =
                reinterpret_cast<const std::byte*>(frame.data());
        const std::vector<std::byte> expectedTransformHeaders{
            frameBytes + transformHeaderOffset,
            frameBytes + transformHeaderOffset + frameHeader->totalTHSize,
        };
        EXPECT_EQ(prepared.transformHeaders_h, expectedTransformHeaders);

        const uintptr_t payloadBegin_d = chunkBegin_d
                + prepared.chunk.chunkHeaderSize + frameHeader->totalTHSize;
        const uintptr_t chunkEnd_d = chunkBegin_d + prepared.chunk.chunkSize;
        size_t boundStreamCount    = 0;
        for (size_t streamIdx = 0;
             streamIdx < ZL_DCtx_getNumStreams(prepared.dctx.get());
             ++streamIdx) {
            const ZL_Data* const stream = ZL_DCtx_getConstStream(
                    prepared.dctx.get(), static_cast<ZL_IDType>(streamIdx));
            if (stream == nullptr || ZL_Data_rPtr(stream) == nullptr) {
                continue;
            }

            const uintptr_t streamBegin_d =
                    reinterpret_cast<uintptr_t>(ZL_Data_rPtr(stream));
            EXPECT_GE(streamBegin_d, payloadBegin_d);
            ASSERT_LE(streamBegin_d, chunkEnd_d);
            EXPECT_LE(ZL_Data_contentSize(stream), chunkEnd_d - streamBegin_d);
            ++boundStreamCount;
        }
        EXPECT_GT(boundStreamCount, 0);
    }
}

TEST(GPUDecompressPreparationTest, RejectsNullFrameHeader)
{
    // The device-to-host copy also rejects a null source with the same code, so
    // the message must show that the descriptor check fired first.
    const gpu::GPUFrameHeaderForChunks invalidFrameHeader{
        .frameHeader_d   = nullptr,
        .frameHeaderSize = 1,
    };

    const gpu::Result<std::vector<gpu::PreparedGPUChunk>> result =
            gpu::prepareChunksForPlanning(
                    poly::span<const gpu::GPUFrameHeaderForChunks>{
                            &invalidFrameHeader, 1 },
                    poly::span<const gpu::GPUChunk>{},
                    nullptr);

    ASSERT_TRUE(result.isError());
    const ZL_Report report = result.report();
    EXPECT_EQ(ZL_errorCode(report), ZL_ErrorCode_parameter_invalid);
    const std::string message{ ZL_E_str(ZL_RES_error(report)) };
    EXPECT_NE(
            message.find("frameHeader.frameHeader_d == nullptr"),
            std::string::npos)
            << message;
}

gpu::DevicePtr<std::byte> copyToDevice(const std::string& frame)
{
    gpu::DevicePtr<std::byte> frame_d =
            gpu::deviceAlloc<std::byte>(frame.size());
    ZL_CUDA_CHECK(cudaMemcpy(
            frame_d.get(), frame.data(), frame.size(), cudaMemcpyHostToDevice));
    return frame_d;
}

TEST(GPUDecompressPreparationTest, RejectsTruncatedFrameHeader)
{
    // This fails if a frame-header descriptor that is too short to parse
    // throws instead of returning corruption.
    const std::string frame                  = makeTwoChunkFrame();
    const gpu::DevicePtr<std::byte> frame_d  = copyToDevice(frame);
    const gpu::GPUFrameHeaderForChunks magic = {
        .frameHeader_d   = frame_d.get(),
        .frameHeaderSize = 4,
    };

    const gpu::Result<std::vector<gpu::PreparedGPUChunk>> result =
            gpu::prepareChunksForPlanning(
                    poly::span<const gpu::GPUFrameHeaderForChunks>{ &magic, 1 },
                    poly::span<const gpu::GPUChunk>{},
                    nullptr);

    ASSERT_TRUE(result.isError());
    EXPECT_EQ(ZL_errorCode(result.report()), ZL_ErrorCode_corruption);
}

TEST(GPUDecompressPreparationTest, DropsPlanningErrorInfo)
{
    // Chunk 0 uses Bitpack, which has no GPU planner. The planning error keeps
    // its info in that chunk's DCTX, which decompressChunks frees before it
    // returns. This fails if the C++ or the C API returns an error that still
    // points into that DCTX.
    const std::string frame                 = makeTwoChunkFrame();
    const gpu::DevicePtr<std::byte> frame_d = copyToDevice(frame);
    std::vector<gpu::GPUChunk> chunks;
    std::vector<gpu::GPUFrameHeaderForChunks> frameHeaders;
    ASSERT_FALSE(ZL_isError(
            gpu::collectGPUChunksFromFrame(
                    frame_d.get(), bytes(frame), chunks, frameHeaders)));

    ZL_Report const result =
            gpu::decompressChunks(nullptr, 0, frameHeaders, chunks, nullptr);
    ZL_Report const cResult =
            ZL_GPU_decompress(nullptr, 0, frame_d.get(), frame.size(), nullptr);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_temporaryLibraryLimitation);
    EXPECT_EQ(ZL_E_dy(ZL_RES_error(result)), nullptr);
    EXPECT_EQ(ZL_errorCode(cResult), ZL_ErrorCode_temporaryLibraryLimitation);
    EXPECT_EQ(ZL_E_dy(ZL_RES_error(cResult)), nullptr);
}

} // namespace
} // namespace tests
} // namespace openzl
