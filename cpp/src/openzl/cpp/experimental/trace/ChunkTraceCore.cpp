// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "openzl/cpp/experimental/trace/ChunkTraceCore.hpp"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/cpp/experimental/trace/CborHelpers.hpp"

namespace openzl::visualizer {

void ChunkTraceCore::createSourceForStream(
        const char* sourceCodecName,
        const StreamID& streamID,
        Stream& stream,
        std::vector<Codec>& codecInfo,
        size_t& currCodecNum,
        size_t chunkId)
{
    Codec source = {
        .name         = sourceCodecName,
        .cType        = true, // standard
        .cID          = 0,
        .cHeaderSize  = 0,
        .cLocalParams = {},
        .chunkId      = chunkId,
    };
    source.codecNum = currCodecNum;
    codecInfo.push_back(std::move(source));
    codecInfo[currCodecNum].outEdges.push_back(streamID);
    stream.producerCodec = currCodecNum;
    ++currCodecNum;
}

void ChunkTraceCore::createSinkForStream(
        const char* sinkCodecName,
        const StreamID& streamID,
        Stream& stream,
        std::vector<Codec>& codecInfo,
        size_t& currCodecNum,
        size_t chunkId)
{
    Codec sink = {
        .name         = sinkCodecName,
        .cType        = true, // standard
        .cID          = 0,
        .cHeaderSize  = 0,
        .cLocalParams = {},
        .chunkId      = chunkId,
    };
    sink.codecNum = currCodecNum;
    codecInfo.push_back(std::move(sink));
    codecInfo[currCodecNum].inEdges.push_back(streamID);
    stream.consumerCodec = currCodecNum;
    ++currCodecNum;
}

void ChunkTraceCore::finalizeUnsourcedStreams(
        const char* sourceCodecName,
        std::map<ZL_DataID, Stream, ZL_DataIDCustomComparator>& streamInfo,
        std::vector<Codec>& codecInfo,
        size_t& currCodecNum,
        size_t chunkId)
{
    for (auto& [streamID, stream] : streamInfo) {
        if (!stream.producerCodec.has_value()) {
            createSourceForStream(
                    sourceCodecName,
                    streamID,
                    stream,
                    codecInfo,
                    currCodecNum,
                    chunkId);
        }
    }
}

void ChunkTraceCore::finalizeUnconsumedStreams(
        const char* terminalCodecName,
        std::map<ZL_DataID, Stream, ZL_DataIDCustomComparator>& streamInfo,
        std::vector<Codec>& codecInfo,
        size_t& currCodecNum,
        size_t chunkId)
{
    for (auto& [streamID, stream] : streamInfo) {
        if (!stream.consumerCodec.has_value()) {
            createSinkForStream(
                    terminalCodecName,
                    streamID,
                    stream,
                    codecInfo,
                    currCodecNum,
                    chunkId);
        }
    }
}

ZL_Report ChunkTraceCore::serializeChunkDataToCBOR(
        A1C_Arena* a1c_arena,
        A1C_ArrayBuilder* chunkArrayBuilder,
        size_t chunkId,
        std::map<ZL_DataID, Stream, ZL_DataIDCustomComparator>& streamInfo,
        std::vector<Codec>& codecInfo,
        std::vector<Graph>& graphInfo,
        const ZL_CCtx* cctx)
{
    A1C_ARRAY_TRY_ADD_R(chunkItem, *chunkArrayBuilder);
    A1C_MapBuilder chunkBuilder = A1C_Item_map_builder(chunkItem, 4, a1c_arena);
    ZL_RET_R_IF_NULL(allocation, chunkBuilder.map);

    ZL_RET_R_IF_ERR(addIntValue(chunkBuilder, "chunkId", chunkId));

    // Streams
    A1C_MAP_TRY_ADD_R(streamsPair, chunkBuilder);
    A1C_Item_string_refCStr(&streamsPair->key, "streams");
    A1C_ArrayBuilder streamsBuilder = A1C_Item_array_builder(
            &streamsPair->val, streamInfo.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, streamsBuilder.array);
    for (auto& stream : streamInfo) {
        A1C_ARRAY_TRY_ADD_R(a1c_stream, streamsBuilder);
        ZL_RET_R_IF_ERR(stream.second.serializeStream(a1c_arena, a1c_stream));
    }

    // Codecs
    A1C_MAP_TRY_ADD_R(codecsPair, chunkBuilder);
    A1C_Item_string_refCStr(&codecsPair->key, "codecs");
    A1C_ArrayBuilder codecsBuilder = A1C_Item_array_builder(
            &codecsPair->val, codecInfo.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, codecsBuilder.array);
    for (size_t codecNum = 0; codecNum < codecInfo.size(); ++codecNum) {
        Codec& codec = codecInfo[codecNum];
        A1C_ARRAY_TRY_ADD_R(a1c_codec, codecsBuilder);
        ZL_RET_R_IF_ERR(codec.serializeCodec(a1c_arena, a1c_codec, cctx));
    }

    // Graphs (empty array for decompress)
    A1C_MAP_TRY_ADD_R(graphsPair, chunkBuilder);
    A1C_Item_string_refCStr(&graphsPair->key, "graphs");
    A1C_ArrayBuilder graphsBuilder = A1C_Item_array_builder(
            &graphsPair->val, graphInfo.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, graphsBuilder.array);
    for (auto& graph : graphInfo) {
        A1C_ARRAY_TRY_ADD_R(a1c_graph, graphsBuilder);
        ZL_RET_R_IF_ERR(graph.serializeGraph(a1c_arena, a1c_graph, cctx));
    }

    return ZL_returnSuccess();
}

ZL_Report ChunkTraceCore::writeSerializedStreamdump(
        std::vector<uint8_t>& buffer,
        std::string& outTrace)
{
    outTrace = std::string(buffer.begin(), buffer.end());
    return ZL_returnSuccess();
}

} // namespace openzl::visualizer
