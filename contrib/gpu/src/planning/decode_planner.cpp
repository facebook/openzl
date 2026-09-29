// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/planning/decode_planner.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <utility>
#include <vector>

#include "contrib/gpu/src/planning/codec_decode_registry.hpp"
#include "openzl/common/wire_format.h"
#include "openzl/decompress/dctx2.h"
#include "openzl/shared/overflow.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"

namespace openzl::gpu {
namespace {

/**
 * Adds one prepared chunk to a batch-wide decode plan.
 *
 * The planner turns frame and chunk metadata into a graph that a GPU executor
 * can use without reading stream payloads or allocating device buffers. It
 * validates the frame's stream graph, asks each codec planner for its output
 * shape and storage size, and records the order and storage relationships
 * needed to run the decoders later. @c planDecode builds into a temporary plan,
 * so an error leaves the caller's plan unchanged.
 *
 * Terminology used here:
 * - A node is a decoder call described by the frame.
 * - A run is the plan's record of one node's decoder call. It identifies the
 *   codec, its inputs and outputs, and the runs that must finish first.
 * - A stream is data stored in the frame or produced by a node.
 * - A slot is a stream's position within one chunk. Slot numbering starts at
 *   zero for each chunk, so a slot is not a batch-wide index. Every slot has
 *   one entry in @c DecodePlan::streams.
 * - A producer is the run that writes a stream. Stored streams have no
 *   producing run.
 * - A consumer is the one run that reads a stream. Final outputs have no
 *   consumer.
 * - A dependency is a run that must finish because it produced one of another
 *   run's inputs.
 *
 * Streams keep two different kinds of size information. @c StreamShape
 * describes the logical data: its type, element count, and element width.
 * @c StreamStorageSize describes the required byte storage: the data bytes and,
 * for strings, the separate string-length table.
 *
 *
 * The plan uses flat arrays. A chunk's @c streams range selects its entries in
 * @c DecodePlan::streams. A run's @c inputs and @c outputs ranges select stream
 * indexes in @c DecodePlan::streamIndices. Its @c dependencies range selects
 * producer run indexes in @c DecodePlan::runIndices.
 *
 * A chunk's final outputs are its last slots in reverse order: output zero is
 * the last slot, output one is the slot before it, and so on. An arena-backed
 * final output becomes @c StorageClass::Destination so its decoder writes
 * directly to the caller's output. A @c Stored or @c StreamRef final output
 * keeps its storage class and tells the executor which bytes it must copy.
 *
 * This planner describes the work. It does not run decoders, allocate their
 * buffers, or copy final outputs. Stored bytes remain owned by the prepared
 * decode context, and codec headers remain owned by the staged host buffer.
 */
class ChunkPlanner {
   public:
    /**
     * Creates a planner whose chunk, context, and destination plan must outlive
     * this object.
     */
    ChunkPlanner(
            size_t chunkIndex,
            const PreparedGpuChunkView& chunk,
            DecodePlan& plan)
            : chunkIndex_(chunkIndex),
              chunk_(chunk),
              dctx_(*chunk.dctx),
              header_(DCtx_getFrameHeader(&dctx_)),
              plan_(plan),
              numStreams_(ZL_DCtx_getNumStreams(&dctx_))
    {
    }

    /** Plans every node in a chunk and records the chunk's final outputs. */
    ZL_Report planChunk()
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        if (header_ == nullptr) {
            ZL_ERR(logicError);
        }
        const DFH_Struct& header = *header_;
        size_t numOutputs        = 0;

        // Step 1: Validate the prepared chunk metadata and output range.
        ZL_ERR_IF_ERR(validateHeader(header, numOutputs));

        // Step 2: Reserve one stream record for every slot.
        ZL_ERR_IF_ERR(reserveChunkSlots());

        // Step 3: Plan every node in chunk execution order.
        for (size_t nodeIndex = 0; nodeIndex < VECTOR_SIZE(header.nodes);
             ++nodeIndex) {
            ZL_ERR_IF_ERR(
                    planNodeInOrder(header, static_cast<ZL_IDType>(nodeIndex)));
        }

