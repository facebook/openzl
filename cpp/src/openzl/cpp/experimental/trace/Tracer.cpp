// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "cpp/src/openzl/cpp/experimental/trace/Tracer.hpp"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/logging.h"
#include "openzl/compress/dyngraph_interface.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/experimental/trace/Codec.hpp"
#include "openzl/cpp/experimental/trace/StreamVisualizer.hpp"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_input.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_output.h"
#include "openzl/zl_reflection.h"

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

inline std::string streamTypeToStr(ZL_Type stype)
{
    switch (stype) {
        case ZL_Type_serial:
            return "Serialized";
        case ZL_Type_struct:
            return "Fixed_Width";
        case ZL_Type_numeric:
            return "Numeric";
        case ZL_Type_string:
            return "Variable_Size";
        default:
            return "default";
    }
}

inline std::string graphTypeToStr(ZL_GraphType gtype)
{
    switch (gtype) {
        case ZL_GraphType_standard:
            return "Standard";
        case ZL_GraphType_static:
            return "Static";
        case ZL_GraphType_selector:
            return "Selector";
        case ZL_GraphType_function:
            return "Function";
        case ZL_GraphType_multiInput:
            return "Multiple_Input";
        case ZL_GraphType_parameterized:
            return "Parameterized";
        case ZL_GraphType_segmenter:
            return "Segmenter";
        default:
            throw std::runtime_error("Unsupported ZL_GraphType value!");
    }
}

Tracer::TraceResult Tracer::extractTrace()
{
    return std::move(trace);
}
void Tracer::on_codecEncode_start(
        ZL_Encoder* encoder,
        const ZL_Compressor* compressor,
        ZL_NodeID nid,
        const ZL_Input* inStreams[],
        size_t nbInStreams)
{
    // for root streams to a compression tree that are not called as output to
    // any codec
    for (size_t i = 0; i < nbInStreams; ++i) {
        ZL_DataID streamID = ZL_Input_id(inStreams[i]);
        if (streamInfo_.find(streamID) == streamInfo_.end()) {
            const ZL_Type stype        = ZL_Input_type(inStreams[i]);
            streamInfo_[streamID].type = stype;
        }
    }
    // set codec metadata
    Codec newCodec{ .name  = ZL_Compressor_Node_getName(compressor, nid),
                    .cType = ZL_Compressor_Node_isStandard(compressor, nid),
                    .cID   = ZL_Compressor_Node_getCodecID(compressor, nid),
                    .cHeaderSize = 0,
                    .cLocalParams =
                            LocalParams(*ZL_Encoder_getLocalParams(encoder)) };
    codecInfo_.push_back(newCodec);
    for (size_t i = 0; i < nbInStreams; ++i) {
        ZL_DataID streamID = ZL_Input_id(inStreams[i]);
        codecInEdges_[currCodecNum_].push_back(
                streamID); // set input streams of this codec
        streamConsumerCodec_[streamID] =
                currCodecNum_; // set consumer codec number of this streams to
                               // retrieve header number in cSize calculation
    }
    // add codec to associated graph if applicable
    if (currEncompassingGraph_) {
        graphInfo_.back().second.push_back(currCodecNum_);
    }
}

void Tracer::on_codecEncode_end(
        ZL_Encoder*,
        const ZL_Output* outStreams[],
        size_t nbOutputs,
        ZL_Report codecExecResult)
{
    if (ZL_isError(codecExecResult)) {
        codecInfo_[currCodecNum_].cFailure = codecExecResult;
    }
    // Note: if the codec failed, we have 0 output streams, so this will be a
    // no-op
    for (size_t i = 0; i < nbOutputs; ++i) {
        // set stream ELT values
        const ZL_Output* createdStream = outStreams[i];
        ZL_DataID streamID             = ZL_Output_id(createdStream);
        streamInfo_[streamID]          = {
                     .type        = ZL_Output_type(createdStream),
                     .outputIdx   = i,
                     .eltWidth    = openzl::unwrap(ZL_Output_eltWidth(createdStream)),
                     .numElts     = openzl::unwrap(ZL_Output_numElts(createdStream)),
                     .contentSize = openzl::unwrap(ZL_Output_contentSize(createdStream)),
        };
        streamdump(createdStream);

        codecOutEdges_[currCodecNum_].push_back(streamID);
    }

    // connect stream successors for cSize calculation
    for (auto streamID : codecInEdges_[currCodecNum_]) {
        streamSuccessors_[streamID] = codecOutEdges_[currCodecNum_];
    }

    ++currCodecNum_;
}

