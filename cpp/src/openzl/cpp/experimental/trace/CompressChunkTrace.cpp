// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "openzl/cpp/experimental/trace/CompressChunkTrace.hpp"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/compress/dyngraph_interface.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/experimental/trace/CborHelpers.hpp"
#include "openzl/zl_compress.h"
#include "openzl/zl_input.h"
#include "openzl/zl_output.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_segmenter.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <variant>

namespace openzl::visualizer {

void CompressChunkTrace::recordStartStreams(
        const ZL_Input* inStreams[],
        size_t numInStreams)
{
    for (size_t i = 0; i < numInStreams; ++i) {
        StreamID streamID = ZL_Input_id(inStreams[i]);
        if (streamInfo_.find(streamID) == streamInfo_.end()) {
            ZL_Type type       = ZL_Input_type(inStreams[i]);
            size_t eltWidth    = ZL_Input_eltWidth(inStreams[i]);
            size_t numElts     = ZL_Input_numElts(inStreams[i]);
            size_t contentSize = ZL_Input_contentSize(inStreams[i]);

            StreamPreview preview = ChunkTraceCore::getStreamPreview(
                    ZL_Input_ptr(inStreams[i]),
                    type,
                    eltWidth,
                    numElts,
                    ZL_Input_stringLens(inStreams[i]));

            streamInfo_[streamID] = Stream{
                .id            = streamID,
                .type          = type,
                .outputIdx     = i,
                .eltWidth      = eltWidth,
                .numElts       = numElts,
                .contentSize   = contentSize,
                .chunkId       = chunkId_,
                .streamPreview = std::move(preview),
            };
            ChunkTraceCore::createSourceForStream(
                    "zl.#start",
                    streamID,
                    streamInfo_[streamID],
                    codecInfo_,
                    currCodecNum_,
                    chunkId_);
        }
    }
}

void CompressChunkTrace::finalizeTrace(ZL_Report const result)
{
    if (ZL_isError(result)) {
        ZL_LOG(ALWAYS, "Compression not successful!");
        std::cerr << "Compression not successful!" << std::endl;
        ChunkTraceCore::finalizeUnconsumedStreams(
                "zl.#in_progress",
                streamInfo_,
                codecInfo_,
                currCodecNum_,
                chunkId_);
        // Apply conversion error to the relevant terminal codec
        if (maybeConversionError_.has_value()) {
            auto it = streamInfo_.find(maybeConversionError_->streamId);
            if (it != streamInfo_.end()
                && it->second.consumerCodec.has_value()) {
                codecInfo_[it->second.consumerCodec.value()].cFailure =
                        maybeConversionError_->failureReport;
            }
        }
    } else {
        compressedSize_ = ZL_validResult(result);
        ChunkTraceCore::finalizeUnconsumedStreams(
                "zl.store", streamInfo_, codecInfo_, currCodecNum_, chunkId_);
    }

    printStreamMetadata();
    printCodecMetadata();
}

void CompressChunkTrace::resolveErrorStrings(const ZL_CCtx* cctx)
{
    for (auto& codec : codecInfo_) {
        if (ZL_isError(codec.cFailure)) {
            const char* str =
                    ZL_CCtx_getErrorContextString(cctx, codec.cFailure);
            codec.cFailureString = str ? str : "";
        }
    }
    for (auto& graph : graphInfo_) {
        if (ZL_isError(graph.gFailure)) {
            const char* str =
                    ZL_CCtx_getErrorContextString(cctx, graph.gFailure);
            graph.gFailureString = str ? str : "";
        }
    }
}

ZL_Report CompressChunkTrace::serializeToCBOR(
        A1C_Arena* a1c_arena,
        A1C_ArrayBuilder* chunkArrayBuilder)
{
    return ChunkTraceCore::serializeChunkDataToCBOR(
            a1c_arena,
            chunkArrayBuilder,
            chunkId_,
            streamInfo_,
            codecInfo_,
            graphInfo_);
}

size_t CompressChunkTrace::getCompressedSize()
{
    return compressedSize_;
}

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

void CompressChunkTrace::printStreamMetadata()
{
    std::cout << "digraph stream_topo {" << std::endl;
    for (auto& s : streamInfo_) {
        const StreamID streamID = s.first;
        ZL_IDType sid           = streamID.sid;
        ChunkTraceCore::fillCSize(
                streamID, streamInfo_, codecInfo_, compressedSize_);

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

void CompressChunkTrace::printCodecMetadata()
{
    size_t graphInfoIdx = 0;
    for (size_t codecNum = 0; codecNum < codecInfo_.size(); ++codecNum) {
        // identify the start of a graph within our compression tree, if
        // identified, print text required to group codecs and streams
        // together in this graph
        if (graphInfoIdx < graphInfo_.size()
            && codecNum == graphInfo_[graphInfoIdx].codecs.front()) {
            Graph& graph = graphInfo_[graphInfoIdx];
            std::cout << "subgraph cluster_" << graphInfoIdx << '{' << std::endl
                      << "label=\"" << graph.gName
                      << "\\ntype=" << graphTypeToStr(graph.gType);
            if (!graph.gFailureString.empty()) {
                std::cout << "\\nFailure: " << graph.gFailureString;
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
        if (!metadata.cFailureString.empty()) {
            std::cout << "\\n Failure: " << metadata.cFailureString;
        }
        printLocalParams(metadata.cLocalParams);
        std::cout << "\"];" << std::endl;

        // Output the edges from transform to streams
        auto customStreamSort = [](const StreamID& a, const StreamID& b) {
            return a.sid < b.sid;
        };
        std::vector<StreamID> trChildStreams = codecInfo_[codecNum].outEdges;
        std::sort(
                trChildStreams.begin(), trChildStreams.end(), customStreamSort);
        size_t labelNum = trChildStreams.size() - 1;
        for (const auto& stream : trChildStreams) {
            std::cout << "T" << codecNum << " -> S" << stream.sid
                      << "[label=\"#" << labelNum << "\"];" << std::endl;
            --labelNum;
        }

        // Output the stream(s) that are the input for the transform
        std::vector<StreamID> trParentStreams = codecInfo_[codecNum].inEdges;
        labelNum                              = 0;
        std::sort(
                trParentStreams.begin(),
                trParentStreams.end(),
                customStreamSort);
        for (const auto& stream : trParentStreams) {
            std::cout << "S" << stream.sid << " -> T" << codecNum
                      << "[label=\"#" << labelNum << "\"];" << std::endl;
            ++labelNum;
        }
        // last codec in the current graph reached, so move to next one
        if (graphInfoIdx < graphInfo_.size()
            && codecNum == graphInfo_[graphInfoIdx].codecs.back()) {
            std::cout << '}' << std::endl;
            ++graphInfoIdx;
        }
    }

    std::cout << "}" << std::endl;
}

void CompressChunkTrace::on_codecEncode_start(
        ZL_Encoder* encoder,
        const ZL_Compressor* compressor,
        ZL_NodeID nid,
        const ZL_Input* inStreams[],
        size_t nbInStreams)
{
    recordStartStreams(inStreams, nbInStreams);
    // set codec metadata
    Codec newCodec{ .name  = ZL_Compressor_Node_getName(compressor, nid),
                    .cType = ZL_Compressor_Node_isStandard(compressor, nid),
                    .cID   = ZL_Compressor_Node_getCodecID(compressor, nid),
                    .cHeaderSize = 0,
                    .cLocalParams =
                            LocalParams(*ZL_Encoder_getLocalParams(encoder)),
                    .chunkId = chunkId_ };
    newCodec.codecNum = currCodecNum_;
    codecInfo_.push_back(newCodec);
    for (size_t i = 0; i < nbInStreams; ++i) {
        StreamID streamID = ZL_Input_id(inStreams[i]);
        codecInfo_[currCodecNum_].inEdges.push_back(
                streamID); // set input streams of this codec
        streamInfo_[streamID].consumerCodec =
                currCodecNum_; // set consumer codec of this
                               // stream to retrieve header
                               // in cSize calculation
    }
    // add codec to associated graph if applicable
    if (currEncompassingGraph_) {
        graphInfo_.back().codecs.push_back(currCodecNum_);
    }
}

void CompressChunkTrace::on_codecEncode_end(
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
        StreamID streamID              = ZL_Output_id(createdStream);
        ZL_Type type                   = ZL_Output_type(createdStream);
        size_t eltWidth = openzl::unwrap(ZL_Output_eltWidth(createdStream));
        size_t numElts  = openzl::unwrap(ZL_Output_numElts(createdStream));
        size_t contentSize =
                openzl::unwrap(ZL_Output_contentSize(createdStream));

        StreamPreview preview = ChunkTraceCore::getStreamPreview(
                ZL_Output_constPtr(createdStream),
                type,
                eltWidth,
                numElts,
                ZL_Output_constStringLens(createdStream));

        streamInfo_[streamID] = {
            .id            = streamID,
            .type          = type,
            .outputIdx     = i,
            .eltWidth      = eltWidth,
            .numElts       = numElts,
            .contentSize   = contentSize,
            .chunkId       = chunkId_,
            .streamPreview = std::move(preview),
        };

        streamInfo_[streamID].producerCodec = currCodecNum_;
        codecInfo_[currCodecNum_].outEdges.push_back(streamID);
        streamdump(outStreams[i]);
    }

    // connect stream successors for cSize calculation
    for (const auto& inStreamID : codecInfo_[currCodecNum_].inEdges) {
        streamInfo_[inStreamID].successors = codecInfo_[currCodecNum_].outEdges;
    }

    ++currCodecNum_;
}

void CompressChunkTrace::on_ZL_Encoder_getScratchSpace(
        ZL_Encoder* /*ei*/,
        size_t /*size*/)
{
}

void CompressChunkTrace::on_ZL_Encoder_sendCodecHeader(
        ZL_Encoder* /*encoder*/,
        const void* /*trh*/,
        size_t trhSize)
{
    codecInfo_[currCodecNum_].cHeaderSize = trhSize;
}

void CompressChunkTrace::on_ZL_Encoder_createTypedStream(
        ZL_Encoder* /*encoder*/,
        int /*outStreamIndex*/,
        size_t /*eltsCapacity*/,
        size_t /*eltWidth*/,
        ZL_Output* /*createdStream*/)
{
}

void CompressChunkTrace::on_migraphEncode_start(
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
    Graph currGraph =
            Graph{ .gType        = ZL_Compressor_getGraphType(compressor, gid),
                   .gName        = ZL_Compressor_Graph_getName(compressor, gid),
                   .gFailure     = ZL_returnSuccess(),
                   .gLocalParams = LocalParams(*GCTX_getAllLocalParams(graph)),
                   .chunkId      = chunkId_,
                   .inEdges      = std::move(inEdges) };
    graphInfo_.push_back(std::move(currGraph));
}

void CompressChunkTrace::on_migraphEncode_end(
        ZL_Graph*,
        ZL_GraphID[],
        size_t,
        ZL_Report graphExecResult)
{
    if (ZL_isError(graphExecResult)) {
        bool codecsHaveErrors = std::accumulate(
                graphInfo_.back().codecs.begin(),
                graphInfo_.back().codecs.end(),
                false,
                [this](bool acc, CodecID codecId) {
                    return acc || ZL_isError(codecInfo_[codecId].cFailure);
                });
        if (!codecsHaveErrors) {
            // Only report failures that occur outside individual codec
            // executions
            graphInfo_.back().gFailure = graphExecResult;
            // also add an "in-progress" placeholder if there are no codecs
            if (graphInfo_.back().codecs.size() == 0) {
                Codec inProgress = {
                    .name         = "zl.#in_progress",
                    .cType        = true, // standard
                    .cID          = 0,
                    .cHeaderSize  = 0,
                    .cLocalParams = {},
                    .chunkId      = chunkId_,
                };
                inProgress.codecNum = currCodecNum_;
                codecInfo_.push_back(std::move(inProgress));
                graphInfo_.back().codecs.push_back(currCodecNum_);
                for (auto edge : graphInfo_.back().inEdges) {
                    auto data                     = ZL_Edge_getData(edge);
                    auto id                       = ZL_Input_id(data);
                    streamInfo_[id].consumerCodec = currCodecNum_;
                    codecInfo_[currCodecNum_].inEdges.push_back(id);
                }
                ++currCodecNum_;
            }
        }
        currEncompassingGraph_ = false;
        return;
    }
    // If the graph didn't have any codecs between it, we don't want to
    // report it
    if (graphInfo_.size() > 0 && graphInfo_.back().codecs.size() == 0) {
        graphInfo_.pop_back();
    }
    currEncompassingGraph_ = false;
}

void CompressChunkTrace::on_cctx_convertOneInput(
        const ZL_CCtx* const /*cctx*/,
        const ZL_Data* const input,
        const ZL_Type /*inType*/,
        const ZL_Type /*portTypeMask*/,
        const ZL_Report conversionResult)
{
    if (ZL_isError(conversionResult)) {
        maybeConversionError_ = {
            .streamId      = ZL_Data_id(input),
            .failureReport = conversionResult,
        };
    }
}

void CompressChunkTrace::on_segmenterEncode_start(ZL_Segmenter* segCtx)
{
    const auto nbInStreams = ZL_Segmenter_numInputs(segCtx);

    // This is necessary because the cctx multiInput start doesn't use real
    // stream IDs, so these aren't recorded anywhere
    if (ZL_Input_id(ZL_Segmenter_getInput(segCtx, 0)).sid == 0) {
        std::vector<const ZL_Input*> streams;
        for (size_t i = 0; i < nbInStreams; ++i) {
            streams.push_back(ZL_Segmenter_getInput(segCtx, i));
        }
        recordStartStreams(streams.data(), nbInStreams);
    }

    // A segmenter is technically a graph.
    // However, they behave more like a dispatch codec in the trace because they
    // ingest the in streams and send them to a successor graph.
    Codec newCodec{ .name  = "segmenter", // TODO(segm): expose segmenter name
                    .cType = false,
                    .cID   = 0, // eh?
                    .cHeaderSize = 0,
                    .cLocalParams =
                            LocalParams(*ZL_Segmenter_getLocalParams(segCtx)),
                    .chunkId = chunkId_ };
    newCodec.codecNum = currCodecNum_;
    codecInfo_.push_back(newCodec);
    for (size_t i = 0; i < nbInStreams; ++i) {
        StreamID streamID = ZL_Input_id(ZL_Segmenter_getInput(segCtx, i));
        codecInfo_[currCodecNum_].inEdges.push_back(
                streamID); // set input streams of this codec
        streamInfo_[streamID].consumerCodec =
                currCodecNum_; // set consumer codec of
                               // this stream to retrieve header
                               // in cSize calculation
    }
}

void CompressChunkTrace::on_segmenterEncode_end(
        ZL_Segmenter* /*segCtx*/,
        ZL_Report r)
{
    if (ZL_isError(r)) {
        codecInfo_[currCodecNum_].cFailure = r;
    }
}

void CompressChunkTrace::on_ZL_Segmenter_processChunk_start(
        ZL_Segmenter* /*segCtx*/,
        const size_t /*numElts*/[],
        size_t /*numInputs*/,
        ZL_GraphID /*startingGraphID*/,
        const ZL_RuntimeGraphParameters* /*rGraphParams*/)
{
}

void CompressChunkTrace::on_ZL_Segmenter_processChunk_end(
        ZL_Segmenter* /*segCtx*/,
        ZL_Report r)
{
    finalizeTrace(r);
}

void CompressChunkTrace::streamdump(const ZL_Output* createdStream)
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
    streamdump_[ZL_Output_id(createdStream).sid] = { content, strLens };
}
std::map<size_t, std::pair<std::string, std::string>>&&
CompressChunkTrace::getStreamdump()
{
    return std::move(streamdump_);
}
} // namespace openzl::visualizer
