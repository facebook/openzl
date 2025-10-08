// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/cpp/experimental/trace/Tracer.hpp"
#include "openzl/cpp/poly/StringView.hpp"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_opaque_types.h"

#include <map>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace openzl::visualizer {
class CompressionTraceHooks : public openzl::CompressIntrospectionHooks {
   public:
    CompressionTraceHooks()           = default;
    ~CompressionTraceHooks() override = default;

    std::pair<
            poly::string_view,
            std::map<size_t, std::pair<poly::string_view, poly::string_view>>>
    getLatestTrace();

    // ***************************************************
    // Overridden functions from CompressIntrospectionHooks
    // ***************************************************
    void on_codecEncode_start(
            ZL_Encoder* encoder,
            const ZL_Compressor* compressor,
            ZL_NodeID nid,
            const ZL_Input* inStreams[],
            size_t nbInStreams) override;

    void on_codecEncode_end(
            ZL_Encoder*,
            const ZL_Output* outStreams[],
            size_t nbOutputs,
            ZL_Report codecExecResult) override;

    void on_ZL_Encoder_getScratchSpace(ZL_Encoder* ei, size_t size) override;

    void on_ZL_Encoder_sendCodecHeader(
            ZL_Encoder* encoder,
            const void* trh,
            size_t trhSize) override;

    void on_ZL_Encoder_createTypedStream(
            ZL_Encoder* encoder,
            int outStreamIndex,
            size_t eltsCapacity,
            size_t eltWidth,
            ZL_Output* createdStream) override;

    void on_migraphEncode_start(
            ZL_Graph* graph,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge* inputs[],
            size_t nbInputs) override;

    void on_migraphEncode_end(
            ZL_Graph*,
            ZL_GraphID successorGraphs[],
            size_t nbSuccessors,
            ZL_Report graphExecResult) override;

    void on_cctx_convertOneInput(
            const ZL_CCtx* const cctx,
            const ZL_Data* const,
            const ZL_Type inType,
            const ZL_Type portTypeMask,
            const ZL_Report conversionResult) override;

    void on_ZL_Graph_getScratchSpace(ZL_Graph* graph, size_t size) override;

    void on_ZL_Edge_setMultiInputDestination_wParams(
            ZL_Graph* graph,
            ZL_Edge* inputs[],
            size_t nbInputs,
            ZL_GraphID gid,
            const ZL_LocalParams* lparams) override;

    void on_ZL_CCtx_compressMultiTypedRef_start(
            ZL_CCtx const* const cctx,
            void const* const dst,
            size_t const dstCapacity,
            ZL_TypedRef const* const inputs[],
            size_t const nbInputs) override;
    void on_ZL_CCtx_compressMultiTypedRef_end(
            ZL_CCtx const* const cctx,
            ZL_Report const result) override;

   private:
    std::stringstream outStream_; // output stream to write to
    std::map<size_t, std::pair<std::string, std::string>>
            latestStreamdumpCache_; // cache for latest streamdumps. Key is the
                                    // stream ID, value is a pair of strings
                                    // (content, string lengths (or ""))
    std::string latestTraceCache_;  // cache for latest trace

    std::unique_ptr<Tracer>
            tracer_; // pointer to the actual class that does a trace
};
} // namespace openzl::visualizer