void Tracer::on_ZL_Encoder_getScratchSpace(ZL_Encoder*, size_t)
{
    // std::cout << "function on_ZL_Encoder_getScratchSpace successfully
    // called!"
    //           << std::endl;
}

void Tracer::on_ZL_Encoder_sendCodecHeader(
        ZL_Encoder*,
        const void*,
        size_t trhSize)
{
    codecInfo_[currCodecNum_].cHeaderSize = trhSize;
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
    currEncompassingGraph_ = true;
    std::vector<ZL_Edge*> inEdges;
    inEdges.reserve(nbEdges);
    for (size_t i = 0; i < nbEdges; ++i) {
        inEdges.push_back(edges[i]);
    }
    Graph currGraph = Graph{ ZL_Compressor_getGraphType(compressor, gid),
                             ZL_Compressor_Graph_getName(compressor, gid),
                             ZL_returnSuccess(),
                             LocalParams(*GCTX_getAllLocalParams(graph)),
                             std::move(inEdges) };
    graphInfo_.emplace_back(currGraph, std::vector<size_t>());
}

void Tracer::on_migraphEncode_end(
        ZL_Graph*,
        ZL_GraphID[],
        size_t,
        ZL_Report graphExecResult)
{
    if (ZL_isError(graphExecResult)) {
        bool codecsHaveErrors = std::accumulate(
                graphInfo_.back().second.begin(),
                graphInfo_.back().second.end(),
                false,
                [this](bool acc, size_t codecNum) {
                    return acc || ZL_isError(codecInfo_[codecNum].cFailure);
                });
        if (!codecsHaveErrors) {
            // Only report failures that occur outside individual codec
            // executions
            graphInfo_.back().first.gFailure = graphExecResult;
            // also add an "in-progress" placeholder if there are no codecs
            if (graphInfo_.back().second.size() == 0) {
                graphInfo_.back().second.push_back(currCodecNum_);
                Codec inProgress = {
                    .name         = "zl.#in_progress",
                    .cType        = true, // standard
                    .cID          = 0,
                    .cHeaderSize  = 0,
                    .cLocalParams = {},
                };
                codecInfo_.push_back(std::move(inProgress));
                codecOutEdges_[currCodecNum_] = {};
                for (auto edge : graphInfo_.back().first.inEdges) {
                    auto data                = ZL_Edge_getData(edge);
                    auto id                  = ZL_Input_id(data);
                    streamConsumerCodec_[id] = currCodecNum_;
                    codecInEdges_[currCodecNum_].push_back(id);
                }
                ++currCodecNum_;
            }
        }
        currEncompassingGraph_ = false;
        return;
    }
    // If the graph didn't have any codecs between it, we don't want to
    // report it
    if (graphInfo_.size() > 0 && graphInfo_.back().second.size() == 0) {
        graphInfo_.pop_back();
    }
    currEncompassingGraph_ = false;
}

