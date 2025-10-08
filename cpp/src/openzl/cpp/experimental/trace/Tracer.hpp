// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <map>
#include <optional>

#include "openzl/cpp/experimental/trace/Codec.hpp"
#include "openzl/cpp/experimental/trace/Graph.hpp"
#include "openzl/cpp/experimental/trace/StreamVisualizer.hpp"

namespace openzl::visualizer {

class Tracer {
   public:
    explicit Tracer(const ZL_CCtx* const cctx) : cctx_(cctx) {}

    struct TraceResult {
        std::string trace;
        std::map<size_t, std::pair<std::string, std::string>> streamdump;
    };

    TraceResult extractTrace();

    // Trampolined functions from CompressionTraceHooks
    void on_codecEncode_start(
            ZL_Encoder* encoder,
            const ZL_Compressor* compressor,
            ZL_NodeID nid,
            const ZL_Input* inStreams[],
            size_t nbInStreams);

    void on_codecEncode_end(
            ZL_Encoder*,
            const ZL_Output* outStreams[],
            size_t nbOutputs,
            ZL_Report codecExecResult);

    void on_ZL_Encoder_getScratchSpace(ZL_Encoder* ei, size_t size);

    void on_ZL_Encoder_sendCodecHeader(
            ZL_Encoder* encoder,
            const void* trh,
            size_t trhSize);

    void on_ZL_Encoder_createTypedStream(
            ZL_Encoder* encoder,
            int outStreamIndex,
            size_t eltsCapacity,
            size_t eltWidth,
            ZL_Output* createdStream);

    void on_migraphEncode_start(
            ZL_Graph* graph,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge* inputs[],
            size_t nbInputs);

    void on_migraphEncode_end(
            ZL_Graph*,
            ZL_GraphID successorGraphs[],
            size_t nbSuccessors,
            ZL_Report graphExecResult);

    void on_cctx_convertOneInput(
            const ZL_CCtx* const cctx,
            const ZL_Data* const,
            const ZL_Type inType,
            const ZL_Type portTypeMask,
            const ZL_Report conversionResult);

    void on_ZL_Graph_getScratchSpace(ZL_Graph* graph, size_t size);

    void on_ZL_Edge_setMultiInputDestination_wParams(
            ZL_Graph* graph,
            ZL_Edge* inputs[],
            size_t nbInputs,
            ZL_GraphID gid,
            const ZL_LocalParams* lparams);

    void on_ZL_CCtx_compressMultiTypedRef_start(
            ZL_CCtx const* const cctx,
            void const* const dst,
            size_t const dstCapacity,
            ZL_TypedRef const* const inputs[],
            size_t const nbInputs);
    void on_ZL_CCtx_compressMultiTypedRef_end(
            ZL_CCtx const* const cctx,
            ZL_Report const result);

   private:
    void printStreamMetadata();
    void printCodecMetadata();

    void streamdump(const ZL_Output* stream);

    ZL_Report serializeStreamdumpToCbor(
            A1C_Arena* a1c_arena,
            std::vector<uint8_t>& buffer);
    ZL_Report writeSerializedStreamdump(std::vector<uint8_t>& buffer);

    void setCompressedSize(size_t compressionResultSize);
    size_t fillCSize(std::vector<size_t>& cSize, const ZL_DataID streamID);

    struct ConversionError {
        ZL_DataID streamId;
        ZL_Report failureReport;
    };

    const ZL_CCtx* cctx_{};
    size_t compressedSize_{};
    size_t currCodecNum_ = 0;
    std::map<ZL_DataID, Stream, ZL_DataIDCustomComparator> streamInfo_;
    std::vector<Codec> codecInfo_;
    std::unordered_map<size_t, std::vector<ZL_DataID>> codecInEdges_;
    std::unordered_map<size_t, std::vector<ZL_DataID>> codecOutEdges_;
    std::unordered_map<
            ZL_DataID,
            std::vector<ZL_DataID>,
            ZL_DataIDHash,
            ZL_DataIDEquality>
            streamSuccessors_;
    std::unordered_map<ZL_DataID, size_t, ZL_DataIDHash, ZL_DataIDEquality>
            streamConsumerCodec_;
    std::vector<std::pair<Graph, std::vector<size_t>>> graphInfo_;
    bool currEncompassingGraph_ = false; // if codecs are running within a graph
    std::optional<ConversionError> maybeConversionError_ = std::nullopt;

    TraceResult trace;
};

} // namespace openzl::visualizer
