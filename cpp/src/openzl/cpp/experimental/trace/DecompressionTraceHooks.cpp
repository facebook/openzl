// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "openzl/cpp/experimental/trace/DecompressionTraceHooks.hpp"

#include <map>
#include <stdexcept>
#include <utility>

namespace openzl::visualizer {

void DecompressionTraceHooks::on_ZL_DCtx_decompressMultiTBuffer_start(
        ZL_DCtx* dctx,
        size_t nbOutputs,
        const void* framePtr,
        size_t frameSize)
{
    // Reset cached data
    latestStreamdumpCache_ = {};

    if (tracer_) {
        throw std::runtime_error(
                "Corrupted state. Trace context already exists!");
    }
    tracer_ = std::make_unique<DecompressTracer>(showStreamPreview_);
    tracer_->on_ZL_DCtx_decompressMultiTBuffer_start(
            dctx, nbOutputs, framePtr, frameSize);
}

void DecompressionTraceHooks::on_ZL_DCtx_decompressMultiTBuffer_end(
        ZL_DCtx* dctx,
        ZL_Report result)
{
    tracer_->on_ZL_DCtx_decompressMultiTBuffer_end(dctx, result);

    auto trace             = tracer_->extractTrace();
    latestTraceCache_      = std::move(trace.trace);
    latestStreamdumpCache_ = std::move(trace.streamdump);
    tracer_                = nullptr;
}

void DecompressionTraceHooks::on_decompressChunk_start(
        ZL_DCtx* dctx,
        size_t chunkIndex)
{
    tracer_->on_decompressChunk_start(dctx, chunkIndex);
}

void DecompressionTraceHooks::on_decompressChunk_end(
        ZL_DCtx* dctx,
        ZL_Report result)
{
    tracer_->on_decompressChunk_end(dctx, result);
}

void DecompressionTraceHooks::on_ZL_Decoder_getCodecHeader(
        const ZL_Decoder* dictx,
        const void* trh,
        size_t trhSize)
{
    tracer_->on_ZL_Decoder_getCodecHeader(dictx, trh, trhSize);
}

void DecompressionTraceHooks::on_codecDecode_start(
        ZL_Decoder* dictx,
        const ZL_Data* const* inStreams,
        size_t nbInStreams)
{
    tracer_->on_codecDecode_start(dictx, inStreams, nbInStreams);
}

void DecompressionTraceHooks::on_codecDecode_end(
        ZL_Decoder* dictx,
        const ZL_Data* const* outStreams,
        size_t nbOutStreams,
        ZL_Report result)
{
    tracer_->on_codecDecode_end(dictx, outStreams, nbOutStreams, result);
}

std::pair<
        poly::string_view,
        std::map<std::string, std::pair<poly::string_view, poly::string_view>>>
DecompressionTraceHooks::getLatestTrace()
{
    std::map<std::string, std::pair<poly::string_view, poly::string_view>>
            streamdumps;
    for (size_t chunkId = 0; chunkId < latestStreamdumpCache_.size();
         chunkId++) {
        for (const auto& entry : latestStreamdumpCache_[chunkId]) {
            std::string key = "chunk_" + std::to_string(chunkId) + "_stream_"
                    + std::to_string(entry.streamId);
            streamdumps[key] = { poly::string_view(entry.content),
                                 poly::string_view(entry.strLens) };
        }
    }
    return { latestTraceCache_, std::move(streamdumps) };
}

} // namespace openzl::visualizer