        // Step 4: Record stored streams not seen as node inputs.
        for (size_t streamSlot = 0; streamSlot < numStreams_; ++streamSlot) {
            ZL_ERR_IF_ERR(
                    recordStreamIfNeeded(static_cast<ZL_IDType>(streamSlot)));
        }

        // Step 5: Mark final outputs as destinations or copy sources.
        ZL_ERR_IF_ERR(classifyFinalOutputs(numOutputs));

        plan_.chunks.push_back(
                {
                        .streams    = { .begin = chunkBase_,
                                        .count = numStreams_ },
                        .numOutputs = numOutputs,
                });
        return ZL_returnSuccess();
    }

   private:
    /**
     * Reserves this chunk's slot range at the end of @c plan_.streams.
     *
     * Each record starts as @c Unreached and is filled in place. This keeps a
     * stream at the index for its slot regardless of planning order.
     */
    ZL_Report reserveChunkSlots()
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        size_t end;
        ZL_ERR_IF(
                ZL_overflowAddST(plan_.streams.size(), numStreams_, &end),
                integerOverflow);
        // The largest index is reserved so kNoStream stays distinct.
        ZL_ERR_IF_GE(
                end,
                static_cast<size_t>(kNoStream),
                temporaryLibraryLimitation);
        chunkBase_ = plan_.streams.size();
        plan_.streams.resize(end);
        return ZL_returnSuccess();
    }

    /// Dense index of chunk-local slot @p streamSlot, which must be in range.
    PlannedStreamIndex streamIndexOf(ZL_IDType streamSlot) const
    {
        return chunkBase_ + streamSlot;
    }

