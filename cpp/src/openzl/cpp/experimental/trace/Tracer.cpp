// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "openzl/cpp/experimental/trace/Tracer.hpp"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/logging.h"
#include "openzl/compress/dyngraph_interface.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/experimental/trace/CborHelpers.hpp"
#include "openzl/cpp/experimental/trace/Codec.hpp"
#include "openzl/cpp/experimental/trace/StreamVisualizer.hpp"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_output.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openzl::visualizer {

static constexpr size_t MAIN_TRACE_IDX = 0;

Tracer::TraceResult Tracer::extractTrace()
{
    // Aggregate streamdump from all chunks
    trace.streamdump.reserve(graphRuns.size());
    for (auto& chunk : graphRuns) {
        auto streamdump                  = chunk.getStreamdump();
        std::vector<StreamdumpEntry> tmp = {};
        tmp.reserve(streamdump.size());
        for (auto& [k, v] : streamdump) {
            tmp.push_back(
                    StreamdumpEntry{ std::move(k),
                                     std::move(v.first),
                                     std::move(v.second) });
        }
        trace.streamdump.push_back(tmp);
    }
    return std::move(trace);
}

void Tracer::on_segmenterEncode_start(ZL_Segmenter* segCtx)
{
    if (graphRuns.size() != 1) {
        throw std::runtime_error(
                "Compression tracing does not support multiple segmenters within the same compression");
    }
    segmented = true;
    graphRuns[MAIN_TRACE_IDX].on_segmenterEncode_start(segCtx);
}

void Tracer::on_segmenterEncode_end(ZL_Segmenter* segCtx, ZL_Report r)
{
    currChunk = &graphRuns[MAIN_TRACE_IDX];
    currChunk->on_segmenterEncode_end(segCtx, r);
}

void Tracer::on_ZL_Segmenter_processChunk_start(
        ZL_Segmenter* segCtx,
        const size_t numElts[],
        size_t numInputs,
        ZL_GraphID startingGraphID,
        const ZL_RuntimeGraphParameters* rGraphParams)
{
    auto chunkNum = graphRuns.size();
    graphRuns.emplace_back(chunkNum);
    currChunk = &(graphRuns[chunkNum]);
    currChunk->on_ZL_Segmenter_processChunk_start(
            segCtx, numElts, numInputs, startingGraphID, rGraphParams);
}

void Tracer::on_ZL_Segmenter_processChunk_end(ZL_Segmenter* segCtx, ZL_Report r)
{
    currChunk->on_ZL_Segmenter_processChunk_end(segCtx, r);
}

void Tracer::on_codecEncode_start(
        ZL_Encoder* encoder,
        const ZL_Compressor* compressor,
        ZL_NodeID nid,
        const ZL_Input* inStreams[],
        size_t nbInStreams)
{
    currChunk->on_codecEncode_start(
            encoder, compressor, nid, inStreams, nbInStreams);
}

void Tracer::on_codecEncode_end(
        ZL_Encoder* encoder,
        const ZL_Output* outStreams[],
        size_t nbOutputs,
        ZL_Report codecExecResult)
{
    currChunk->on_codecEncode_end(
            encoder, outStreams, nbOutputs, codecExecResult);
}

void Tracer::on_ZL_Encoder_getScratchSpace(ZL_Encoder* ei, size_t size)
{
    currChunk->on_ZL_Encoder_getScratchSpace(ei, size);
}

void Tracer::on_ZL_Encoder_sendCodecHeader(
        ZL_Encoder* encoder,
        const void* trh,
        size_t trhSize)
{
    currChunk->on_ZL_Encoder_sendCodecHeader(encoder, trh, trhSize);
}

void Tracer::on_ZL_Encoder_createTypedStream(
        ZL_Encoder*,
        int,
        size_t eltsCapacity,
        size_t eltWidth,
        ZL_Output* createdStream)
{
}

void Tracer::on_migraphEncode_start(
        ZL_Graph* graph,
        const ZL_Compressor* compressor,
        ZL_GraphID gid,
        ZL_Edge* edges[],
        size_t nbEdges)
{
    currChunk->on_migraphEncode_start(graph, compressor, gid, edges, nbEdges);
}

void Tracer::on_migraphEncode_end(
        ZL_Graph* graph,
        ZL_GraphID successorGraphs[],
        size_t nbSuccessors,
        ZL_Report graphExecResult)
{
    currChunk->on_migraphEncode_end(
            graph, successorGraphs, nbSuccessors, graphExecResult);
}

void Tracer::on_cctx_convertOneInput(
        const ZL_CCtx* const cctx,
        const ZL_Data* const input,
        const ZL_Type inType,
        const ZL_Type portTypeMask,
        const ZL_Report conversionResult)
{
    currChunk->on_cctx_convertOneInput(
            cctx, input, inType, portTypeMask, conversionResult);
}

void Tracer::on_ZL_Graph_getScratchSpace(ZL_Graph*, size_t) {}

void Tracer::on_ZL_Edge_setMultiInputDestination_wParams(
        ZL_Graph*,
        ZL_Edge*[],
        size_t,
        ZL_GraphID,
        const ZL_LocalParams*)
{
}

