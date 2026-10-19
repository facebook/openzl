// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cuda_runtime_api.h>

#include <cstddef>
#include <memory>
#include <new>
#include <vector>

#include "contrib/gpu/src/decompress/gpu_decompress.hpp"
#include "contrib/gpu/src/planning/decode_planner.hpp"
#include "openzl/common/errors_internal.h"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/poly/Span.hpp"
#include "openzl/decompress/dctx2.h"
#include "openzl/shared/overflow.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_version.h"

namespace openzl::gpu {

namespace {

// openzl::FrameInfo reports a corrupt header by throwing, and this code must
// not throw.
using FrameInfoPtr =
        std::unique_ptr<ZL_FrameInfo, decltype(&ZL_FrameInfo_free)>;

struct DeviceToHostCopy {
    poly::span<std::byte> dst_h;
    const std::byte* src_d;
};

ZL_Report copyDeviceToHostAndSynchronize(
        poly::span<const DeviceToHostCopy> copies,
        ZL_GPU_Stream stream,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);

    for (const DeviceToHostCopy& copy : copies) {
        if (copy.dst_h.empty()) {
            continue;
        }
        ZL_ERR_IF_NULL(copy.dst_h.data(), parameter_invalid);
        ZL_ERR_IF_NULL(copy.src_d, parameter_invalid);
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
            ZL_ERR(GENERIC,
                   "cudaMemcpyAsync failed: %s",
                   cudaGetErrorString(result));
        }
        copyEnqueued = true;
    }
    if (copyEnqueued) {
        cudaError_t const result = cudaStreamSynchronize(stream);
        ZL_ERR_IF_NE(result, cudaSuccess, GENERIC);
    }

    return ZL_returnSuccess();
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
    // pointing into the source object. Only move construction is used, so
    // assignment stays deleted.
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

Result<StagedHeaders> copyHeadersToHost(
        poly::span<const GPUFrameHeaderForChunks> frameHeaders,
        poly::span<const GPUChunk> chunks,
        ZL_GPU_Stream stream)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);

    size_t stagingSize = 0;
    for (const GPUFrameHeaderForChunks& frameHeader : frameHeaders) {
        ZL_ERR_IF(
                frameHeader.frameHeader_d == nullptr
                        || frameHeader.frameHeaderSize == 0,
                parameter_invalid);
        ZL_ERR_IF(
                ZL_overflowAddST(
                        stagingSize, frameHeader.frameHeaderSize, &stagingSize),
                allocation);
    }
    for (const GPUChunk& chunk : chunks) {
        ZL_ERR_IF(
                chunk.frameHeaderIdx >= frameHeaders.size()
                        || chunk.chunk_d == nullptr
                        || chunk.chunkHeaderSize == 0
                        || chunk.chunkHeaderSize > chunk.chunkSize,
                parameter_invalid);
        ZL_ERR_IF(
                ZL_overflowAddST(
                        stagingSize, chunk.chunkHeaderSize, &stagingSize),
                allocation);
    }

    StagedHeaders stagedHeaders;
    stagedHeaders.frameHeaders_h.reserve(frameHeaders.size());
    stagedHeaders.chunkHeaders_h.reserve(chunks.size());

    // Allocate the host buffer before starting copies or creating views into
    // it, so its address cannot move.
    ZL_ERR_IF_GT(stagingSize, stagedHeaders.storage_h.max_size(), allocation);
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
    ZL_ERR_IF_ERR(copyDeviceToHostAndSynchronize(copies, stream, nullptr));

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