    /** Writes one planned stream into its chunk-local slot. */
    ZL_Report writeStream(ZL_IDType streamSlot, const PlannedStream& stream)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        ZL_ERR_IF_GE(streamSlot, numStreams_, corruption);
        plan_.streams.at(streamIndexOf(streamSlot)) = stream;
        return ZL_returnSuccess();
    }

    /**
     * Validates the prepared chunk counts and final-output range.
     *
     * Reads node and output metadata from @p header and the prepared stream
     * count from @c numStreams_. Writes the validated output count to
     * @p numOutputs.
     */
    ZL_Report validateHeader(const DFH_Struct& header, size_t& numOutputs) const
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        ZL_ERR_IF_GT(
                numStreams_,
                static_cast<size_t>(std::numeric_limits<ZL_IDType>::max()) + 1,
                temporaryLibraryLimitation);
        ZL_ERR_IF_GT(
                VECTOR_SIZE(header.nodes),
                static_cast<size_t>(std::numeric_limits<ZL_IDType>::max()) + 1,
                temporaryLibraryLimitation);
        ZL_TRY_LET_CONST(
                size_t,
                outputCount,
                ZL_FrameInfo_getNumOutputs(header.frameinfo));
        ZL_ERR_IF_GT(outputCount, numStreams_, corruption);
        numOutputs = outputCount;
        return ZL_returnSuccess();
    }

    /**
     * Classifies final outputs as direct destinations or copy sources.
     *
     * The frame's outputs are the last @p numOutputs slots, in reverse. A
     * @c StreamArena output can be written directly to the caller's buffer. A
     * @c Stored or @c StreamRef output keeps its class so the executor knows to
     * copy it after decoding.
     */
    ZL_Report classifyFinalOutputs(size_t numOutputs)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        for (size_t outputIndex = 0; outputIndex < numOutputs; ++outputIndex) {
            PlannedStream& stream = plan_.streams.at(streamIndexOf(
                    static_cast<ZL_IDType>(numStreams_ - 1 - outputIndex)));
            if (stream.storageClass == StorageClass::StreamArena) {
                stream.storageClass = StorageClass::Destination;
                continue;
            }
            ZL_ERR_IF(
                    stream.storageClass != StorageClass::Stored
                            && stream.storageClass != StorageClass::StreamRef,
                    corruption);
        }
        return ZL_returnSuccess();
    }

    /**
     * Records a chunk-local stream if its slot is not already populated.
     *
     * Records a stored stream. A generated stream must already have been
     * recorded while planning its node.
     */
    ZL_Report recordStreamIfNeeded(ZL_IDType streamSlot)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);
        ZL_ERR_IF_GE(streamSlot, numStreams_, corruption);
        // If the stream is already recorded, there is nothing to do.
        if (plan_.streams.at(streamIndexOf(streamSlot)).storageClass
            != StorageClass::Unreached) {
            return ZL_returnSuccess();
        }

        const ZL_IDType producer =
                DCTX_getStreamProducerNodeIdx(&dctx_, streamSlot);
        // An unrecorded generated stream means its producer was not planned.
        ZL_ERR_IF_NE(producer, ZL_PRODUCER_STORE, corruption);
        ZL_ERR_IF_ERR(recordStoredStream(streamSlot));
        return ZL_returnSuccess();
    }

    /**
     * Records the shape and storage requirements of a stored input stream.
     *
     * Reads the stream's @c ZL_Data from @c dctx_ and writes a stored
     * @c PlannedStream into slot @p streamSlot. This handles both node inputs
     * and stored streams found after all nodes are planned.
     */
    ZL_Report recordStoredStream(ZL_IDType streamSlot)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        const ZL_Data* const data = ZL_DCtx_getConstStream(&dctx_, streamSlot);
        ZL_ERR_IF_NULL(data, corruption);
        size_t stringLengthsBytes = 0;
        if (ZL_Data_type(data) == ZL_Type_string) {
            ZL_ERR_IF(
                    ZL_overflowMulST(
                            ZL_Data_numElts(data),
                            sizeof(uint32_t),
                            &stringLengthsBytes),
                    integerOverflow);
        }
        return writeStream(
                streamSlot,
                PlannedStream{
                    .shape = {
                            .type     = ZL_Data_type(data),
                            .numElts  = ZL_Data_numElts(data),
                            .eltWidth = ZL_Data_eltWidth(data),
                    },
                    .storageSize = {
                            .dataBytes          = ZL_Data_contentSize(data),
                            .stringLengthsBytes = stringLengthsBytes,
                    },
                    .storageClass = StorageClass::Stored,
                });
    }

    /**
     * Plans one codec node after all earlier nodes in chunk execution order.
     *
     * Records the node's input and output streams, then adds one codec run to
     * @c plan_.
     */
    ZL_Report planNodeInOrder(const DFH_Struct& header, ZL_IDType nodeIndex)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        // Step 1: Validate the node index.
        ZL_ERR_IF_GE(nodeIndex, VECTOR_SIZE(header.nodes), corruption);
        ZL_ERR_IF_GE(
                plan_.codecRuns.size(),
                static_cast<size_t>(kNoRun),
                temporaryLibraryLimitation);

        // Step 2: Find the planner for the node's transform.
        const DFH_NodeInfo* const nodes = VECTOR_DATA(header.nodes);
        if (nodes == nullptr) {
            ZL_ERR(corruption);
        }
        const DFH_NodeInfo& node = *(nodes + nodeIndex);
        CodecDecodePlanningFn* const planner =
                findCodecDecodePlanner(node.trpid, header.formatVersion);
        if (planner == nullptr) {
            ZL_ERR(temporaryLibraryLimitation);
        }

        size_t inputEnd = 0;

        // Step 3: Validate the range of streams entering the node.
        ZL_ERR_IF_ERR(validateNodeInputRange(node, inputEnd));
        // This node's run will be appended at this index.
        const auto runIndex =
                static_cast<PlannedRunIndex>(plan_.codecRuns.size());

        IndexRange inputs{};

        // Step 4: Collect inputs in decoder argument order.
        ZL_ERR_IF_ERR(collectInputProperties(node, runIndex, inputEnd, inputs));

        // Step 5: Find and validate the node's output slots.
        ZL_ERR_IF_ERR(resolveOutputSlots(node, nodeIndex, inputEnd));

        ByteRange codecHeader{};

        // Step 6: Find the node's codec header in the staged headers.
        ZL_ERR_IF_ERR(resolveCodecHeader(node, codecHeader));

        // Step 7: Ask the codec planner for each output's properties.
        ZL_ERR_IF_ERR(planCodecOutputs(
                node, header.formatVersion, *planner, codecHeader));

        IndexRange outputs{};

        // Step 8: Validate and record the output streams.
        ZL_ERR_IF_ERR(writeOutputStreams(runIndex, inputs, outputs));

        // Step 9: Record the runs that produced the inputs, then record this
        // run.
        ZL_ERR_IF_ERR(recordDependenciesAndRun(
                nodeIndex, node.trpid, codecHeader, inputs, outputs));
        return ZL_returnSuccess();
    }

    /**
     * Validates the node's contiguous input-stream range.
     *
     * Writes the exclusive end slot to @p inputEnd.
     */
    ZL_Report validateNodeInputRange(const DFH_NodeInfo& node, size_t& inputEnd)
            const
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        const size_t inputBase = node.inputStreamBaseIdx;
        const size_t numInputs = node.numInputStreams;
        ZL_ERR_IF_GT(inputBase, numStreams_, corruption);
        ZL_ERR_IF_GT(numInputs, numStreams_ - inputBase, corruption);
        inputEnd = inputBase + numInputs;
        return ZL_returnSuccess();
    }

    /**
     * Collects the node's stream inputs in decoder argument order.
     *
     * Records stored inputs when needed, marks @p consumingRun as their
     * consumer, appends their indexes to @c plan_.streamIndices, and writes the
     * new range to @p inputs. It also rebuilds @c planningInputs_.
     */
    ZL_Report collectInputProperties(
            const DFH_NodeInfo& node,
            PlannedRunIndex consumingRun,
            size_t inputEnd,
            IndexRange& inputs)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        const size_t numInputs = node.numInputStreams;
        planningInputs_.clear();
        planningInputs_.reserve(numInputs);
        inputs.begin = plan_.streamIndices.size();
        inputs.count = numInputs;
        for (size_t inputIndex = 0; inputIndex < numInputs; ++inputIndex) {
            // Slots run backward, while decoder arguments run forward.
            const auto streamSlot =
                    static_cast<ZL_IDType>(inputEnd - 1 - inputIndex);
            // Record a stored input if no earlier node recorded this slot.
            ZL_ERR_IF_ERR(recordStreamIfNeeded(streamSlot));
            const PlannedStreamIndex index = streamIndexOf(streamSlot);
            PlannedStream& planned         = plan_.streams.at(index);
            // Each stream can enter only one node.
            ZL_ERR_IF_NE(planned.consumer, kNoRun, corruption);
            planned.consumer = consumingRun;
            plan_.streamIndices.push_back(index);
            planningInputs_.push_back(
                    {
                            .shape            = planned.shape,
                            .availableStorage = planned.storageSize,
                    });
        }
        return ZL_returnSuccess();
    }

    /**
     * Finds and validates the node's output slots.
     *
     * Rebuilds @c outputSlots_ without writing the stream records.
     */
    ZL_Report resolveOutputSlots(
            const DFH_NodeInfo& node,
            ZL_IDType nodeIndex,
            size_t inputEnd)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        outputSlots_.clear();
        outputSlots_.reserve(node.nbRegens);
        if (node.nbRegens == 0) {
            return ZL_returnSuccess();
        }
        // Each distance is an output slot offset from inputEnd.
        const uint32_t* const regenDistances = node.regenDistances;
        if (regenDistances == nullptr) {
            ZL_ERR(corruption);
        }
        for (const uint32_t regenDistance :
             std::span<const uint32_t>{ regenDistances, node.nbRegens }) {
            size_t streamSlot;
            ZL_ERR_IF(
                    ZL_overflowAddST(inputEnd, regenDistance, &streamSlot),
                    integerOverflow);
            ZL_ERR_IF_GE(streamSlot, numStreams_, corruption);
            const auto slot = static_cast<ZL_IDType>(streamSlot);
            ZL_ERR_IF_NE(
                    DCTX_getStreamProducerNodeIdx(&dctx_, slot),
                    nodeIndex,
                    corruption);
            ZL_ERR_IF(
                    std::find(outputSlots_.begin(), outputSlots_.end(), slot)
                            != outputSlots_.end(),
                    corruption);
            ZL_ERR_IF(
                    plan_.streams.at(streamIndexOf(slot)).storageClass
                            != StorageClass::Unreached,
                    corruption);
            outputSlots_.push_back(slot);
        }
        return ZL_returnSuccess();
    }

    /**
     * Finds the codec header for a node.
     *
     * Checks that the header fits in the staged headers and returns its offset
     * and size.
     */
    ZL_Report resolveCodecHeader(
            const DFH_NodeInfo& node,
            ByteRange& codecHeader) const
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        size_t headerEnd;
        ZL_ERR_IF(
                ZL_overflowAddST(node.trhStart, node.trhSize, &headerEnd),
                integerOverflow);
        ZL_ERR_IF_GT(headerEnd, chunk_.transformHeaders_h.size(), corruption);
        codecHeader = {
            .offset = node.trhStart,
            .size   = node.trhSize,
        };
        return ZL_returnSuccess();
    }

    /**
     * Calls the codec planner for the node's outputs.
     *
     * Rebuilds @c outputPlans_ from @c planningInputs_ and the selected codec
     * header.
     */
    ZL_Report planCodecOutputs(
            const DFH_NodeInfo& node,
            uint32_t frameFormatVersion,
            CodecDecodePlanningFn& planOutputs,
            ByteRange codecHeader)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        outputPlans_.assign(node.nbRegens, CodecDecodeOutputPlan{});
        const CodecDecodePlanningContext context{
            .transform          = node.trpid,
            .frameFormatVersion = frameFormatVersion,
            .codecHeader_h      = chunk_.transformHeaders_h.subspan(
                    codecHeader.offset, codecHeader.size),
        };
        ZL_ERR_IF_ERR(planOutputs(
                context,
                planningInputs_,
                outputPlans_,
                ZL_GET_OPERATION_CONTEXT(&dctx_)));
        return ZL_returnSuccess();
    }

    /**
     * Validates and records the node's output streams.
     *
     * Sets @p runIndex as each output's producer, appends the output indexes to
     * @c plan_.streamIndices, and writes the new range to @p outputs.
     */
    ZL_Report writeOutputStreams(
            PlannedRunIndex runIndex,
            IndexRange inputs,
            IndexRange& outputs)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        outputs.begin = plan_.streamIndices.size();
        outputs.count = outputSlots_.size();
        for (size_t outputIndex = 0; outputIndex < outputSlots_.size();
             ++outputIndex) {
            const ZL_IDType slot                = outputSlots_[outputIndex];
            const CodecDecodeOutputPlan& output = outputPlans_.at(outputIndex);

            PlannedStreamIndex aliasStream = kNoStream;
            size_t aliasOffset             = 0;

            // Check that an alias fits in its input, then record the backing
            // stream and byte offset.
            if (output.alias.has_value()) {
                const StreamInputAlias local = *output.alias;
                ZL_ERR_IF_GE(local.inputIndex, inputs.count, corruption);
                const size_t inputCapacity =
                        planningInputs_.at(local.inputIndex)
                                .availableStorage.dataBytes;
                ZL_ERR_IF_GT(local.offsetBytes, inputCapacity, corruption);
                size_t aliasEnd;
                ZL_ERR_IF(
                        ZL_overflowAddST(
                                local.offsetBytes,
                                output.storageSize.dataBytes,
                                &aliasEnd),
                        integerOverflow);
                ZL_ERR_IF_GT(aliasEnd, inputCapacity, corruption);
                aliasStream =
                        plan_.streamIndices.at(inputs.begin + local.inputIndex);
                aliasOffset = local.offsetBytes;
            }

            ZL_ERR_IF_ERR(writeStream(
                    slot,
                    PlannedStream{
                            .shape        = output.shape,
                            .storageSize  = output.storageSize,
                            .storageClass = aliasStream == kNoStream
                                    ? StorageClass::StreamArena
                                    : StorageClass::StreamRef,
                            .producer     = runIndex,
                            .aliasStream  = aliasStream,
                            .aliasOffset  = aliasOffset,
                    }));
            plan_.streamIndices.push_back(streamIndexOf(slot));
        }
        return ZL_returnSuccess();
    }

    /**
     * Records the codec run after its input and output streams are written.
     *
     * Stored inputs have no producing run. When two inputs come from the same
     * run, the dependency list includes that run once.
     */
    ZL_Report recordDependenciesAndRun(
            ZL_IDType nodeIndex,
            PublicTransformInfo transform,
            ByteRange codecHeader,
            IndexRange inputs,
            IndexRange outputs)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(&dctx_);

        // New dependencies start here in runIndices.
        const size_t dependencyBegin = plan_.runIndices.size();
        for (size_t offset = 0; offset < inputs.count; ++offset) {
            const PlannedStreamIndex input =
                    plan_.streamIndices.at(inputs.begin + offset);
            const PlannedRunIndex producer = plan_.streams.at(input).producer;
            // Add each run that produced an input once.
            if (producer != kNoRun
                && std::find(
                           plan_.runIndices.begin() + dependencyBegin,
                           plan_.runIndices.end(),
                           producer)
                        == plan_.runIndices.end()) {
                plan_.runIndices.push_back(producer);
            }
        }

        // Store the ranges for this run's inputs, outputs, and dependencies.
        plan_.codecRuns.push_back(
                {
                        .chunkNode    = { .chunkIndex = chunkIndex_,
                                          .nodeIndex  = nodeIndex },
                        .transform    = transform,
                        .codecHeader  = codecHeader,
                        .inputs       = inputs,
                        .outputs      = outputs,
                        .dependencies = { .begin = dependencyBegin,
                                          .count = plan_.runIndices.size()
                                                  - dependencyBegin },
                });
        return ZL_returnSuccess();
    }

    size_t chunkIndex_;
    const PreparedGpuChunkView& chunk_;
    ZL_DCtx& dctx_;
    const DFH_Struct* header_;
    DecodePlan& plan_;
    /// Number of wire slots this chunk declares.
    size_t numStreams_;
    /// Index in @c plan_.streams of this chunk's slot zero.
    size_t chunkBase_ = 0;
    // Reuse this scratch space for every node in the chunk.
    std::vector<StreamPlanningInput> planningInputs_;
    std::vector<ZL_IDType> outputSlots_;
    std::vector<CodecDecodeOutputPlan> outputPlans_;
};

} // namespace

ZL_Report planDecode(
        std::span<const PreparedGpuChunkView> chunks,
        DecodePlan& plan)
{
    ZL_DCtx* const dctx = chunks.empty() ? nullptr : chunks.front().dctx;
    ZL_RESULT_DECLARE_SCOPE_REPORT(dctx);
    for (const PreparedGpuChunkView& chunk : chunks) {
        ZL_ERR_IF_NULL(chunk.dctx, parameter_invalid);
    }
    try {
        // Step 1: Build a temporary plan so an error leaves plan unchanged.
        DecodePlan result;
        result.chunks.reserve(chunks.size());

        // Step 2: Add each chunk to the plan.
        for (size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
            ChunkPlanner planner{ chunkIndex, chunks[chunkIndex], result };
            ZL_ERR_IF_ERR(planner.planChunk());
        }

        // Step 3: Replace plan only after every chunk succeeds.
        plan = std::move(result);
        return ZL_returnSuccess();
    } catch (const std::bad_alloc&) {
        ZL_ERR(allocation);
    }
}

} // namespace openzl::gpu
