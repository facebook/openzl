// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "contrib/gpu/src/common/codec_decode_planning.hpp"

namespace openzl::gpu {

/**
 * Index into @c DecodePlan::streams.
 *
 * For slot @c N in a chunk, this index is the chunk's @c streams.begin plus
 * @c N.
 */
using PlannedStreamIndex = size_t;

/// Position of a codec run in @c DecodePlan::codecRuns.
using PlannedRunIndex = size_t;

/// Absence of a stream reference.
inline constexpr PlannedStreamIndex kNoStream = SIZE_MAX;

/// Absence of a codec run reference.
inline constexpr PlannedRunIndex kNoRun = SIZE_MAX;

/// A contiguous range in one of the plan's arrays.
struct IndexRange {
    size_t begin;
    size_t count;

    bool operator==(const IndexRange&) const = default;
};

/**
 * Offset and size of a codec header within a chunk's transform headers.
 */
struct ByteRange {
    size_t offset;
    size_t size;

    bool operator==(const ByteRange&) const = default;
};

/**
 * Identifies a decoder node by its chunk and node indexes.
 *
 * A node index is unique only within its chunk.
 */
struct ChunkNodeId {
    /// Zero-based position of the prepared chunk in the input batch.
    size_t chunkIndex;
    /// Chunk-local node index in the chunk's decoder execution order.
    ZL_IDType nodeIndex;

    bool operator==(const ChunkNodeId&) const = default;
};

/// Where a stream's bytes are stored.
enum class StorageClass : uint32_t {
    /// The planner has not recorded this stream yet.
    Unreached,
    /// Bytes stored in the compressed frame.
    Stored,
    /// Bytes written directly to the caller's output buffer.
    Destination,
    /// Bytes written to the decode stream arena.
    StreamArena,
    /// Bytes that refer to part of another stream.
    StreamRef,
};

/**
 * Plan data for one stream slot.
 *
 * The plan has one record for every slot declared by each chunk. A stream has
 * at most one consumer, so its producer and consumer define how long its
 * storage must remain valid.
 */
struct PlannedStream {
    /// Logical type and element layout available to consumers.
    StreamShape shape{};
    /// Required sizes of the stream's storage regions.
    StreamStorageSize storageSize{};
    /// Where the stream's bytes are stored.
    StorageClass storageClass = StorageClass::Unreached;
    /// Producing codec run, or @c kNoRun for a stream stored in the frame.
    PlannedRunIndex producer = kNoRun;
    /// Consuming codec run, or @c kNoRun when no run consumes this stream.
    PlannedRunIndex consumer = kNoRun;
    /// Backing stream, set only for @c StreamRef.
    PlannedStreamIndex aliasStream = kNoStream;
    /// Byte offset in the backing stream.
    size_t aliasOffset = 0;

    bool operator==(const PlannedStream&) const = delete;
};

/**
 * Plan data for one decoder call.
 *
 * Inputs are in decoder argument order. Outputs are in regenerated-stream
 * order. Dependencies list each run that produced an input at most once.
 */
struct PlannedCodecRun {
    /// Chunk and node for this run.
    ChunkNodeId chunkNode;
    /// Transform used to select the decoder and read its metadata.
    PublicTransformInfo transform;
    /// Codec header within the chunk's transform headers.
    ByteRange codecHeader;
    /// Input indexes in @c DecodePlan::streamIndices.
    IndexRange inputs;
    /// Output indexes in @c DecodePlan::streamIndices.
    IndexRange outputs;
    /// Runs that produced the inputs, stored in @c DecodePlan::runIndices.
    IndexRange dependencies;

    bool operator==(const PlannedCodecRun&) const = delete;
};

/**
 * The streams and output count for one prepared chunk.
 *
 * @c streams has one entry for every slot in the chunk. The outputs are the
 * last @c numOutputs slots in reverse order: output @c 0 is the last slot,
 * output @c 1 is the slot before it, and so on.
 */
struct PlannedChunk {
    /// Range of @c DecodePlan::streams holding this chunk's slots.
    IndexRange streams;
    /// Number of frame outputs this chunk contributes.
    size_t numOutputs;

    bool operator==(const PlannedChunk&) const = default;
};

/**
 * Decode graph for a batch of prepared chunks.
 *
 * Records refer to each other by index. Input, output, and dependency lists
 * are stored in shared arrays. The plan does not point into caller storage.
 *
 * Streams are grouped by chunk and, within a chunk, indexed by wire slot.
 * Codec runs are grouped by chunk in chunk execution order.
 * A final @c Destination stream is written directly by its producer; a final
 * @c Stored or @c StreamRef stream must be copied to the caller's output.
 */
struct DecodePlan {
    /// Stream metadata, grouped by chunk and slot-indexed within each chunk.
    std::vector<PlannedStream> streams;
    /// Codec runs, grouped by chunk and in chunk execution order.
    std::vector<PlannedCodecRun> codecRuns;
    /// Input and output stream indexes used by @c PlannedCodecRun.
    std::vector<PlannedStreamIndex> streamIndices;
    /// Producer run indexes used by @c PlannedCodecRun.
    std::vector<PlannedRunIndex> runIndices;
    /// One entry per input chunk, in batch order.
    std::vector<PlannedChunk> chunks;

    /// Input streams of @p run, in decoder argument order.
    std::span<const PlannedStreamIndex> inputsOf(
            const PlannedCodecRun& run) const
    {
        return slice(streamIndices, run.inputs);
    }

    /// Output streams of @p run, in regenerated-stream order.
    std::span<const PlannedStreamIndex> outputsOf(
            const PlannedCodecRun& run) const
    {
        return slice(streamIndices, run.outputs);
    }

    /// Producer runs @p run must wait for, each appearing at most once.
    std::span<const PlannedRunIndex> dependenciesOf(
            const PlannedCodecRun& run) const
    {
        return slice(runIndices, run.dependencies);
    }

    /// Streams of chunk @p chunkIndex, in chunk-local slot order.
    std::span<const PlannedStream> streamsOf(size_t chunkIndex) const
    {
        return slice(streams, chunks.at(chunkIndex).streams);
    }

    /// Number of frame outputs chunk @p chunkIndex contributes.
    size_t numFinalOutputsOf(size_t chunkIndex) const
    {
        return chunks.at(chunkIndex).numOutputs;
    }

    /**
     * Stream carrying output @p outputIndex of chunk @p chunkIndex.
     *
     * If the stream is not a @c Destination, the executor must copy its bytes
     * to the caller's output.
     *
     * @p outputIndex must be below @c numFinalOutputsOf; a larger one names a
     * stream belonging to an earlier chunk.
     */
    PlannedStreamIndex finalOutputOf(size_t chunkIndex, size_t outputIndex)
            const
    {
        const IndexRange range = chunks.at(chunkIndex).streams;
        return range.begin + range.count - 1 - outputIndex;
    }

    /// Chunk-local wire slot of @p stream, which must belong to @p chunkIndex.
    ZL_IDType slotOf(size_t chunkIndex, PlannedStreamIndex stream) const
    {
        return static_cast<ZL_IDType>(
                stream - chunks.at(chunkIndex).streams.begin);
    }

   private:
    template <typename T>
    static std::span<const T> slice(
            const std::vector<T>& values,
            IndexRange range)
    {
        return std::span{ values }.subspan(range.begin, range.count);
    }
};

} // namespace openzl::gpu
