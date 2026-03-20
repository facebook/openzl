// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <map>
#include <string>
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
     * Creates a "zl.#start" placeholder codec and appends it to codecInfo.
     * Increments currCodecNum.
     */
    static void initTrace(
            std::vector<Codec>& codecInfo,
            size_t& currCodecNum,
            size_t chunkId);

    /**
     * For each unconsumed stream (no consumerCodec), creates a terminal codec
     * with the given name, wires it up, and increments currCodecNum.
     */
    static void finalizeUnconsumedStreams(
            const char* terminalCodecName,
            std::map<ZL_DataID, Stream, ZL_DataIDCustomComparator>& streamInfo,
            std::vector<Codec>& codecInfo,
            size_t& currCodecNum,
            size_t chunkId);

    /**
     * Serializes chunkId, streams, codecs, and optionally graphs into
     * a CBOR chunk item appended to chunkArrayBuilder.
     * cctx may be nullptr (decompress side).
     * graphInfo may be empty (decompress side has no graphs).
     */
    static ZL_Report serializeChunkDataToCBOR(
            A1C_Arena* a1c_arena,
            A1C_ArrayBuilder* chunkArrayBuilder,
            size_t chunkId,
            std::map<ZL_DataID, Stream, ZL_DataIDCustomComparator>& streamInfo,
            std::vector<Codec>& codecInfo,
            std::vector<Graph>& graphInfo,
            const ZL_CCtx* cctx);

    /**
     * Encodes CBOR buffer bytes into a string (for trace output).
     */
    static ZL_Report writeSerializedStreamdump(
            std::vector<uint8_t>& buffer,
            std::string& outTrace);
};

} // namespace openzl::visualizer
