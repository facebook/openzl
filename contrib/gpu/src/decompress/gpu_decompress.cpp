// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cuda_runtime_api.h>

#include <cstddef>
#include <exception>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#include "contrib/gpu/src/common/errors.hpp"
#include "contrib/gpu/src/decompress/gpu_decompress.hpp"
#include "contrib/gpu/src/planning/decode_planner.hpp"
#include "openzl/common/errors_internal.h"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/FrameInfo.hpp"
#include "openzl/cpp/poly/Span.hpp"
#include "openzl/decompress/dctx2.h"
#include "openzl/shared/overflow.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_version.h"

namespace openzl::gpu {

namespace {

std::string_view asStringView(poly::span<const std::byte> bytes)
{
    return {
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size(),
    };
}

struct DeviceToHostCopy {
    poly::span<std::byte> dst_h;
    const std::byte* src_d;
};

void copyDeviceToHostAndSynchronize(
        poly::span<const DeviceToHostCopy> copies,
        ZL_GPU_Stream stream)
{
    for (const DeviceToHostCopy& copy : copies) {
        if (copy.dst_h.empty()) {
            continue;
        }
        ZL_THROW_IF_NULL(copy.dst_h.data(), parameter_invalid);
        ZL_THROW_IF_NULL(copy.src_d, parameter_invalid);
    }

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
            throwError(
                    ZL_ErrorCode_GENERIC,
                    std::string("cudaMemcpyAsync failed: ")
                            + cudaGetErrorString(result));
        }
        copyEnqueued = true;
    }
    if (copyEnqueued) {
        cudaError_t const result = cudaStreamSynchronize(stream);
        ZL_THROW_IF_NE(result, cudaSuccess, GENERIC);
    }
}

} // namespace

namespace {

struct StagedChunkHeader {
    poly::span<const std::byte> bytes_h;
    size_t frameHeaderIdx;
};

struct StagedHeaders {
    // frameHeaders_h and chunkHeaders_h point into storage_h. A move keeps the
    // vector's buffer, so the spans stay valid, but a copy would leave them
    // pointing into the source object.
    StagedHeaders()                                = default;
    StagedHeaders(const StagedHeaders&)            = delete;
    StagedHeaders& operator=(const StagedHeaders&) = delete;
    StagedHeaders(StagedHeaders&&)                 = default;
    StagedHeaders& operator=(StagedHeaders&&)      = delete;
    ~StagedHeaders()                               = default;

