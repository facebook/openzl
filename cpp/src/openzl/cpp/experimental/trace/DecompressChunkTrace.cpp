// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "openzl/cpp/experimental/trace/DecompressChunkTrace.hpp"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/experimental/trace/CborHelpers.hpp"
#include "openzl/decompress/dictx.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

#include <variant>

namespace openzl::visualizer {

namespace {
std::variant<
        std::vector<std::string>,
        std::vector<int64_t>,
        std::vector<uint8_t>>
emptyPreview(ZL_Type type)
{
    switch (type) {
        case ZL_Type_string:
            return std::vector<std::string>{};
        case ZL_Type_numeric:
            return std::vector<int64_t>{};
        case ZL_Type_struct:
        case ZL_Type_serial:
        default:
            return std::vector<uint8_t>{};
    }
}
} // namespace

void DecompressChunkTrace::initTrace()
{
    ChunkTraceCore::initTrace(codecInfo_, currCodecNum_, chunkId_);
}

void DecompressChunkTrace::finalizeTrace(ZL_Report result)
{
    if (ZL_isError(result)) {
        ChunkTraceCore::finalizeUnconsumedStreams(
                "zl.#in_progress",
                streamInfo_,
                codecInfo_,
                currCodecNum_,
                chunkId_);
    } else {
        ChunkTraceCore::finalizeUnconsumedStreams(
                "zl.regen", streamInfo_, codecInfo_, currCodecNum_, chunkId_);
    }
}

ZL_Report DecompressChunkTrace::serializeToCBOR(
        A1C_Arena* a1c_arena,
        A1C_ArrayBuilder* chunkArrayBuilder)
{
    std::vector<Graph> noGraphs;
    return ChunkTraceCore::serializeChunkDataToCBOR(
            a1c_arena,
            chunkArrayBuilder,
            chunkId_,
            streamInfo_,
            codecInfo_,
            noGraphs,
            nullptr);
}

void DecompressChunkTrace::on_codecDecode_start(
        ZL_Decoder* dictx,
        const ZL_Data* const* inStreams,
        size_t nbInStreams)
{
    // Extract transform info from ZL_Decoder
    const char* transformName = DT_getTransformName(dictx->dt);

    Codec newCodec{
        .name         = transformName ? transformName : "",
        .cType        = true,
        .cID          = dictx->dt->miGraphDesc.CTid,
        .cHeaderSize  = 0,
        .cLocalParams = {},
        .chunkId      = chunkId_,
    };
    newCodec.codecNum = currCodecNum_;
    codecInfo_.push_back(std::move(newCodec));

    for (size_t i = 0; i < nbInStreams; ++i) {
        StreamID streamID = ZL_Data_id(inStreams[i]);
        // Lazy discovery: if stream not yet seen, it's a stored/initial stream
        if (streamInfo_.find(streamID) == streamInfo_.end()) {
            ZL_Type type          = ZL_Data_type(inStreams[i]);
            streamInfo_[streamID] = Stream{
                .id            = streamID,
                .type          = type,
                .outputIdx     = i,
                .eltWidth      = ZL_Data_eltWidth(inStreams[i]),
                .numElts       = ZL_Data_numElts(inStreams[i]),
                .contentSize   = ZL_Data_contentSize(inStreams[i]),
                .chunkId       = chunkId_,
                .streamPreview = emptyPreview(type),
            };
            // Link as output of start node (codec 0)
            codecInfo_[0].outEdges.push_back(streamID);
        }
        codecInfo_[currCodecNum_].inEdges.push_back(streamID);
        streamInfo_[streamID].consumerCodec = currCodecNum_;
    }
}

void DecompressChunkTrace::on_codecDecode_end(
        ZL_Decoder* /* dictx */,
        const ZL_Data* const* outStreams,
        size_t nbOutStreams,
        ZL_Report result)
{
    if (ZL_isError(result)) {
        codecInfo_[currCodecNum_].cFailure = result;
    }

    // Record output streams with full metadata from ZL_Data objects
    for (size_t i = 0; i < nbOutStreams; ++i) {
        StreamID streamID = ZL_Data_id(outStreams[i]);
        if (streamInfo_.find(streamID) == streamInfo_.end()) {
            ZL_Type type          = ZL_Data_type(outStreams[i]);
            streamInfo_[streamID] = Stream{
                .id            = streamID,
                .type          = type,
                .outputIdx     = i,
                .eltWidth      = ZL_Data_eltWidth(outStreams[i]),
                .numElts       = ZL_Data_numElts(outStreams[i]),
                .contentSize   = ZL_Data_contentSize(outStreams[i]),
                .chunkId       = chunkId_,
                .streamPreview = emptyPreview(type),
            };
        }
        codecInfo_[currCodecNum_].outEdges.push_back(streamID);
    }

    // Capture streamdump for each output stream
    for (size_t i = 0; i < nbOutStreams; ++i) {
        streamdump(outStreams[i]);
    }

    // Connect stream successors for cSize calculation
    for (const auto& inStreamID : codecInfo_[currCodecNum_].inEdges) {
        streamInfo_[inStreamID].successors = codecInfo_[currCodecNum_].outEdges;
    }

    ++currCodecNum_;
}

void DecompressChunkTrace::streamdump(const ZL_Data* data)
{
    auto content = std::string(
            (const char*)ZL_Data_rPtr(data), ZL_Data_contentSize(data));
    std::string strLens = "";
    if (ZL_Data_type(data) == ZL_Type_string) {
        auto ptr = ZL_Data_rStringLens(data);
        strLens  = std::string(
                (const char*)ptr, ZL_Data_numElts(data) * sizeof(ptr[0]));
    }
    streamdump_[ZL_Data_id(data).sid] = { content, strLens };
}

std::map<size_t, std::pair<std::string, std::string>>&&
DecompressChunkTrace::getStreamdump()
{
    return std::move(streamdump_);
}

void DecompressChunkTrace::on_ZL_Decoder_getCodecHeader(
        const ZL_Decoder* /* dictx */,
        const void* /* trh */,
        size_t trhSize)
{
    // The header read happens between codecDecode_start and codecDecode_end,
    // so currCodecNum_ points to the current codec being decoded.
    codecInfo_[currCodecNum_].cHeaderSize = trhSize;
}

} // namespace openzl::visualizer
