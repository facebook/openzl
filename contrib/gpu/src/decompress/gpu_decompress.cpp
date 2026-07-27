// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cuda_runtime_api.h>

#include <cstddef>
#include <new>
#include <span>
#include <string_view>
#include <vector>

#include "contrib/gpu/src/decompress/gpu_decompress.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/FrameInfo.hpp"
#include "openzl/decompress/dctx2.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_version.h"

namespace openzl::gpu {

namespace {

std::string_view asStringView(std::span<const std::byte> bytes)
{
    return {
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size(),
    };
}

} // namespace

ZL_Report decompressChunks(
        void*,
        size_t,
        std::span<const GPUFrameHeaderForChunks>,
        std::span<const GPUChunk>,
        ZL_GPU_Stream)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    ZL_ERR(GENERIC, "GPU decompression is not implemented");
}

ZL_Report collectGPUChunksFromFrame(
        const void* src_d,
        std::span<const std::byte> src_h,
        std::vector<GPUChunk>& chunks,
        std::vector<GPUFrameHeaderForChunks>& frameHeaders)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    ZL_TRY_LET_CONST(
            size_t,
            frameHeaderSize,
            ZL_getHeaderSize(src_h.data(), src_h.size()));

    FrameInfo frameInfo{ asStringView(src_h) };
    size_t const formatVersion = frameInfo.formatVersion();
    ZL_ERR_IF_LT(
            formatVersion, ZL_CHUNK_VERSION_MIN, formatVersion_unsupported);

    DCtx dctx;

    ZL_ERR_IF_ERR(DCTX_initFromFrameInfo(dctx.get(), frameInfo.get()));

    const std::byte* const frame_d = static_cast<const std::byte*>(src_d);
    const GPUFrameHeaderForChunks collectedFrameHeader{
        .frameHeader_d   = frame_d,
        .frameHeaderSize = frameHeaderSize,
    };
    size_t const frameHeaderIdx = frameHeaders.size();
    std::vector<GPUChunk> collectedChunks;
    // look for first chunk after frame header ends
    size_t chunkOffset = frameHeaderSize;
    while (true) {
        ZL_ERR_IF_GE(chunkOffset, src_h.size(), srcSize_tooSmall);

        // end of frame
        if (src_h[chunkOffset] == std::byte{ 0 }) {
            ++chunkOffset;
            break;
        }

        ZL_TRY_LET_CONST(
                DCTX_FrameChunkInfo,
                chunkInfo,
                DCTX_prepareFrameChunk(
                        dctx.get(), src_h.data(), src_h.size(), chunkOffset));
        ZL_ERR_IF_EQ(chunkInfo.chunkSize, 0, corruption);
        ZL_ERR_IF_GT(
                chunkInfo.chunkSize,
                src_h.size() - chunkOffset,
                srcSize_tooSmall);

        collectedChunks.push_back(
                {
                        .frameHeaderIdx  = frameHeaderIdx,
                        .chunk_d         = frame_d + chunkOffset,
                        .chunkSize       = chunkInfo.chunkSize,
                        .chunkHeaderSize = chunkInfo.chunkHeaderSize,
                });
        chunkOffset += chunkInfo.chunkSize;
    }
    ZL_ERR_IF_NE(chunkOffset, src_h.size(), srcSize_tooLarge);

    ZL_ERR_IF_EQ(frameHeaders.size(), frameHeaders.max_size(), allocation);
    ZL_ERR_IF_GT(
            collectedChunks.size(),
            chunks.max_size() - chunks.size(),
            allocation);
    frameHeaders.reserve(frameHeaders.size() + 1);
    chunks.reserve(chunks.size() + collectedChunks.size());
    frameHeaders.push_back(collectedFrameHeader);
    chunks.insert(chunks.end(), collectedChunks.begin(), collectedChunks.end());
    return ZL_returnValue(chunkOffset);
}

namespace {

// temporary until we can replace with jump table
ZL_Report collectGPUChunksFromFullCopy(
        std::span<const std::byte> src_d,
        ZL_GPU_Stream stream,
        std::vector<GPUChunk>& chunks,
        std::vector<GPUFrameHeaderForChunks>& frameHeaders)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    std::vector<std::byte> src_h;
    ZL_ERR_IF_GT(src_d.size(), src_h.max_size(), srcSize_tooLarge);
    src_h.resize(src_d.size());

    cudaError_t cudaResult = cudaMemcpyAsync(
            src_h.data(),
            src_d.data(),
            src_d.size(),
            cudaMemcpyDeviceToHost,
            stream);
    ZL_ERR_IF_NE(cudaResult, cudaSuccess, GENERIC);
    cudaResult = cudaStreamSynchronize(stream);
    ZL_ERR_IF_NE(cudaResult, cudaSuccess, GENERIC);

    return collectGPUChunksFromFrame(src_d.data(), src_h, chunks, frameHeaders);
}

} // namespace

ZL_Report collectGPUChunks(
        std::span<const std::byte> src_d,
        ZL_GPU_Stream stream,
        std::vector<GPUChunk>& chunks,
        std::vector<GPUFrameHeaderForChunks>& frameHeaders)
{
    // Not inline so we can replace with jump table later
    return collectGPUChunksFromFullCopy(src_d, stream, chunks, frameHeaders);
}

} // namespace openzl::gpu

extern "C" ZL_Report ZL_GPU_decompress(
        void* dst_d,
        size_t dstCapacity,
        const void* src_d,
        size_t srcSize,
        ZL_GPU_Stream stream)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    try {
        std::vector<openzl::gpu::GPUChunk> chunks;
        std::vector<openzl::gpu::GPUFrameHeaderForChunks> frameHeaders;
        ZL_Report const collectResult = openzl::gpu::collectGPUChunks(
                std::span<const std::byte>{
                        static_cast<const std::byte*>(src_d),
                        srcSize,
                },
                stream,
                chunks,
                frameHeaders);
        if (ZL_isError(collectResult)) {
            return collectResult;
        }

        return openzl::gpu::decompressChunks(
                dst_d, dstCapacity, frameHeaders, chunks, stream);
    } catch (const std::bad_alloc&) {
        ZL_ERR(allocation, "GPU decompression allocation failed");
    } catch (...) {
        ZL_ERR(GENERIC, "GPU decompression failed with an exception");
    }
}
