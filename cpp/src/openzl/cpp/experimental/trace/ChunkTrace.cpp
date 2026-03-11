// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "openzl/cpp/experimental/trace/ChunkTrace.hpp"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/compress/dyngraph_interface.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/experimental/trace/CborHelpers.hpp"
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

using StreamPreview = std::variant<
        std::vector<std::string>,
        std::vector<int64_t>,
        std::vector<uint8_t>>;

static std::vector<int64_t>
getNumericData(const void* data, size_t eltWidth, size_t numElts)
{
    if (data == nullptr || numElts == 0) {
        return {};
    }

    std::vector<int64_t> numericData;
    numericData.reserve(numElts);

    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    for (size_t i = 0; i < numElts; ++i) {
        int64_t val = 0;
        switch (eltWidth) {
            case 1:
                val = reinterpret_cast<const int8_t*>(bytes)[i];
                break;
            case 2:
                val = reinterpret_cast<const int16_t*>(bytes)[i];
                break;
            case 4:
                val = reinterpret_cast<const int32_t*>(bytes)[i];
                break;
            case 8:
                val = reinterpret_cast<const int64_t*>(bytes)[i];
                break;
            default:
                throw std::runtime_error("Unexpected numeric eltWidth!");
        }
        numericData.push_back(val);
    }

    return numericData;
}

static std::vector<std::string>
getStringData(const void* data, size_t numStrings, const uint32_t* stringLens)
{
    if (data == nullptr || stringLens == nullptr || numStrings == 0) {
        return {};
    }

    std::vector<std::string> strings;
    strings.reserve(numStrings);

    const auto* bytes = reinterpret_cast<const char*>(data);
    size_t offset     = 0;

    for (size_t i = 0; i < numStrings; ++i) {
        uint32_t len          = stringLens[i];
        std::string rawStr    = std::string(bytes + offset, len);
        std::string parsedStr = "";
        if (rawStr.find('\n') != std::string::npos
            || rawStr.find('\t') != std::string::npos
            || rawStr.find('\r') != std::string::npos) {
            for (char c : rawStr) {
                switch (c) {
                    case '\n':
                        parsedStr += "\\n";
                        break;
                    case '\t':
                        parsedStr += "\\t";
                        break;
                    case '\r':
                        parsedStr += "\\r";
                        break;
                    default:
                        parsedStr += c;
                        break;
                }
            }
        } else {
            parsedStr = std::move(rawStr);
        }

        strings.push_back(parsedStr);
        offset += len;
    }

    return strings;
}

static std::vector<uint8_t> getSerialData(const void* data, size_t numBytes)
{
    if (data == nullptr || numBytes == 0) {
        return {};
    }

    std::vector<uint8_t> bytes;
    const auto* rawBytes = reinterpret_cast<const uint8_t*>(data);
    bytes.assign(rawBytes, rawBytes + numBytes);

    return bytes;
}

static StreamPreview getStreamPreviewData(
        const void* data,
        ZL_Type type,
        size_t eltWidth,
        size_t numElts,
        const uint32_t* stringLens = nullptr)
{
    switch (type) {
        case ZL_Type_numeric:
            return getNumericData(data, eltWidth, numElts);
        case ZL_Type_string:
            return getStringData(data, numElts, stringLens);
        case ZL_Type_struct:
        case ZL_Type_serial:
            return getSerialData(data, numElts);
        default:
            throw std::runtime_error("Unsupported stream preview type!");
    }
}

void ChunkTrace::initTrace()
{
    Codec compressionStart = {
        .name         = "zl.#start",
        .cType        = true, // standard
        .cID          = 0,
        .cHeaderSize  = 0,
        .cLocalParams = {},
        .chunkId      = chunkId_,
    };
    compressionStart.codecNum = currCodecNum_;
    codecInfo_.push_back(std::move(compressionStart));
    ++currCodecNum_;
}

void ChunkTrace::recordStartStreams(
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

            StreamPreview preview = getStreamPreviewData(
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
            codecInfo_[0].outEdges.push_back(streamID);
        }
    }
}

