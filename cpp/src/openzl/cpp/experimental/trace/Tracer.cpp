// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "cpp/src/openzl/cpp/experimental/trace/Tracer.hpp"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/logging.h"
#include "openzl/compress/dyngraph_interface.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/experimental/trace/CborHelpers.hpp"
#include "openzl/cpp/experimental/trace/Codec.hpp"
#include "openzl/cpp/experimental/trace/StreamVisualizer.hpp"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_input.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_output.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_segmenter.h"

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

Tracer::TraceResult Tracer::extractTrace()
{
    return std::move(trace);
}

void Tracer::on_segmenterEncode_start(ZL_Segmenter* segCtx)
{
    if (graphRuns.size() != 0) {
        throw std::runtime_error(
                "Compression tracing does not support multiple segmenters within the same compression");
    }
    segmented              = true;
    const auto nbInStreams = ZL_Segmenter_numInputs(segCtx);

    std::cout << "[SEGM] encode_start";
    // TODO(segm): this may not be necessary if the cctx multiinput start
    // already can do this
    for (size_t i = 0; i < nbInStreams; ++i) {
        const auto* inStream = ZL_Segmenter_getInput(segCtx, i);
        ZL_DataID streamID   = ZL_Input_id(inStream);
        std::cout << " (" << streamID.sid << ", "
                  << ZL_Input_contentSize(inStream) << ")";
        if (nonChunkedRun.streamInfo_.find(streamID)
            == nonChunkedRun.streamInfo_.end()) {
            const ZL_Type stype                      = ZL_Input_type(inStream);
            nonChunkedRun.streamInfo_[streamID].type = stype;
            nonChunkedRun.codecOutEdges_[0].push_back(streamID);
        }
    }
    std::cout << std::endl;

    // A segmenter is technically a graph.
    // However, they behave more like a dispatch codec in the trace because they
    // ingest the in streams and send them to a successor graph.
    Codec newCodec{ .name  = "segmenter", // TODO(segm): expose segmenter name
                    .cType = false,
                    .cID   = 0, // eh?
                    .cHeaderSize = 0,
                    .cLocalParams =
                            LocalParams(*ZL_Segmenter_getLocalParams(segCtx)) };
    nonChunkedRun.codecInfo_.push_back(newCodec);
    for (size_t i = 0; i < nbInStreams; ++i) {
        ZL_DataID streamID = ZL_Input_id(ZL_Segmenter_getInput(segCtx, i));
        nonChunkedRun.codecInEdges_[nonChunkedRun.currCodecNum_].push_back(
                streamID); // set input streams of this codec
        nonChunkedRun.streamConsumerCodec_[streamID] =
                nonChunkedRun.currCodecNum_; // set consumer codec number of
                                             // this streams to retrieve header
                                             // number in cSize calculation
    }
}

void Tracer::on_segmenterEncode_end(ZL_Segmenter* segCtx, ZL_Report r)
{
    if (ZL_isError(r)) {
        nonChunkedRun.codecInfo_[nonChunkedRun.currCodecNum_].cFailure = r;
    }
    ++nonChunkedRun.currCodecNum_;
}

void Tracer::on_ZL_Segmenter_processChunk_start(
        ZL_Segmenter* segCtx,
        const size_t[],
        size_t,
        ZL_GraphID,
        const ZL_RuntimeGraphParameters*)
{
    graphRuns.emplace_back();
    currChunk = &(graphRuns[graphRuns.size() - 1]);
    currChunk->initTrace();
}

void Tracer::on_ZL_Segmenter_processChunk_end(ZL_Segmenter*, ZL_Report)
{
    currChunk = &nonChunkedRun;
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
    // streamdump needs to remain in Tracer as it accesses trace member
    for (size_t i = 0; i < nbOutputs; ++i) {
        streamdump(outStreams[i]);
    }
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
    std::cout << "CCTX_START";
    for (size_t i = 0; i < nbInputs; ++i) {
        std::cout << " (" << ZL_Input_id(inputs[i]).sid << ", "
                  << ZL_Input_contentSize(inputs[i]) << ")";
    }
    std::cout << std::endl;

    frameVersion = ZL_CCtx_getParameter(cctx, ZL_CParam_formatVersion);
    currChunk    = &nonChunkedRun;
    currChunk->initTrace();
}

