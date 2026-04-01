// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <map>
#include <vector>

#include "openzl/cpp/experimental/trace/ChunkTraceCore.hpp"
#include "openzl/cpp/experimental/trace/Codec.hpp"
#include "openzl/cpp/experimental/trace/StreamVisualizer.hpp"

namespace openzl::visualizer {

/**
 * Per-chunk data collection for decompression tracing.
 * Analogous to CompressChunkTrace on the compression side, but without graph
 * info (no graphs at decompress time).
 */
class DecompressChunkTrace {
   public:
    DecompressChunkTrace() = delete;
    explicit DecompressChunkTrace(size_t chunkId, bool showStreamPreview)
            : chunkId_(chunkId), showStreamPreview_(showStreamPreview)
    {
    }

    /**
     * Helper function to create a dummy chunk that just contains a segmenter
     * node, in cases where the trace is a multi-chunk run.
     */
    static DecompressChunkTrace makeSegmenterChunk(
            size_t chunkId,
            bool showStreamPreview);
    /**
     * Finalize the trace.
     * - On success, unsourced streams (decoded outputs) go to "zl.regen".
     * - On failure, unsourced streams go to "zl.#in_progress".
     */
    void finalizeTrace(ZL_Report result);

    /**
     * Resolve error context strings for all codecs.
     * Must be called while the DCtx is still alive.
     */
    void resolveErrorStrings(const ZL_DCtx* dctx);

    ZL_Report serializeToCBOR(
            A1C_Arena* a1c_arena,
            A1C_ArrayBuilder* chunkArrayBuilder,
            ZL_OperationContext* opCtx);

    /* ************** Trampolined hook calls ************** */
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

    std::map<size_t, std::pair<std::string, std::string>>&& getStreamdump();

   private:
    void streamdump(const ZL_Data* data);

    const size_t chunkId_;
    bool showStreamPreview_     = true; // show stream preview data from trace
    size_t totalCompressedSize_ = 0;
    size_t currCodecNum_        = 0;
    std::map<ZL_DataID, Stream, ZL_DataIDCustomComparator> streamInfo_;
    std::map<size_t, std::pair<std::string, std::string>> streamdump_;
    std::vector<Codec> codecInfo_;
    // No graphInfo_ — decompression has no graphs
};

} // namespace openzl::visualizer
