// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "openzl/cpp/DecompressIntrospectionHooks.hpp"
#include "openzl/cpp/experimental/trace/DecompressTracer.hpp"
#include "openzl/cpp/poly/StringView.hpp"
#include "openzl/zl_opaque_types.h"

#include <map>
#include <memory>
#include <string>

namespace openzl::visualizer {

class DecompressionTraceHooks : public openzl::DecompressIntrospectionHooks {
   public:
    explicit DecompressionTraceHooks(bool showStreamPreview)
            : showStreamPreview_(showStreamPreview)
    {
    }
    ~DecompressionTraceHooks() override = default;

    std::pair<
            poly::string_view,
            std::map<
                    std::string,
                    std::pair<poly::string_view, poly::string_view>>>
    getLatestTrace();

    // ***************************************************
    // Overridden functions from DecompressIntrospectionHooks
    // ***************************************************
    void on_ZL_DCtx_decompressMultiTBuffer_start(
            ZL_DCtx* dctx,
            size_t nbOutputs,
            const void* framePtr,
            size_t frameSize) override;
    void on_ZL_DCtx_decompressMultiTBuffer_end(ZL_DCtx* dctx, ZL_Report result)
            override;

    void on_decompressChunk_start(ZL_DCtx* dctx, size_t chunkIndex) override;
    void on_decompressChunk_end(ZL_DCtx* dctx, ZL_Report result) override;

    void on_ZL_Decoder_getCodecHeader(
            const ZL_Decoder* dictx,
            const void* trh,
            size_t trhSize) override;

    void on_codecDecode_start(
            ZL_Decoder* dictx,
            const ZL_Data* const* inStreams,
            size_t nbInStreams) override;
    void on_codecDecode_end(
            ZL_Decoder* dictx,
            const ZL_Data* const* outStreams,
            size_t nbOutStreams,
            ZL_Report result) override;

   private:
    std::vector<std::vector<StreamdumpEntry>> latestStreamdumpCache_;
    std::string latestTraceCache_;

    bool showStreamPreview_ = true;
    std::unique_ptr<DecompressTracer> tracer_;
};

} // namespace openzl::visualizer