void Tracer::on_ZL_CCtx_compressMultiTypedRef_end(
        ZL_CCtx const* const,
        ZL_Report const result)
{
    // TODO(segm): check if there was a segmenter run. If so, this processing
    // needs to happen only on the last chunk

    // If the compression is successful, we can assume all the streams
    // without targets go to STORE
    nonChunkedRun.finalizeTrace(result);

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
    // TODO(segm): new format to write vector of streamum
    A1C_Item* root = A1C_Item_root(a1c_arena);
    ZL_RET_R_IF_NULL(allocation, root);
    /* want 3 inner maps:
     * 1. streams and their associated metadata
     * 2. codecs and their associated metadata, and their in/out edges
     * 3. graph info, specifically, which codecs and edges are within a
     * graph
     */
    A1C_MapBuilder rootBuilder = A1C_Item_map_builder(root, 6, a1c_arena);

    ZL_RET_R_IF_NULL(allocation, rootBuilder.map);

    ZL_RET_R_IF_ERR(addIntValue(rootBuilder, "libraryVersion", libraryVersion));
    ZL_RET_R_IF_ERR(addIntValue(rootBuilder, "frameVersion", frameVersion));
    ZL_RET_R_IF_ERR(addIntValue(rootBuilder, "traceVersion", traceVersion));

    // 1. Make the streams map
    A1C_MAP_TRY_ADD_R(streamsPair, rootBuilder);
    A1C_Item_string_refCStr(&streamsPair->key, "streams");
    A1C_ArrayBuilder streamsBuilder = A1C_Item_array_builder(
            &streamsPair->val, nonChunkedRun.streamInfo_.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, streamsBuilder.array);
    for (auto& stream : nonChunkedRun.streamInfo_) {
        A1C_ARRAY_TRY_ADD_R(a1c_stream, streamsBuilder);
        ZL_RET_R_IF_ERR(stream.second.serializeStream(a1c_arena, a1c_stream));
    }

    // 2. Make the codecs map + their in and out edges as part of metadata
    A1C_MAP_TRY_ADD_R(codecsPair, rootBuilder);
    A1C_Item_string_refCStr(&codecsPair->key, "codecs");
    A1C_ArrayBuilder codecsBuilder = A1C_Item_array_builder(
            &codecsPair->val, nonChunkedRun.codecInfo_.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, codecsBuilder.array);
    for (size_t codecNum = 0; codecNum < nonChunkedRun.codecInfo_.size();
         ++codecNum) {
        Codec& codec = nonChunkedRun.codecInfo_[codecNum];
        A1C_ARRAY_TRY_ADD_R(a1c_codec, codecsBuilder);
        ZL_RET_R_IF_ERR(codec.serializeCodec(
                a1c_arena,
                a1c_codec,
                cctx_,
                nonChunkedRun.codecInEdges_[codecNum],
                nonChunkedRun.codecOutEdges_[codecNum]));
    }

    // 3. graph info map
    A1C_MAP_TRY_ADD_R(graphsPair, rootBuilder);
    A1C_Item_string_refCStr(&graphsPair->key, "graphs");
    A1C_ArrayBuilder graphsBuilder = A1C_Item_array_builder(
            &graphsPair->val, nonChunkedRun.graphInfo_.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, graphsBuilder.array);
    for (auto& [graph, codecIDs] : nonChunkedRun.graphInfo_) {
        A1C_ARRAY_TRY_ADD_R(a1c_graph, graphsBuilder);
        ZL_RET_R_IF_ERR(
                graph.serializeGraph(a1c_arena, a1c_graph, cctx_, codecIDs));
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

void Tracer::streamdump(const ZL_Output* createdStream)
{
    auto content = std::string(
            (const char*)ZL_Output_constPtr(createdStream),
            ZL_validResult(ZL_Output_contentSize(createdStream)));
    std::string strLens = "";
    if (ZL_Output_type(createdStream) == ZL_Type_string) {
        auto ptr = ZL_Output_constStringLens(createdStream);
        strLens  = std::string(
                (const char*)ptr,
                ZL_validResult(ZL_Output_numElts(createdStream))
                        * sizeof(ptr[0]));
    }
    trace.streamdump[ZL_Output_id(createdStream).sid] = { content, strLens };
}

} // namespace openzl::visualizer
