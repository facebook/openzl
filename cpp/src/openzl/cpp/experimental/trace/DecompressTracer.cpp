// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "openzl/cpp/experimental/trace/DecompressTracer.hpp"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/logging.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/experimental/trace/CborHelpers.hpp"
#include "openzl/zl_errors.h"

#include <utility>
#include <vector>

namespace openzl::visualizer {

static constexpr size_t MAIN_CHUNK_IDX = 0;

TraceResult DecompressTracer::extractTrace()
{
    // Aggregate streamdump from all chunks
    trace_.streamdump.reserve(chunks_.size());
    for (auto& chunk : chunks_) {
        auto streamdump                  = chunk.getStreamdump();
        std::vector<StreamdumpEntry> tmp = {};
        tmp.reserve(streamdump.size());
        for (auto& [k, v] : streamdump) {
            tmp.push_back(
                    StreamdumpEntry{
                            k, std::move(v.first), std::move(v.second) });
        }
        trace_.streamdump.push_back(std::move(tmp));
    }
    return std::move(trace_);
}

void DecompressTracer::on_ZL_DCtx_decompressMultiTBuffer_start(
        ZL_DCtx* /* dctx */,
        size_t /* nbOutputs */,
        const void* framePtr,
        size_t frameSize)
{
    frameVersion_ = static_cast<uint32_t>(
            ZL_validResult(ZL_getFormatVersionFromFrame(framePtr, frameSize)));

    // Create the main chunk at index 0
    chunks_.emplace_back(MAIN_CHUNK_IDX);
    currChunk_ = &chunks_[MAIN_CHUNK_IDX];
}

void DecompressTracer::on_ZL_DCtx_decompressMultiTBuffer_end(
        ZL_DCtx* /* dctx */,
        ZL_Report /* result */)
{
    // Create a dummy "main" chunk, if necessary
    if (chunks_.size() > 1) {
        auto dummyChunk =
                DecompressChunkTrace::makeSegmenterChunk(chunks_.size());
        std::vector<DecompressChunkTrace> newChunks = { std::move(dummyChunk) };
        for (auto& chunk : chunks_) {
            newChunks.push_back(std::move(chunk));
        }
        this->chunks_ = std::move(newChunks);
    }

    // Serialize the trace to CBOR
    Arena* arena        = ALLOC_HeapArena_create();
    A1C_Arena a1c_arena = A1C_Arena_wrap(arena);
    std::vector<uint8_t> buffer;
    auto res = serializeStreamdumpToCbor(&a1c_arena, buffer);
    if (ZL_isError(res)) {
        ZL_LOG(ERROR, "Failed to serialize decompression trace!");
        ALLOC_Arena_freeArena(arena);
        throw std::runtime_error("Failed to serialize decompression trace.");
    }
    ALLOC_Arena_freeArena(arena);
    if (ZL_isError(
                ChunkTraceCore::writeSerializedStreamdump(
                        buffer, trace_.trace))) {
        ZL_LOG(ERROR, "Failed to write serialized decompression trace!");
        throw std::runtime_error(
                "Failed to write serialized decompression trace.");
    }
}

void DecompressTracer::on_decompressChunk_start(
        ZL_DCtx* /* dctx */,
        size_t chunkIndex)
{
    if (chunkIndex > 0) {
        // Multi-chunk frame: create additional chunk traces
        chunks_.emplace_back(chunkIndex);
        currChunk_ = &chunks_.back();
    }
}

void DecompressTracer::on_decompressChunk_end(
        ZL_DCtx* /* dctx */,
        ZL_Report result)
{
    currChunk_->finalizeTrace(result);
    // The main chunk is finalized in decompressMultiTBuffer_end
}

void DecompressTracer::on_ZL_Decoder_getCodecHeader(
        const ZL_Decoder* dictx,
        const void* trh,
        size_t trhSize)
{
    currChunk_->on_ZL_Decoder_getCodecHeader(dictx, trh, trhSize);
}

void DecompressTracer::on_codecDecode_start(
        ZL_Decoder* dictx,
        const ZL_Data* const* inStreams,
        size_t nbInStreams)
{
    currChunk_->on_codecDecode_start(dictx, inStreams, nbInStreams);
}

void DecompressTracer::on_codecDecode_end(
        ZL_Decoder* dictx,
        const ZL_Data* const* outStreams,
        size_t nbOutStreams,
        ZL_Report result)
{
    currChunk_->on_codecDecode_end(dictx, outStreams, nbOutStreams, result);
}

ZL_Report DecompressTracer::serializeStreamdumpToCbor(
        A1C_Arena* a1c_arena,
        std::vector<uint8_t>& buffer)
{
    A1C_Item* root = A1C_Item_root(a1c_arena);
    ZL_RET_R_IF_NULL(allocation, root);

    // 5 top-level fields: libraryVersion, frameVersion, traceVersion,
    // operationType, chunks
    A1C_MapBuilder rootBuilder = A1C_Item_map_builder(root, 5, a1c_arena);
    ZL_RET_R_IF_NULL(allocation, rootBuilder.map);

    ZL_RET_R_IF_ERR(addIntValue(rootBuilder, "libraryVersion", libraryVersion));
    ZL_RET_R_IF_ERR(addIntValue(rootBuilder, "frameVersion", frameVersion_));
    ZL_RET_R_IF_ERR(addIntValue(rootBuilder, "traceVersion", traceVersion));
    ZL_RET_R_IF_ERR(addIntValue(rootBuilder, "operationType", 1));

    // Wrap chunks in a "chunks" array
    A1C_MAP_TRY_ADD_R(chunksPair, rootBuilder);
    A1C_Item_string_refCStr(&chunksPair->key, "chunks");
    A1C_ArrayBuilder chunksBuilder =
            A1C_Item_array_builder(&chunksPair->val, chunks_.size(), a1c_arena);
    ZL_RET_R_IF_NULL(allocation, chunksBuilder.array);

    for (auto& chunk : chunks_) {
        ZL_RET_R_IF_ERR(chunk.serializeToCBOR(a1c_arena, &chunksBuilder));
    }

    // Encode to buffer
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

} // namespace openzl::visualizer
