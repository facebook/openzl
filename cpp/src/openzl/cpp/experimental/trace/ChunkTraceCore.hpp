// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <map>
#include <vector>

#include "openzl/cpp/experimental/trace/Codec.hpp"
#include "openzl/cpp/experimental/trace/Graph.hpp"
#include "openzl/cpp/experimental/trace/StreamVisualizer.hpp"

namespace openzl::visualizer {

struct StreamdumpEntry {
    size_t streamId;
    std::string content;
    std::string strLens;
};

struct TraceResult {
    std::string trace;
    std::vector<std::vector<StreamdumpEntry>> streamdump;
};

/**
 * Static-only helper class for shared ChunkTrace logic between
 * CompressChunkTrace and DecompressChunkTrace.
 * Owns no data — all state is passed in by the caller.
 */
class ChunkTraceCore {
   public:
    ChunkTraceCore() = delete; // non-instantiable

    /**
     * Creates a source codec with the given name for a single stream.
     * Wires the stream into the source codec's outEdges, sets producerCodec,
     * and increments currCodecNum.
     */
    static void createSourceForStream(
            const char* sourceCodecName,
            const StreamID& streamID,
            Stream& stream,
            std::vector<Codec>& codecInfo,
            size_t& currCodecNum,
            size_t chunkId);

    /**
     * Creates a sink codec with the given name for a single stream.
     * Wires the stream into the sink codec's inEdges, sets consumerCodec,
     * and increments currCodecNum.
     */
    static void createSinkForStream(
            const char* sinkCodecName,
            const StreamID& streamID,
            Stream& stream,
            std::vector<Codec>& codecInfo,
            size_t& currCodecNum,
            size_t chunkId);

    /**
     * For each unsourced stream (no producerCodec), creates a source codec
     * with the given name via createSourceForStream.
     */
    static void finalizeUnsourcedStreams(
            const char* sourceCodecName,
            std::map<ZL_DataID, Stream, ZL_DataIDCustomComparator>& streamInfo,
            std::vector<Codec>& codecInfo,
            size_t& currCodecNum,
            size_t chunkId);

    /**
     * For each unconsumed stream (no consumerCodec), creates a terminal codec
     * with the given name via createSinkForStream.
     */
    static void finalizeUnconsumedStreams(
            const char* terminalCodecName,
            std::map<ZL_DataID, Stream, ZL_DataIDCustomComparator>& streamInfo,
            std::vector<Codec>& codecInfo,
            size_t& currCodecNum,
            size_t chunkId);

    /**
     * Builds a StreamPreview from raw stream data, dispatching by ZL_Type.
     * stringLens is only used for ZL_Type_string streams.
     */
    static StreamPreview getStreamPreview(
            const void* data,
            ZL_Type type,
            size_t eltWidth,
            size_t numElts,
            const uint32_t* stringLens = nullptr);

    /**
     * Recursively computes the compressed size (cSize) of a stream by
     * summing the cSize of its successors. Uses Stream::cSize directly
     * for memoization (0 means not yet computed).
     */
    static size_t fillCSize(
            const StreamID& streamID,
            std::map<ZL_DataID, Stream, ZL_DataIDCustomComparator>& streamInfo,
            const std::vector<Codec>& codecInfo,
            size_t totalSize);

    /**
     * Serializes chunkId, streams, codecs, and optionally graphs into
     * a CBOR chunk item appended to chunkArrayBuilder.
     * graphInfo may be empty (decompress side has no graphs).
     */
    static ZL_Report serializeChunkDataToCBOR(
            A1C_Arena* a1c_arena,
            A1C_ArrayBuilder* chunkArrayBuilder,
            size_t chunkId,
            std::map<ZL_DataID, Stream, ZL_DataIDCustomComparator>& streamInfo,
            std::vector<Codec>& codecInfo,
            std::vector<Graph>& graphInfo,
            ZL_OperationContext* opCtx);

    /**
     * Encodes CBOR buffer bytes into a string (for trace output).
     */
    static ZL_Report writeSerializedStreamdump(
            std::vector<uint8_t>& buffer,
            std::string& outTrace);
};

} // namespace openzl::visualizer