void Tracer::on_cctx_convertOneInput(
        const ZL_CCtx* const,
        const ZL_Data* const input,
        const ZL_Type,
        const ZL_Type,
        const ZL_Report conversionResult)
{
    if (ZL_isError(conversionResult)) {
        this->maybeConversionError_ = {
            .streamId      = ZL_Data_id(input),
            .failureReport = conversionResult,
        };
    }
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

void Tracer::on_ZL_CCtx_compressMultiTypedRef_end(
        ZL_CCtx const* const,
        ZL_Report const result)
{
    // If the compression is successful, we can assume all the streams
    // without targets go to STORE
    if (ZL_isError(result)) {
        ZL_LOG(ALWAYS, "Compression not successful!");
        std::cerr << "Compression not successful!" << std::endl;
        // temp: in-flight streams go to an "in-progress" node
        for (unsigned i = 0; i < streamInfo_.size(); ++i) {
            const ZL_DataID streamID = { .sid = i };
            if (streamConsumerCodec_.find(streamID)
                == streamConsumerCodec_.end()) {
                Codec inProgress = {
                    .name         = "zl.#in_progress",
                    .cType        = true, // standard
                    .cID          = 0,
                    .cHeaderSize  = 0,
                    .cLocalParams = {},
                };
                if (this->maybeConversionError_.has_value()
                    && this->maybeConversionError_->streamId.sid
                            == streamID.sid) {
                    inProgress.cFailure = maybeConversionError_->failureReport;
                }
                codecInfo_.push_back(std::move(inProgress));
                codecInEdges_[currCodecNum_].push_back(streamID);
                codecOutEdges_[currCodecNum_]  = {};
                streamConsumerCodec_[streamID] = currCodecNum_;
                ++currCodecNum_;
            }
        }
    } else {
        setCompressedSize(ZL_validResult(result));
        for (unsigned i = 0; i < streamInfo_.size(); ++i) {
            const ZL_DataID streamID = { .sid = i };
            if (streamConsumerCodec_.find(streamID)
                == streamConsumerCodec_.end()) {
                Codec store = {
                    .name         = "zl.store",
                    .cType        = true, // standard
                    .cID          = 0,
                    .cHeaderSize  = 0,
                    .cLocalParams = {},
                };
                codecInfo_.push_back(std::move(store));
                codecInEdges_[currCodecNum_].push_back(streamID);
                codecOutEdges_[currCodecNum_]  = {};
                streamConsumerCodec_[streamID] = currCodecNum_;
                ++currCodecNum_;
            }
        }
    }

    printStreamMetadata();
    printCodecMetadata();

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

size_t Tracer::fillCSize(std::vector<size_t>& cSize, const ZL_DataID streamID)
{
    size_t cSize_idx = streamID.sid;
    // already filled up
    if (cSize.size() != 0 && cSize[cSize_idx] != SIZE_MAX) {
        return cSize[cSize_idx];
    }
    // base case: stream has no successors, and is input to the frame
    if (streamSuccessors_.find(streamID) == streamSuccessors_.end()
        || streamSuccessors_.at(streamID).size() == 0) {
        cSize[cSize_idx] = streamInfo_.at(streamID).contentSize;
        return cSize[cSize_idx];
    }

    // Get the header size
    if (streamConsumerCodec_.find(streamID) != streamConsumerCodec_.end()) {
        size_t codecNum  = streamConsumerCodec_.at(streamID);
        cSize[cSize_idx] = codecInfo_.at(codecNum).cHeaderSize;
    } else {
        cSize[cSize_idx] = 0;
    }

    size_t nbSuccessors = streamSuccessors_.at(streamID).size();
    // recursively fill cSize of this stream by summing the cSize of its
    // successor streams
    for (size_t i = 0; i < nbSuccessors; ++i) {
        ZL_DataID successorStreamID = streamSuccessors_.at(streamID)[i];
        cSize[cSize_idx] += fillCSize(cSize, successorStreamID);
    }

    // if the consumer codec has multiple inputs, assume each input provides
    // equal contribution
    cSize[cSize_idx] /= codecInEdges_[streamConsumerCodec_.at(streamID)].size();
    return cSize[cSize_idx];
}

void Tracer::setCompressedSize(size_t compressionResultSize)
{
    compressedSize_ = compressionResultSize;
}

void Tracer::printStreamMetadata()
{
    std::vector<size_t> cSize(streamInfo_.size(), SIZE_MAX);
    std::cout << "digraph stream_topo {" << std::endl;
    for (auto& s : streamInfo_) {
        const ZL_DataID streamID    = s.first;
        ZL_IDType sid               = streamID.sid;
        streamInfo_[streamID].cSize = fillCSize(cSize, streamID);
        streamInfo_[streamID].share =
                static_cast<double>(streamInfo_[streamID].cSize)
                / static_cast<double>(compressedSize_) * 100;

        Stream metadata = s.second;
        std::cout << 'S' << sid << " [shape=record, label=\"Stream: " << sid
                  << "\\nType: " << streamTypeToStr(metadata.type)
                  << "\\nOutputIdx: " << metadata.outputIdx
                  << "\\nEltWidth: " << metadata.eltWidth
                  << "\\n#Elts: " << metadata.numElts
                  << "\\nCSize: " << metadata.cSize << "\\nShare: ";
        {
            std::cout << std::fixed << std::setprecision(2) << metadata.share
                      << "%\"];" << std::endl;
        }
    }
    std::cout << std::endl; // 1 line space between following text
}

// helper function to print out local params
static void printLocalParams(const LocalParams& lpi)
{
    const auto ips = lpi.getIntParams();
    if (ips.size() > 0) {
        std::cout << "\\nIntParams (paramId, paramValue): ";
        std::cout << '(' << ips[0].paramId << ", " << ips[0].paramValue << ')';
        for (size_t i = 1; i < ips.size(); ++i) {
            std::cout << ", ";
            std::cout << '(' << ips[i].paramId << ", " << ips[i].paramValue
                      << ')';
        }
    }
    const auto cps = lpi.getCopyParams();
    if (cps.size() > 0) {
        std::cout << "\\nCopyParams (paramId, paramSize): ";
        std::cout << '(' << cps[0].paramId << ", " << cps[0].paramSize << ')';
        for (size_t i = 1; i < cps.size(); ++i) {
            std::cout << ", ";
            std::cout << '(' << cps[i].paramId << ", " << cps[i].paramSize
                      << ')';
        }
    }
    const auto rps = lpi.getRefParams();
    if (rps.size() > 0) {
        std::cout << "\\nRefParams (paramId): ";
        std::cout << '(' << rps[0].paramId << ')';
        for (size_t i = 1; i < rps.size(); ++i) {
            std::cout << ", ";
            std::cout << '(' << rps[i].paramId << ')';
        }
    }
}

void Tracer::printCodecMetadata()
{
    size_t graphInfoIdx = 0;
    for (size_t codecNum = 0; codecNum < codecInfo_.size(); ++codecNum) {
        // identify the start of a graph within our compression tree, if
        // identified, print text required to group codecs and streams
        // together in this graph
        if (graphInfoIdx < graphInfo_.size()
            && codecNum == graphInfo_[graphInfoIdx].second.front()) {
            Graph& graph = graphInfo_[graphInfoIdx].first;
            std::cout << "subgraph cluster_" << graphInfoIdx << '{' << std::endl
                      << "label=\"" << graph.gName
                      << "\\ntype=" << graphTypeToStr(graph.gType);
            if (ZL_isError(graph.gFailure)) {
                std::cout << "\\nFailure: "
                          << ZL_CCtx_getErrorContextString(
                                     cctx_, graph.gFailure);
            }
            printLocalParams(graph.gLocalParams);
            std::cout << "\";" << std::endl << "color=maroon" << std::endl;
        }
        // print general codec metadata
        Codec& metadata       = codecInfo_[codecNum];
        std::string codecName = metadata.cType ? "Standard" : "Custom";
        std::cout << "T" << codecNum << " [shape=Mrecord, label=\""
                  << metadata.name << "(ID: " << metadata.cID << ")\\n "
                  << codecName << " transform " << codecNum
                  << "\\n Header size: " << metadata.cHeaderSize;
        if (ZL_isError(metadata.cFailure)) {
            std::cout << "\\n Failure: "
                      << ZL_CCtx_getErrorContextString(
                                 cctx_, metadata.cFailure);
        }
        printLocalParams(metadata.cLocalParams);
        std::cout << "\"];" << std::endl;

        // Output the edges from transform to streams
        auto customStreamSort = [](const ZL_DataID& a, const ZL_DataID& b) {
            return a.sid < b.sid;
        };
        std::vector<ZL_DataID> trChildStreams = codecOutEdges_[codecNum];
        std::sort(
                trChildStreams.begin(), trChildStreams.end(), customStreamSort);
        size_t labelNum = trChildStreams.size() - 1;
        for (ZL_DataID strID : trChildStreams) {
            std::cout << "T" << codecNum << " -> S" << strID.sid << "[label=\"#"
                      << labelNum << "\"];" << std::endl;
            --labelNum;
        }

        // Output the stream(s) that are the input for the transform
        std::vector<ZL_DataID> trParentStreams = codecInEdges_[codecNum];
        labelNum                               = 0;
        std::sort(
                trParentStreams.begin(),
                trParentStreams.end(),
                customStreamSort);
        for (ZL_DataID strID : trParentStreams) {
            std::cout << "S" << strID.sid << " -> T" << codecNum << "[label=\"#"
                      << labelNum << "\"];" << std::endl;
            ++labelNum;
        }
        // last codec in the current graph reached, so move to next one
        if (graphInfoIdx < graphInfo_.size()
            && codecNum == graphInfo_[graphInfoIdx].second.back()) {
            std::cout << '}' << std::endl;
            ++graphInfoIdx;
        }
    }

    std::cout << "}" << std::endl;
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
    A1C_MapBuilder rootBuilder = A1C_Item_map_builder(root, 3, a1c_arena);
    ZL_RET_R_IF_NULL(allocation, rootBuilder.map);

    // 1. Make the streams map
    A1C_MAP_TRY_ADD_R(streamsPair, rootBuilder);
    A1C_Item_string_refCStr(&streamsPair->key, "streams");
    A1C_ArrayBuilder streamsBuilder = A1C_Item_array_builder(
            &streamsPair->val, streamInfo_.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, streamsBuilder.array);
    for (auto& stream : streamInfo_) {
        A1C_ARRAY_TRY_ADD_R(a1c_stream, streamsBuilder);
        ZL_RET_R_IF_ERR(stream.second.serializeStream(a1c_arena, a1c_stream));
    }

    // 2. Make the codecs map + their in and out edges as part of metadata
    A1C_MAP_TRY_ADD_R(codecsPair, rootBuilder);
    A1C_Item_string_refCStr(&codecsPair->key, "codecs");
    A1C_ArrayBuilder codecsBuilder = A1C_Item_array_builder(
            &codecsPair->val, codecInfo_.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, codecsBuilder.array);
    for (size_t codecNum = 0; codecNum < codecInfo_.size(); ++codecNum) {
        Codec& codec = codecInfo_[codecNum];
        A1C_ARRAY_TRY_ADD_R(a1c_codec, codecsBuilder);
        ZL_RET_R_IF_ERR(codec.serializeCodec(
                a1c_arena,
                a1c_codec,
                cctx_,
                codecInEdges_[codecNum],
                codecOutEdges_[codecNum]));
    }

    // 3. graph info map
    A1C_MAP_TRY_ADD_R(graphsPair, rootBuilder);
    A1C_Item_string_refCStr(&graphsPair->key, "graphs");
    A1C_ArrayBuilder graphsBuilder = A1C_Item_array_builder(
            &graphsPair->val, graphInfo_.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, graphsBuilder.array);
    for (auto& [graph, codecIDs] : graphInfo_) {
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