Result<std::vector<PreparedGPUChunk>> createDctxsForChunks(
        const StagedHeaders& stagedHeaders,
        poly::span<const GPUChunk> chunks)
{
    // No shared DCTX exists until each per-chunk context is created.
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);

    std::vector<FrameInfoPtr> frameInfos;
    frameInfos.reserve(stagedHeaders.frameHeaders_h.size());
    for (poly::span<const std::byte> frameHeader_h :
         stagedHeaders.frameHeaders_h) {
        FrameInfoPtr frameInfo{
            ZL_FrameInfo_create(frameHeader_h.data(), frameHeader_h.size()),
            ZL_FrameInfo_free,
        };
        ZL_ERR_IF_NULL(frameInfo.get(), corruption);
        frameInfos.push_back(std::move(frameInfo));
    }

    ZL_ERR_IF_NE(stagedHeaders.chunkHeaders_h.size(), chunks.size(), GENERIC);
    std::vector<PreparedGPUChunk> result;
    result.reserve(chunks.size());
    auto chunkHeaderIt = stagedHeaders.chunkHeaders_h.begin();
    for (const GPUChunk& chunk : chunks) {
        const StagedChunkHeader& chunkHeader = *chunkHeaderIt++;
        DCtx dctx;

        ZL_ERR_IF_ERR(ZL_DCtx_setParameter(
                dctx.get(),
                ZL_DParam_enableCodecFusion,
                ZL_TernaryParam_disable));
        ZL_ERR_IF_GE(
                chunkHeader.frameHeaderIdx,
                frameInfos.size(),
                parameter_invalid);
        ZL_ERR_IF_ERR(DCTX_initFromFrameInfo(
                dctx.get(), frameInfos.at(chunkHeader.frameHeaderIdx).get()));
        ZL_ERR_IF_ERR(DCTX_prepareFrameChunkFromHeader(
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

ZL_Report copyTransformHeadersToHost(
        poly::span<PreparedGPUChunk> preparedChunks,
        ZL_GPU_Stream stream)
{
    ZL_DCtx* const dctx = preparedChunks.empty()
            ? nullptr
            : preparedChunks.front().dctx.get();
    ZL_RESULT_DECLARE_SCOPE_REPORT(dctx);

    for (PreparedGPUChunk& prepared : preparedChunks) {
        const DFH_Struct* const frameHeader =
                DCtx_getFrameHeader(prepared.dctx.get());
        ZL_ERR_IF_NULL(frameHeader, logicError);
        ZL_ERR_IF_GT(
                prepared.chunk.chunkHeaderSize,
                prepared.chunk.chunkSize,
                parameter_invalid);
        ZL_ERR_IF_GT(
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
    return copyDeviceToHostAndSynchronize(
            copies, stream, ZL_GET_OPERATION_CONTEXT(dctx));
}

} // namespace

Result<std::vector<PreparedGPUChunk>> prepareChunksForPlanning(
        poly::span<const GPUFrameHeaderForChunks> frameHeaders,
        poly::span<const GPUChunk> chunks,
        ZL_GPU_Stream stream)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    Result<StagedHeaders> const stagedHeaders =
            copyHeadersToHost(frameHeaders, chunks, stream);
    if (stagedHeaders.isError()) {
        return stagedHeaders.report();
    }
    Result<std::vector<PreparedGPUChunk>> result =
            createDctxsForChunks(stagedHeaders.value(), chunks);
    if (result.isError()) {
        return result.report();
    }
    ZL_ERR_IF_ERR(copyTransformHeadersToHost(result.value(), stream));
    return result;
}

ZL_Report decompressChunks(
        void*,
        size_t,
        poly::span<const GPUFrameHeaderForChunks> frameHeaders,
        poly::span<const GPUChunk> chunks,
        ZL_GPU_Stream stream)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    Result<std::vector<PreparedGPUChunk>> prepareResult =
            prepareChunksForPlanning(frameHeaders, chunks, stream);
    if (prepareResult.isError()) {
        return prepareResult.report();
    }
    std::vector<PreparedGPUChunk> preparedChunks =
            std::move(prepareResult).value();
    std::vector<PreparedGpuChunkView> chunkViews;
    chunkViews.reserve(preparedChunks.size());
    for (PreparedGPUChunk& prepared : preparedChunks) {
        chunkViews.push_back(
                {
                        .dctx               = prepared.dctx.get(),
                        .transformHeaders_h = prepared.transformHeaders_h,
                });
    }
    Result<DecodePlan> const planResult = planDecode(chunkViews);
    if (planResult.isError()) {
        return planResult.report();
    }
    ZL_ERR(GENERIC, "GPU decompression is not implemented");
}

namespace {

/** Returns the chunks of a frame whose header ends at @p frameHeaderSize. */
Result<std::vector<GPUChunk>> parseChunkDirectory(
        const ZL_FrameInfo* frameInfo,
        const std::byte* frame_d,
        poly::span<const std::byte> src_h,
        size_t frameHeaderSize,
        size_t frameHeaderIdx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    DCtx dctx;
    ZL_ERR_IF_ERR(DCTX_initFromFrameInfo(dctx.get(), frameInfo));

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
    return collectedChunks;
}

} // namespace

ZL_Report collectGPUChunksFromFrame(
        const void* src_d,
        poly::span<const std::byte> src_h,
        std::vector<GPUChunk>& chunks,
        std::vector<GPUFrameHeaderForChunks>& frameHeaders)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    ZL_TRY_LET_CONST(
            size_t,
            frameHeaderSize,
            ZL_getHeaderSize(src_h.data(), src_h.size()));

    FrameInfoPtr const frameInfo{
        ZL_FrameInfo_create(src_h.data(), src_h.size()),
        ZL_FrameInfo_free,
    };
    ZL_ERR_IF_NULL(frameInfo.get(), corruption);
    ZL_TRY_LET_CONST(
            size_t,
            formatVersion,
            ZL_FrameInfo_getFormatVersion(frameInfo.get()));
    ZL_ERR_IF_LT(
            formatVersion, ZL_CHUNK_VERSION_MIN, formatVersion_unsupported);

    const std::byte* const frame_d = static_cast<const std::byte*>(src_d);
    Result<std::vector<GPUChunk>> const collected = parseChunkDirectory(
            frameInfo.get(),
            frame_d,
            src_h,
            frameHeaderSize,
            frameHeaders.size());
    ZL_ERR_IF_ERR(collected.report());
    const std::vector<GPUChunk>& collectedChunks = collected.value();

    ZL_ERR_IF_EQ(frameHeaders.size(), frameHeaders.max_size(), allocation);
    ZL_ERR_IF_GT(
            collectedChunks.size(),
            chunks.max_size() - chunks.size(),
            allocation);
    frameHeaders.reserve(frameHeaders.size() + 1);
    chunks.reserve(chunks.size() + collectedChunks.size());
    frameHeaders.push_back(
            {
                    .frameHeader_d   = frame_d,
                    .frameHeaderSize = frameHeaderSize,
            });
    chunks.insert(chunks.end(), collectedChunks.begin(), collectedChunks.end());
    return ZL_returnValue(src_h.size());
}

namespace {

// temporary until we can replace with jump table
ZL_Report collectGPUChunksFromFullCopy(
        poly::span<const std::byte> src_d,
        ZL_GPU_Stream stream,
        std::vector<GPUChunk>& chunks,
        std::vector<GPUFrameHeaderForChunks>& frameHeaders)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    std::vector<std::byte> src_h;
    ZL_ERR_IF_GT(src_d.size(), src_h.max_size(), srcSize_tooLarge);
    src_h.resize(src_d.size());

    const DeviceToHostCopy copy{
        .dst_h = src_h,
        .src_d = src_d.data(),
    };
    ZL_ERR_IF_ERR(copyDeviceToHostAndSynchronize(
            poly::span<const DeviceToHostCopy>{ &copy, 1 }, stream, nullptr));

    return collectGPUChunksFromFrame(src_d.data(), src_h, chunks, frameHeaders);
}

} // namespace

ZL_Report collectGPUChunks(
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
        ZL_Report const collectResult = openzl::gpu::collectGPUChunks(
                openzl::poly::span<const std::byte>{
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