    // TODO(maxmandina): Explore context-owned pinned staging if header-copy
    // latency becomes worth amortizing.
    std::vector<std::byte> storage_h;
    std::vector<poly::span<const std::byte>> frameHeaders_h;
    std::vector<StagedChunkHeader> chunkHeaders_h;
};

StagedHeaders copyHeadersToHost(
        poly::span<const GPUFrameHeaderForChunks> frameHeaders,
        poly::span<const GPUChunk> chunks,
        ZL_GPU_Stream stream)
{
    size_t stagingSize = 0;
    for (const GPUFrameHeaderForChunks& frameHeader : frameHeaders) {
        ZL_THROW_IF(
                frameHeader.frameHeader_d == nullptr
                        || frameHeader.frameHeaderSize == 0,
                parameter_invalid);
        ZL_THROW_IF(
                ZL_overflowAddST(
                        stagingSize, frameHeader.frameHeaderSize, &stagingSize),
                allocation);
    }
    for (const GPUChunk& chunk : chunks) {
        ZL_THROW_IF(
                chunk.frameHeaderIdx >= frameHeaders.size()
                        || chunk.chunk_d == nullptr
                        || chunk.chunkHeaderSize == 0
                        || chunk.chunkHeaderSize > chunk.chunkSize,
                parameter_invalid);
        ZL_THROW_IF(
                ZL_overflowAddST(
                        stagingSize, chunk.chunkHeaderSize, &stagingSize),
                allocation);
    }

    StagedHeaders stagedHeaders;
    stagedHeaders.frameHeaders_h.reserve(frameHeaders.size());
    stagedHeaders.chunkHeaders_h.reserve(chunks.size());

    // Allocate the host buffer before starting copies or creating views into
    // it, so its address cannot move.
    stagedHeaders.storage_h.resize(stagingSize);

    std::vector<DeviceToHostCopy> copies;
    copies.reserve(frameHeaders.size() + chunks.size());
    size_t dstOffset = 0;
    for (const GPUFrameHeaderForChunks& frameHeader : frameHeaders) {
        copies.push_back(
                {
                        .dst_h =
                                poly::span<std::byte>{ stagedHeaders.storage_h }
                                        .subspan(
                                                dstOffset,
                                                frameHeader.frameHeaderSize),
                        .src_d = static_cast<const std::byte*>(
                                frameHeader.frameHeader_d),
                });
        dstOffset += frameHeader.frameHeaderSize;
    }
    for (const GPUChunk& chunk : chunks) {
        copies.push_back(
                {
                        .dst_h =
                                poly::span<std::byte>{ stagedHeaders.storage_h }
                                        .subspan(
                                                dstOffset,
                                                chunk.chunkHeaderSize),
                        .src_d = static_cast<const std::byte*>(chunk.chunk_d),
                });
        dstOffset += chunk.chunkHeaderSize;
    }
    copyDeviceToHostAndSynchronize(copies, stream);

    // Preserve frame-header order so each chunk index also identifies the
    // corresponding staged header.
    size_t headerOffset = 0;
    for (const GPUFrameHeaderForChunks& frameHeader : frameHeaders) {
        stagedHeaders.frameHeaders_h.emplace_back(
                stagedHeaders.storage_h.data() + headerOffset,
                frameHeader.frameHeaderSize);
        headerOffset += frameHeader.frameHeaderSize;
    }
    for (const GPUChunk& chunk : chunks) {
        poly::span<const std::byte> const headerBytes_h{
            stagedHeaders.storage_h.data() + headerOffset,
            chunk.chunkHeaderSize,
        };
        stagedHeaders.chunkHeaders_h.push_back(
                {
                        .bytes_h        = headerBytes_h,
                        .frameHeaderIdx = chunk.frameHeaderIdx,
                });
        headerOffset += chunk.chunkHeaderSize;
    }

    return stagedHeaders;
}

std::vector<PreparedGPUChunk> createDctxsForChunks(
        const StagedHeaders& stagedHeaders,
        poly::span<const GPUChunk> chunks)
{
    std::vector<FrameInfo> frameInfos;
    frameInfos.reserve(stagedHeaders.frameHeaders_h.size());
    for (poly::span<const std::byte> frameHeader_h :
         stagedHeaders.frameHeaders_h) {
        frameInfos.emplace_back(asStringView(frameHeader_h));
    }

    ZL_THROW_IF_NE(stagedHeaders.chunkHeaders_h.size(), chunks.size(), GENERIC);
    std::vector<PreparedGPUChunk> result;
    result.reserve(chunks.size());
    auto chunkHeaderIt = stagedHeaders.chunkHeaders_h.begin();
    for (const GPUChunk& chunk : chunks) {
        const StagedChunkHeader& chunkHeader = *chunkHeaderIt++;
        DCtx dctx;

        dctx.unwrap(ZL_DCtx_setParameter(
                dctx.get(),
                ZL_DParam_enableCodecFusion,
                ZL_TernaryParam_disable));

        ZL_THROW_IF_GE(
                chunkHeader.frameHeaderIdx,
                frameInfos.size(),
                parameter_invalid);
        dctx.unwrap(DCTX_initFromFrameInfo(
                dctx.get(), frameInfos.at(chunkHeader.frameHeaderIdx).get()));

        dctx.unwrap(DCTX_prepareFrameChunkFromHeader(
                dctx.get(),
                chunkHeader.bytes_h.data(),
                chunkHeader.bytes_h.size(),
                chunk.chunk_d,
                chunk.chunkSize));

        result.push_back(
                {
                        .chunk = chunk,
                        .dctx  = std::move(dctx),
                });
    }

    return result;
}

void copyTransformHeadersToHost(
        poly::span<PreparedGPUChunk> preparedChunks,
        ZL_GPU_Stream stream)
{
    for (PreparedGPUChunk& prepared : preparedChunks) {
        const DFH_Struct* const frameHeader =
                DCtx_getFrameHeader(prepared.dctx.get());
        ZL_THROW_IF_NULL(frameHeader, logicError);
        ZL_THROW_IF_GT(
                prepared.chunk.chunkHeaderSize,
                prepared.chunk.chunkSize,
                parameter_invalid);
        ZL_THROW_IF_GT(
                frameHeader->totalTHSize,
                prepared.chunk.chunkSize - prepared.chunk.chunkHeaderSize,
                srcSize_tooSmall);
        prepared.transformHeaders_h.resize(frameHeader->totalTHSize);
    }

    std::vector<DeviceToHostCopy> copies;
    copies.reserve(preparedChunks.size());
    for (PreparedGPUChunk& prepared : preparedChunks) {
        if (prepared.transformHeaders_h.empty()) {
            continue;
        }
        const auto* const transformHeaders_d =
                static_cast<const std::byte*>(prepared.chunk.chunk_d)
                + prepared.chunk.chunkHeaderSize;
        copies.push_back(
                {
                        .dst_h = prepared.transformHeaders_h,
                        .src_d = transformHeaders_d,
                });
    }
    copyDeviceToHostAndSynchronize(copies, stream);
}

} // namespace