void Tracer::on_ZL_CCtx_compressMultiTypedRef_start(
        ZL_CCtx const* const cctx,
        void const* const dst,
        size_t const dstCapacity,
        ZL_TypedRef const* const inputs[],
        size_t const nbInputs)
{
    frameVersion = ZL_CCtx_getParameter(cctx, ZL_CParam_formatVersion);
    // The "main" trace is located at idx 0 of graphRuns
    graphRuns.emplace_back(MAIN_TRACE_IDX);
    currChunk = &graphRuns[MAIN_TRACE_IDX];
    currChunk->initTrace();
}

void Tracer::on_ZL_CCtx_compressMultiTypedRef_end(
        ZL_CCtx const* const,
        ZL_Report const result)
{
    // If the compression is successful, we can assume all the streams
    // without targets go to STORE
    graphRuns[MAIN_TRACE_IDX].finalizeTrace(result);

    // convert compression data into a1c_items to write to a CBOR file
    Arena* arena        = ALLOC_HeapArena_create();
    A1C_Arena a1c_arena = A1C_Arena_wrap(arena);
    std::vector<uint8_t> buffer;
    // Serialize the streamdump content in CBOR format
    auto res = serializeStreamdumpToCbor(&a1c_arena, buffer);
    if (ZL_isError(res)) {
        ZL_LOG(ERROR, "Failed to serialize streamdump content!");
        throw std::runtime_error("Failed to serialize streamdump content.");
    }
    ALLOC_Arena_freeArena(arena);
    // Write the serialized streamdump content to a file
    if (ZL_isError(writeSerializedStreamdump(buffer))) {
        ZL_LOG(ERROR,
               "Failed to write serialized streamdump content to a file!");
        throw std::runtime_error(
                "Failed to write serialize streamdump content into a CBOR file.");
    }
}

void Tracer::setCompressedSize(size_t compressionResultSize)
{
    compressedSize_ = compressionResultSize;
}

static bool writeToFile(const std::vector<uint8_t>& buffer, std::ostream& out)
{
    if (!out.good()) {
        return false;
    }
    std::string bufferString(buffer.begin(), buffer.end());
    out.write(bufferString.c_str(), bufferString.size());
    out.flush();
    if (out.fail()) {
        return false;
    }

    return true;
}

ZL_Report Tracer::serializeStreamdumpToCbor(
        A1C_Arena* a1c_arena,
        std::vector<uint8_t>& buffer)
{
    A1C_Item* root = A1C_Item_root(a1c_arena);
    ZL_RET_R_IF_NULL(allocation, root);
    /* want 3 inner maps:
     * 1. streams and their associated metadata
     * 2. codecs and their associated metadata, and their in/out edges
     * 3. graph info, specifically, which codecs and edges are within a
     * graph
     */
    A1C_MapBuilder rootBuilder = A1C_Item_map_builder(root, 4, a1c_arena);

    ZL_RET_R_IF_NULL(allocation, rootBuilder.map);

    ZL_RET_R_IF_ERR(addIntValue(rootBuilder, "libraryVersion", libraryVersion));
    ZL_RET_R_IF_ERR(addIntValue(rootBuilder, "frameVersion", frameVersion));
    ZL_RET_R_IF_ERR(addIntValue(rootBuilder, "traceVersion", traceVersion));

    // Wrap streams, codecs, and graphs in a "chunks" array
    // Non-segmented runs will have 1 chunk in idx 0.
    // Segmented runs will contain the "main" trace in idx 0 and individual
    // chunks in idx 1+. By the "main" trace we mean the trace that includes the
    // start of the compression, up to the segmenter but not including child
    // chunk traces.
    A1C_MAP_TRY_ADD_R(chunksPair, rootBuilder);
    A1C_Item_string_refCStr(&chunksPair->key, "chunks");
    A1C_ArrayBuilder chunksBuilder = A1C_Item_array_builder(
            &chunksPair->val, 1 + graphRuns.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, chunksBuilder.array);

    // Add additional chunks from graphRuns
    for (auto& graphRun : graphRuns) {
        ZL_RET_R_IF_ERR(
                graphRun.serializeToCBOR(a1c_arena, &chunksBuilder, cctx_));
    }

    // encode + write data to a buffer
    size_t encodedSize = A1C_Item_encodedSize(root);
    buffer.resize(encodedSize);
    A1C_Error error;
    size_t bytesWritten =
            A1C_Item_encode(root, buffer.data(), encodedSize, &error);
    if (bytesWritten == 0) {
        ZL_RET_R_WRAP_ERR(A1C_Error_convert(NULL, error));
    }
    ZL_RET_R_IF_NE(allocation, bytesWritten, encodedSize);

    return ZL_returnSuccess();
}

ZL_Report Tracer::writeSerializedStreamdump(std::vector<uint8_t>& buffer)
{
    std::stringstream ss;
    bool successfulWrite = writeToFile(buffer, ss);
    if (!successfulWrite) {
        ZL_RET_R_ERR(GENERIC, "Failed to write to streamdump CBOR file");
    }
    trace.trace = ss.str();
    ZL_LOG(ALWAYS, "Successfully wrote streamdump CBOR");

    return ZL_returnSuccess();
}

} // namespace openzl::visualizer
