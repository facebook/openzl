// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "openzl/cpp/experimental/trace/Codec.hpp"
#include "openzl/cpp/experimental/trace/Graph.hpp"
#include "openzl/cpp/experimental/trace/StreamVisualizer.hpp"

namespace openzl::visualizer {

/**
 * The structures to take one trace of one top-level graph run (aka one that
 * ingests stream 0). A chunked compression will have multiple such top-level
 * invocations.
 */
class ChunkTrace {
   public:
    ChunkTrace() = default;

    /**
     * Callback function to start the trace.
     * - Placeholder start node is inserted
     * - (Maybe?) start streams are recorded
     */
    void initTrace();

    /**
     * Callback to record the first streams (aka user-input). They are never
     * surfaced to the hooks (properly) until either a codec or segmenter takes
     * them in as input.
     */
    void recordStartStreams(const ZL_Input* inStreams[], size_t numInStreams);

    /**
     * Callback function to clean up the trace.
     * - Unconsumed streams go to store or in-progress
     * - CSize is calculated
     * - Metadata is printed
     */
    void finalizeTrace(ZL_Report const result);

    ZL_Report serializeToCBOR(
            A1C_Arena* a1c_arena,
            A1C_ArrayBuilder* chunkArrayBuilder,
            const ZL_CCtx* cctx);

    size_t getCompressedSize();

    /* ************** Trampolined hook calls ************** */
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
            const ZL_Data* const input,
            const ZL_Type inType,
            const ZL_Type portTypeMask,
            const ZL_Report conversionResult);

    struct ConversionError {
        ZL_DataID streamId;
        ZL_Report failureReport;
    };

    // TODO(segm): make this all private
   private:
    void printStreamMetadata();
    void printCodecMetadata();
    size_t fillCSize(std::vector<size_t>& cSize, const ZL_DataID streamID);

    size_t compressedSize_{}; // compressed size for this specific chunk
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
};

} // namespace openzl::visualizer