std::vector<PreparedGPUChunk> prepareChunksForPlanning(
        poly::span<const GPUFrameHeaderForChunks> frameHeaders,
        poly::span<const GPUChunk> chunks,
        ZL_GPU_Stream stream)
{
    const StagedHeaders stagedHeaders =
            copyHeadersToHost(frameHeaders, chunks, stream);
    std::vector<PreparedGPUChunk> result =
            createDctxsForChunks(stagedHeaders, chunks);
    copyTransformHeadersToHost(result, stream);
    return result;
}

size_t decompressChunks(
        void*,
        size_t,
        poly::span<const GPUFrameHeaderForChunks> frameHeaders,
        poly::span<const GPUChunk> chunks,
        ZL_GPU_Stream stream)
{
    std::vector<PreparedGPUChunk> preparedChunks =
            prepareChunksForPlanning(frameHeaders, chunks, stream);
    std::vector<PreparedGpuChunkView> chunkViews;
    chunkViews.reserve(preparedChunks.size());
    for (PreparedGPUChunk& prepared : preparedChunks) {
        chunkViews.push_back(
                {
                        .dctx               = prepared.dctx.get(),
                        .transformHeaders_h = prepared.transformHeaders_h,
                });
    }
    DecodePlan plan;
    unwrap(planDecode(chunkViews, plan));
    throwError(ZL_ErrorCode_GENERIC, "GPU decompression is not implemented");
}

size_t collectGPUChunksFromFrame(
        const void* src_d,
        poly::span<const std::byte> src_h,
        std::vector<GPUChunk>& chunks,
        std::vector<GPUFrameHeaderForChunks>& frameHeaders)
{
    const size_t frameHeaderSize =
            unwrap(ZL_getHeaderSize(src_h.data(), src_h.size()));

    FrameInfo frameInfo{ asStringView(src_h) };
    size_t const formatVersion = frameInfo.formatVersion();
    ZL_THROW_IF_LT(
            formatVersion, ZL_CHUNK_VERSION_MIN, formatVersion_unsupported);

    DCtx dctx;

    dctx.unwrap(DCTX_initFromFrameInfo(dctx.get(), frameInfo.get()));

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
        ZL_THROW_IF_GE(chunkOffset, src_h.size(), srcSize_tooSmall);

        // end of frame
        if (src_h[chunkOffset] == std::byte{ 0 }) {
            ++chunkOffset;
            break;
        }

        const DCTX_FrameChunkInfo chunkInfo =
                dctx.unwrap(DCTX_prepareFrameChunk(
                        dctx.get(), src_h.data(), src_h.size(), chunkOffset));
        ZL_THROW_IF_EQ(chunkInfo.chunkSize, 0, corruption);
        ZL_THROW_IF_GT(
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
    ZL_THROW_IF_NE(chunkOffset, src_h.size(), srcSize_tooLarge);

    ZL_THROW_IF_EQ(frameHeaders.size(), frameHeaders.max_size(), allocation);
    ZL_THROW_IF_GT(
            collectedChunks.size(),
            chunks.max_size() - chunks.size(),
            allocation);
    frameHeaders.reserve(frameHeaders.size() + 1);
    chunks.reserve(chunks.size() + collectedChunks.size());
    frameHeaders.push_back(collectedFrameHeader);
    chunks.insert(chunks.end(), collectedChunks.begin(), collectedChunks.end());
    return chunkOffset;
}

namespace {

// temporary until we can replace with jump table
size_t collectGPUChunksFromFullCopy(
        poly::span<const std::byte> src_d,
        ZL_GPU_Stream stream,
        std::vector<GPUChunk>& chunks,
        std::vector<GPUFrameHeaderForChunks>& frameHeaders)
{
    std::vector<std::byte> src_h;
    ZL_THROW_IF_GT(src_d.size(), src_h.max_size(), srcSize_tooLarge);
    src_h.resize(src_d.size());

    const DeviceToHostCopy copy{
        .dst_h = src_h,
        .src_d = src_d.data(),
    };
    copyDeviceToHostAndSynchronize(
            poly::span<const DeviceToHostCopy>{ &copy, 1 }, stream);

    return collectGPUChunksFromFrame(src_d.data(), src_h, chunks, frameHeaders);
}

} // namespace

size_t collectGPUChunks(
        poly::span<const std::byte> src_d,
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
        openzl::gpu::collectGPUChunks(
                openzl::poly::span<const std::byte>{
                        static_cast<const std::byte*>(src_d),
                        srcSize,
                },
                stream,
                chunks,
                frameHeaders);

        return ZL_returnValue(
                openzl::gpu::decompressChunks(
                        dst_d, dstCapacity, frameHeaders, chunks, stream));
    } catch (const openzl::Exception& e) {
        return ZL_WRAP_ERROR_NO_FRAME(e.toError());
    } catch (const std::bad_alloc&) {
        ZL_ERR(allocation, "GPU decompression allocation failed");
    } catch (const std::exception& e) {
        ZL_ERR(GENERIC, "GPU decompression failed: %s", e.what());
    } catch (...) {
        ZL_ERR(GENERIC, "GPU decompression failed with an exception");
    }
}
