// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "contrib/gpu/src/common/codec_decode_planning.hpp"
#include "openzl/common/assertion.h"
#include "openzl/cpp/poly/Span.hpp"

namespace openzl::gpu {

/**
 * Index into @c DecodePlan::streams.
 *
 * For slot @c N in a chunk, this index is the chunk's @c streams.begin plus
 * @c N.
 */
using PlannedStreamIndex = size_t;

/// Position of a codec run in @c DecodePlan::codecRuns.
using PlannedCodecRunIndex = size_t;

/// A contiguous range in one of the plan's arrays.
struct IndexRange {
    size_t begin;
    size_t count;

    /// Whether the range lies within an array of @p size elements. Avoids
    /// computing begin + count, which can overflow.
    bool fitsIn(size_t size) const
    {
        return begin <= size && count <= size - begin;
    }

    bool operator==(const IndexRange& other) const
    {
        return begin == other.begin && count == other.count;
    }
};

/**
 * Offset and size of a codec header within a chunk's transform headers.
 */
struct ByteRange {
    size_t offset;
    size_t size;

    bool operator==(const ByteRange& other) const
    {
        return offset == other.offset && size == other.size;
    }
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

    bool operator==(const ChunkNodeId& other) const
    {
        return chunkIndex == other.chunkIndex && nodeIndex == other.nodeIndex;
    }
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
 * at most one consumer codec run, so its producer and consumer codec runs
 * define how long its storage must remain valid.
 */
struct PlannedStream {
    /// Logical type and element layout available to consumers.
    StreamShape shape{};
    /// Required sizes of the stream's storage regions.
    StreamStorageSize storageSize{};
    /// Where the stream's bytes are stored.
    StorageClass storageClass = StorageClass::Unreached;
    /// Producing codec run. Empty for a frame-stored stream.
    std::optional<PlannedCodecRunIndex> producerCodecRun;
    /// Consuming codec run. Empty when no codec run consumes the stream.
    std::optional<PlannedCodecRunIndex> consumerCodecRun;
    /// Backing stream, set only for @c StreamRef.
    std::optional<PlannedStreamIndex> aliasStream;
    /// Byte offset in the backing stream.
    size_t aliasOffset = 0;

    bool operator==(const PlannedStream&) const = delete;
};

/**
 * Plan data for one decoder call.
 *
 * Inputs are in decoder argument order. Outputs are in regenerated-stream
 * order. Dependencies list each codec run that produced an input at most once.
 */
struct PlannedCodecRun {
    /// Chunk and node for this codec run.
    ChunkNodeId chunkNode;
    /// Transform used to select the decoder and read its metadata.
    PublicTransformInfo transform;
    /// Codec header within the chunk's transform headers.
    ByteRange codecHeader;
    /// Input indexes in @c DecodePlan::streamIndices.
    IndexRange inputs;
    /// Output indexes in @c DecodePlan::streamIndices.
    IndexRange outputs;
    /// Codec runs that produced the inputs, stored in
    /// @c DecodePlan::codecRunIndices.
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

    bool operator==(const PlannedChunk& other) const
    {
        return streams == other.streams && numOutputs == other.numOutputs;
    }
};

/**
 * Decode graph for a batch of prepared chunks.
 *
 * Records refer to each other by index. Input, output, and dependency lists
 * are stored in shared arrays. The plan does not point into caller storage.
 *
 * Streams are grouped by chunk and, within a chunk, indexed by wire slot.
 * Codec runs are grouped by chunk in chunk execution order.
 * A final @c Destination stream is written directly by its producer codec run;
 * a final @c Stored or @c StreamRef stream must be copied to the caller's
 * output.
 */
struct DecodePlan {
    /// Stream metadata, grouped by chunk and slot-indexed within each chunk.
    std::vector<PlannedStream> streams;
    /// Codec runs, grouped by chunk and in chunk execution order.
    std::vector<PlannedCodecRun> codecRuns;
    /// Input and output stream indexes used by @c PlannedCodecRun.
    std::vector<PlannedStreamIndex> streamIndices;
    /// Producer codec run indexes used by @c PlannedCodecRun.
    std::vector<PlannedCodecRunIndex> codecRunIndices;
    /// One entry per input chunk, in batch order.
    std::vector<PlannedChunk> chunks;

    /// Input streams of @p codecRun, in decoder argument order.
    poly::span<const PlannedStreamIndex> inputsOf(
            const PlannedCodecRun& codecRun) const
    {
        return slice(streamIndices, codecRun.inputs);
    }

    /// Output streams of @p codecRun, in regenerated-stream order.
    poly::span<const PlannedStreamIndex> outputsOf(
            const PlannedCodecRun& codecRun) const
    {
        return slice(streamIndices, codecRun.outputs);
    }

    /// Producer codec runs @p codecRun waits for, each appearing at most once.
    poly::span<const PlannedCodecRunIndex> dependenciesOf(
            const PlannedCodecRun& codecRun) const
    {
        return slice(codecRunIndices, codecRun.dependencies);
    }

    /// Streams of chunk @p chunkIndex, in chunk-local slot order.
    poly::span<const PlannedStream> streamsOf(size_t chunkIndex) const
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

    /**
     * Chunk that @p stream belongs to. @p stream must be below
     * @c streams.size().
     *
     * The chunk ranges start at stream 0, and each one starts where the
     * previous one ends, so the chunk is the last one whose range begins at
     * or before @p stream.
     */
    size_t chunkOf(PlannedStreamIndex stream) const
    {
        ZL_ASSERT_LT(stream, streams.size());
        const auto after = std::upper_bound(
                chunks.begin(),
                chunks.end(),
                stream,
                [](PlannedStreamIndex s, const PlannedChunk& chunk) {
                    return s < chunk.streams.begin;
                });
        ZL_ASSERT(after != chunks.begin());
        return static_cast<size_t>(after - chunks.begin()) - 1;
    }

   private:
    template <typename T>
    static poly::span<const T> slice(
            const std::vector<T>& values,
            IndexRange range)
    {
        return poly::span<const T>{ values }.subspan(range.begin, range.count);
    }
};

} // namespace openzl::gpu