void ChunkTrace::finalizeTrace(ZL_Report const result)
{
    if (ZL_isError(result)) {
        ZL_LOG(ALWAYS, "Compression not successful!");
        std::cerr << "Compression not successful!" << std::endl;
        // temp: in-flight streams go to an "in-progress" node
        for (unsigned i = 0; i < streamInfo_.size(); ++i) {
            const StreamID streamID = { .sid = i };
            if (!streamInfo_.at(streamID).consumerCodec.has_value()) {
                Codec inProgress = {
                    .name         = "zl.#in_progress",
                    .cType        = true, // standard
                    .cID          = 0,
                    .cHeaderSize  = 0,
                    .cLocalParams = {},
                    .chunkId      = chunkId_,
                };
                if (maybeConversionError_.has_value()
                    && maybeConversionError_->streamId.sid == streamID.sid) {
                    inProgress.cFailure = maybeConversionError_->failureReport;
                }
                inProgress.codecNum = currCodecNum_;
                codecInfo_.push_back(std::move(inProgress));
                codecInfo_[currCodecNum_].inEdges.push_back(streamID);
                streamInfo_[streamID].consumerCodec = currCodecNum_;
                ++currCodecNum_;
            }
        }
    } else {
        compressedSize_ = ZL_validResult(result);
        for (unsigned i = 0; i < streamInfo_.size(); ++i) {
            const StreamID streamID = { .sid = i };
            if (!streamInfo_.at(streamID).consumerCodec.has_value()) {
                Codec store = {
                    .name         = "zl.store",
                    .cType        = true, // standard
                    .cID          = 0,
                    .cHeaderSize  = 0,
                    .cLocalParams = {},
                    .chunkId      = chunkId_,
                };
                store.codecNum = currCodecNum_;
                codecInfo_.push_back(std::move(store));
                codecInfo_[currCodecNum_].inEdges.push_back(streamID);
                streamInfo_[streamID].consumerCodec = currCodecNum_;
                ++currCodecNum_;
            }
        }
    }

    printStreamMetadata();
    printCodecMetadata();
}

ZL_Report ChunkTrace::serializeToCBOR(
        A1C_Arena* a1c_arena,
        A1C_ArrayBuilder* chunkArrayBuilder,
        const ZL_CCtx* cctx)
{
    A1C_ARRAY_TRY_ADD_R(chunkItem, *chunkArrayBuilder);
    A1C_MapBuilder chunkBuilder = A1C_Item_map_builder(chunkItem, 4, a1c_arena);
    ZL_RET_R_IF_NULL(allocation, chunkBuilder.map);

    // Add chunkId to the chunk
    ZL_RET_R_IF_ERR(addIntValue(chunkBuilder, "chunkId", chunkId_));

    // 1. Make the streams map
    A1C_MAP_TRY_ADD_R(streamsPair, chunkBuilder);
    A1C_Item_string_refCStr(&streamsPair->key, "streams");
    A1C_ArrayBuilder streamsBuilder = A1C_Item_array_builder(
            &streamsPair->val, streamInfo_.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, streamsBuilder.array);
    for (auto& stream : streamInfo_) {
        A1C_ARRAY_TRY_ADD_R(a1c_stream, streamsBuilder);
        ZL_RET_R_IF_ERR(stream.second.serializeStream(a1c_arena, a1c_stream));
    }

    // 2. Make the codecs map + their in and out edges as part of metadata
    A1C_MAP_TRY_ADD_R(codecsPair, chunkBuilder);
    A1C_Item_string_refCStr(&codecsPair->key, "codecs");
    A1C_ArrayBuilder codecsBuilder = A1C_Item_array_builder(
            &codecsPair->val, codecInfo_.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, codecsBuilder.array);
    for (size_t codecNum = 0; codecNum < codecInfo_.size(); ++codecNum) {
        Codec& codec = codecInfo_[codecNum];
        A1C_ARRAY_TRY_ADD_R(a1c_codec, codecsBuilder);
        ZL_RET_R_IF_ERR(codec.serializeCodec(a1c_arena, a1c_codec, cctx));
    }

    // 3. graph info map
    A1C_MAP_TRY_ADD_R(graphsPair, chunkBuilder);
    A1C_Item_string_refCStr(&graphsPair->key, "graphs");
    A1C_ArrayBuilder graphsBuilder = A1C_Item_array_builder(
            &graphsPair->val, graphInfo_.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, graphsBuilder.array);
    for (auto& graph : graphInfo_) {
        A1C_ARRAY_TRY_ADD_R(a1c_graph, graphsBuilder);
        ZL_RET_R_IF_ERR(graph.serializeGraph(a1c_arena, a1c_graph, cctx));
    }
    return ZL_returnSuccess();
}

size_t ChunkTrace::getCompressedSize()
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

