// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "openzl/cpp/experimental/trace/DecompressChunkTrace.hpp"
#include "openzl/zl_version.h"

namespace openzl::visualizer {

class DecompressTracer {
   public:
    DecompressTracer() = default;

    TraceResult extractTrace();

    // Trampolined functions from DecompressionTraceHooks
    void on_ZL_DCtx_decompressMultiTBuffer_start(
            ZL_DCtx* dctx,
            size_t nbOutputs,
            const void* framePtr,
            size_t frameSize);
    void on_ZL_DCtx_decompressMultiTBuffer_end(ZL_DCtx* dctx, ZL_Report result);

    void on_decompressChunk_start(ZL_DCtx* dctx, size_t chunkIndex);
    void on_decompressChunk_end(ZL_DCtx* dctx, ZL_Report result);

    void on_ZL_Decoder_getCodecHeader(
            const ZL_Decoder* dictx,
            const void* trh,
            size_t trhSize);

    void on_codecDecode_start(
            ZL_Decoder* dictx,
            const ZL_Data* const* inStreams,
            size_t nbInStreams);
    void on_codecDecode_end(
            ZL_Decoder* dictx,
            const ZL_Data* const* outStreams,
            size_t nbOutStreams,
            ZL_Report result);

   private:
    ZL_Report serializeStreamdumpToCbor(
            A1C_Arena* a1c_arena,
            std::vector<uint8_t>& buffer);

    static constexpr uint32_t libraryVersion = ZL_LIBRARY_VERSION_NUMBER;
    static constexpr uint32_t traceVersion   = 1;
    uint32_t frameVersion_{};

    std::vector<DecompressChunkTrace> chunks_;
    DecompressChunkTrace* currChunk_ =
            nullptr; // convenience pointer to the current chunk trace
    ZL_OperationContext* opCtx_ = nullptr;

    TraceResult trace_;
};

} // namespace openzl::visualizer