void ChunkTrace::printStreamMetadata()
{
    std::vector<size_t> cSize(streamInfo_.size(), SIZE_MAX);
    std::cout << "digraph stream_topo {" << std::endl;
    for (auto& s : streamInfo_) {
        const StreamID streamID     = s.first;
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

void ChunkTrace::printCodecMetadata()
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
            if (ZL_isError(graph.gFailure)) {
                std::cout << "\\nFailure: "
                          << "[PLACEHOLDER]";
                // << ZL_CCtx_getErrorContextString(
                //            cctx_, graph.gFailure);
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
                      << "[PLACEHOLDER]";
            // << ZL_CCtx_getErrorContextString(
            //            cctx_, metadata.cFailure);
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

size_t ChunkTrace::fillCSize(
        std::vector<size_t>& cSize,
        const StreamID streamID)
{
    size_t cSize_idx = streamID.sid;
    // already filled up
    if (cSize.size() != 0 && cSize[cSize_idx] != SIZE_MAX) {
        return cSize[cSize_idx];
    }

    const Stream& stream = streamInfo_.at(streamID);

    // base case: stream has no successors, and is input to the frame
    if (stream.successors.empty()) {
        cSize[cSize_idx] = stream.contentSize;
        return cSize[cSize_idx];
    }

    // Get the header size
    if (stream.consumerCodec.has_value()) {
        cSize[cSize_idx] = codecInfo_[stream.consumerCodec.value()].cHeaderSize;
    } else {
        cSize[cSize_idx] = 0;
    }

    // recursively fill cSize of this stream by summing the cSize of its
    // successor streams
    for (const auto& successor : stream.successors) {
        cSize[cSize_idx] += fillCSize(cSize, successor);
    }

    // if the consumer codec has multiple inputs, assume each input provides
    // equal contribution
    cSize[cSize_idx] /= codecInfo_[stream.consumerCodec.value()].inEdges.size();
    return cSize[cSize_idx];
}

void ChunkTrace::on_codecEncode_start(
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

void ChunkTrace::on_codecEncode_end(
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

        StreamPreview preview = getStreamPreviewData(
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

        codecInfo_[currCodecNum_].outEdges.push_back(streamID);
        streamdump(outStreams[i]);
    }

    // connect stream successors for cSize calculation
    for (const auto& inStreamID : codecInfo_[currCodecNum_].inEdges) {
        streamInfo_[inStreamID].successors = codecInfo_[currCodecNum_].outEdges;
    }

    ++currCodecNum_;
}

void ChunkTrace::on_ZL_Encoder_getScratchSpace(
        ZL_Encoder* /*ei*/,
        size_t /*size*/)
{
}

void ChunkTrace::on_ZL_Encoder_sendCodecHeader(
        ZL_Encoder* /*encoder*/,
        const void* /*trh*/,
        size_t trhSize)
{
    codecInfo_[currCodecNum_].cHeaderSize = trhSize;
}

void ChunkTrace::on_ZL_Encoder_createTypedStream(
        ZL_Encoder* /*encoder*/,
        int /*outStreamIndex*/,
        size_t /*eltsCapacity*/,
        size_t /*eltWidth*/,
        ZL_Output* /*createdStream*/)
{
}

void ChunkTrace::on_migraphEncode_start(
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
                             chunkId_,
                             std::move(inEdges) };
    graphInfo_.push_back(std::move(currGraph));
}

void ChunkTrace::on_migraphEncode_end(
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

void ChunkTrace::on_cctx_convertOneInput(
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

void ChunkTrace::on_segmenterEncode_start(ZL_Segmenter* segCtx)
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

void ChunkTrace::on_segmenterEncode_end(ZL_Segmenter* /*segCtx*/, ZL_Report r)
{
    if (ZL_isError(r)) {
        codecInfo_[currCodecNum_].cFailure = r;
    }
}

void ChunkTrace::on_ZL_Segmenter_processChunk_start(
        ZL_Segmenter* /*segCtx*/,
        const size_t /*numElts*/[],
        size_t /*numInputs*/,
        ZL_GraphID /*startingGraphID*/,
        const ZL_RuntimeGraphParameters* /*rGraphParams*/)
{
    initTrace();
}

void ChunkTrace::on_ZL_Segmenter_processChunk_end(
        ZL_Segmenter* /*segCtx*/,
        ZL_Report r)
{
    finalizeTrace(r);
}

void ChunkTrace::streamdump(const ZL_Output* createdStream)
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
ChunkTrace::getStreamdump()
{
    return std::move(streamdump_);
}
} // namespace openzl::visualizer
