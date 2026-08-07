// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "contrib/gpu/src/planning/decode_planner.hpp"
#include "contrib/gpu/testkit/frame_factory.h"
#include "contrib/gpu/testkit/multichunk_frame.h"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/FrameInfo.hpp"
#include "openzl/decompress/dctx2.h"
#include "openzl/openzl.hpp"
#include "openzl/zl_common_types.h"
#include "openzl/zl_data.h"

namespace openzl::gpu {
namespace {

constexpr std::array<size_t, 2> kChunkSizes{ 20000, 24000 };

class PerInputGraph final : public FunctionGraph {
   public:
    explicit PerInputGraph(GraphID graph) : graph_(graph) {}

    FunctionGraphDescription functionGraphDescription() const override
    {
        return {
            .name           = "per_input_graph",
            .inputTypeMasks = { TypeMask::Numeric, TypeMask::Numeric },
            .customGraphs   = { graph_ },
        };
    }

    void graph(GraphState& state) const override
    {
        for (Edge& edge : state.edges()) {
            edge.setDestination(state.customGraphs().front());
        }
    }

   private:
    GraphID graph_;
};

class PreparedHostFrame {
   public:
    explicit PreparedHostFrame(std::string frame) : frame_(std::move(frame))
    {
        FrameInfo frameInfo{ std::string_view{ frame_ } };
        size_t offset =
                ZL_validResult(ZL_getHeaderSize(frame_.data(), frame_.size()));
        while (offset < frame_.size()
               && static_cast<unsigned char>(frame_[offset]) != 0) {
            DCtx dctx;
            ZL_Report const setParameterResult = ZL_DCtx_setParameter(
                    dctx.get(),
                    ZL_DParam_enableCodecFusion,
                    ZL_TernaryParam_disable);
            if (ZL_isError(setParameterResult)) {
                throw std::runtime_error("failed to disable codec fusion");
            }
            ZL_Report const initResult =
                    DCTX_initFromFrameInfo(dctx.get(), frameInfo.get());
            if (ZL_isError(initResult)) {
                throw std::runtime_error("failed to initialize dctx");
            }
            ZL_RESULT_OF(DCTX_FrameChunkInfo)
            const chunkResult = DCTX_prepareFrameChunk(
                    dctx.get(), frame_.data(), frame_.size(), offset);
            if (ZL_RES_isError(chunkResult)) {
                throw std::runtime_error("failed to prepare frame chunk");
            }
            DCTX_FrameChunkInfo const chunk = ZL_RES_value(chunkResult);
            const DFH_Struct* const header  = DCtx_getFrameHeader(dctx.get());
            if (header == nullptr) {
                throw Exception("missing decoded frame header");
            }
            dctxs_.push_back(std::move(dctx));
            transformHeaders_.emplace_back(
                    offset + chunk.chunkHeaderSize, header->totalTHSize);
            offset += chunk.chunkSize;
        }

        views_.reserve(dctxs_.size());
        const auto* const bytes =
                reinterpret_cast<const std::byte*>(frame_.data());
        for (size_t i = 0; i < dctxs_.size(); ++i) {
            const auto [headerOffset, headerSize] = transformHeaders_[i];
            views_.push_back(
                    {
                            .dctx               = dctxs_[i].get(),
                            .transformHeaders_h = { bytes + headerOffset,
                                                    headerSize },
                    });
        }
    }

    std::span<const PreparedGpuChunkView> views() const
    {
        return views_;
    }

   private:
    std::string frame_;
    std::vector<DCtx> dctxs_;
    std::vector<std::pair<size_t, size_t>> transformHeaders_;
    std::vector<PreparedGpuChunkView> views_;
};

std::string makeFloatFrame()
{
    std::vector<float> values(kChunkSizes[0] + kChunkSizes[1]);
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = 1.0F + static_cast<float>(i % 1024) / 2048.0F;
    }

    Compressor compressor;
    const GraphID graph = compressor.buildStaticGraph(
            nodes::Float32Deconstruct::node,
            { graphs::Store{}(), graphs::Constant{}() });
    return testkit::makeMultiChunkFrame(
            compressor,
            {
                    testkit::ChunkSpec{ kChunkSizes[0], graph },
                    testkit::ChunkSpec{ kChunkSizes[1], graph },
            },
            Input::refNumeric<float>(values.data(), values.size()));
}

std::string makeUnsupportedFrame()
{
    std::vector<int32_t> values(40000);
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<int32_t>(i % 31);
    }
    Compressor compressor;
    return testkit::makeFrameWithGraph(
            compressor,
            graphs::Bitpack{}(),
            Input::refNumeric<int32_t>(values.data(), values.size()));
}

std::string makeReferencedOutputFrame()
{
    std::vector<float> values(1000, 1.0F);
    Compressor compressor;
    return testkit::makeFrameWithGraph(
            compressor,
            graphs::Store{}(),
            Input::refNumeric<float>(values.data(), values.size()));
}

std::string makeStoredOutputFrame()
{
    const std::string values(1000, '\x2a');
    Compressor compressor;
    return testkit::makeFrameWithGraph(
            compressor,
            graphs::Store{}(),
            Input::refSerial(values.data(), values.size()));
}

std::string makeTwoInputFloatFrame()
{
    std::vector<float> firstValues(20000, 1.25F);
    std::vector<float> secondValues(24000, 1.5F);
    const std::array inputs{
        Input::refNumeric<float>(firstValues.data(), firstValues.size()),
        Input::refNumeric<float>(secondValues.data(), secondValues.size()),
    };

    Compressor compressor;
    const GraphID floatGraph = compressor.buildStaticGraph(
            nodes::Float32Deconstruct::node,
            { graphs::Store{}(), graphs::Constant{}() });
    compressor.selectStartingGraph(compressor.registerFunctionGraph(
            std::make_shared<PerInputGraph>(floatGraph)));

    CCtx cctx;
    cctx.refCompressor(compressor);
    cctx.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    cctx.setParameter(CParam::StoreOnExpansion, ZL_TernaryParam_disable);
    cctx.setParameter(CParam::MinStreamSize, -1);
    return cctx.compress(inputs);
}

PlannedRunIndex
findRun(const DecodePlan& plan, size_t chunkIndex, ZL_IDType transformId)
{
    PlannedRunIndex found = kNoRun;
    for (size_t index = 0; index < plan.codecRuns.size(); ++index) {
        const PlannedCodecRun& run = plan.codecRuns.at(index);
        if (run.chunkNode.chunkIndex == chunkIndex
            && run.transform.trt == trt_standard
            && run.transform.trid == transformId) {
            if (found != kNoRun) {
                throw Exception("planned codec run is not unique");
            }
            found = static_cast<PlannedRunIndex>(index);
        }
    }
    if (found == kNoRun) {
        throw Exception("planned codec run not found");
    }
    return found;
}

/// The decoder runs in each chunk of the float test frames.
struct ChunkRuns {
    PlannedRunIndex conversion;
    PlannedRunIndex constant;
    PlannedRunIndex floatDeconstruct;
};

ChunkRuns findChunkRuns(const DecodePlan& plan, size_t chunkIndex)
{
    return {
        .conversion =
                findRun(plan,
                        chunkIndex,
                        ZL_StandardTransformID_convert_struct_to_serial),
        .constant = findRun(
                plan, chunkIndex, ZL_StandardTransformID_constant_serial),
        .floatDeconstruct = findRun(
                plan, chunkIndex, ZL_StandardTransformID_float_deconstruct),
    };
}

std::vector<size_t> planArraySizes(const DecodePlan& plan)
{
    return { plan.streams.size(),
             plan.codecRuns.size(),
             plan.streamIndices.size(),
             plan.runIndices.size(),
             plan.chunks.size() };
}

std::vector<const void*> planArrayBuffers(const DecodePlan& plan)
{
    return { plan.streams.data(),
             plan.codecRuns.data(),
             plan.streamIndices.data(),
             plan.runIndices.data(),
             plan.chunks.data() };
}

TEST(DecodePlannerTest, PlansCrossChunkDependenciesOnce)
{
    // This fails if graph planning duplicates a producer, crosses chunk
    // boundaries, drops wire identity, reverses decoder inputs, or computes
    // incorrect stream shapes, storage sizes, or aliases for uneven chunks.
    PreparedHostFrame prepared{ makeFloatFrame() };
    DecodePlan plan;
    ZL_Report const result = planDecode(prepared.views(), plan);
    ASSERT_FALSE(ZL_isError(result));

    ASSERT_EQ(plan.codecRuns.size(), 6);
    ASSERT_EQ(plan.chunks.size(), kChunkSizes.size());
    for (size_t chunkIndex = 0; chunkIndex < kChunkSizes.size(); ++chunkIndex) {
        const ChunkRuns runs              = findChunkRuns(plan, chunkIndex);
        const PlannedCodecRun& conversion = plan.codecRuns.at(runs.conversion);
        const PlannedCodecRun& constant   = plan.codecRuns.at(runs.constant);
        const PlannedCodecRun& floatDeconstruct =
                plan.codecRuns.at(runs.floatDeconstruct);

        ASSERT_EQ(conversion.inputs.count, 1);
        ASSERT_EQ(conversion.outputs.count, 1);
        const PlannedStreamIndex conversionInput = plan.inputsOf(conversion)[0];
        const PlannedStreamIndex conversionOutputIndex =
                plan.outputsOf(conversion)[0];
        const PlannedStream& conversionOutput =
                plan.streams.at(conversionOutputIndex);
        EXPECT_EQ(conversionOutput.shape.type, ZL_Type_struct);
        EXPECT_EQ(conversionOutput.shape.eltWidth, 3);
        EXPECT_EQ(
                conversionOutput.storageSize.dataBytes,
                kChunkSizes[chunkIndex] * 3);
        EXPECT_EQ(conversionOutput.producer, runs.conversion);
        EXPECT_EQ(conversionOutput.storageClass, StorageClass::StreamRef);
        // The alias must fit in its backing stream.
        const PlannedStream& backing =
                plan.streams.at(conversionOutput.aliasStream);
        EXPECT_EQ(backing.storageClass, StorageClass::Stored);
        EXPECT_GE(
                backing.storageSize.dataBytes,
                conversionOutput.aliasOffset
                        + conversionOutput.storageSize.dataBytes);

        const PlannedStream& storedInput = plan.streams.at(conversionInput);
        EXPECT_EQ(conversionOutput.aliasStream, conversionInput);
        EXPECT_EQ(storedInput.storageClass, StorageClass::Stored);
        EXPECT_EQ(storedInput.shape.type, ZL_Type_serial);
        EXPECT_EQ(storedInput.shape.eltWidth, 1);
        EXPECT_EQ(
                storedInput.storageSize.dataBytes, kChunkSizes[chunkIndex] * 3);
        EXPECT_EQ(storedInput.storageSize.stringLengthsBytes, 0);
        EXPECT_EQ(storedInput.producer, kNoRun);
        EXPECT_EQ(storedInput.aliasStream, kNoStream);

        ASSERT_EQ(constant.outputs.count, 1);
        const PlannedStreamIndex constantOutputIndex =
                plan.outputsOf(constant)[0];
        const PlannedStream& constantOutput =
                plan.streams.at(constantOutputIndex);
        EXPECT_EQ(constantOutput.shape.type, ZL_Type_serial);
        EXPECT_EQ(
                constantOutput.storageSize.dataBytes, kChunkSizes[chunkIndex]);
        EXPECT_EQ(constantOutput.producer, runs.constant);
        EXPECT_EQ(constantOutput.storageClass, StorageClass::StreamArena);

        ASSERT_EQ(floatDeconstruct.inputs.count, 2);
        ASSERT_EQ(floatDeconstruct.outputs.count, 1);
        EXPECT_EQ(plan.inputsOf(floatDeconstruct)[0], conversionOutputIndex);
        EXPECT_EQ(plan.inputsOf(floatDeconstruct)[1], constantOutputIndex);
        ASSERT_EQ(floatDeconstruct.dependencies.count, 2);
        EXPECT_EQ(plan.dependenciesOf(floatDeconstruct)[0], runs.conversion);
        EXPECT_EQ(plan.dependenciesOf(floatDeconstruct)[1], runs.constant);

        const PlannedStreamIndex floatOutputIndex =
                plan.outputsOf(floatDeconstruct)[0];
        const PlannedStream& floatOutput = plan.streams.at(floatOutputIndex);
        EXPECT_EQ(floatOutput.producer, runs.floatDeconstruct);
        EXPECT_EQ(floatOutput.shape.type, ZL_Type_numeric);
        EXPECT_EQ(floatOutput.shape.eltWidth, 4);
        EXPECT_EQ(
                floatOutput.storageSize.dataBytes,
                kChunkSizes[chunkIndex] * sizeof(float));
        EXPECT_EQ(floatOutput.storageClass, StorageClass::Destination);
        EXPECT_EQ(floatOutput.aliasStream, kNoStream);

        ASSERT_EQ(plan.numFinalOutputsOf(chunkIndex), 1);
        EXPECT_EQ(plan.finalOutputOf(chunkIndex, 0), floatOutputIndex);

        // Each codec header must fit in this chunk's staged headers.
        const size_t stagedBytes =
                prepared.views()[chunkIndex].transformHeaders_h.size();
        for (const PlannedRunIndex run :
             { runs.conversion, runs.constant, runs.floatDeconstruct }) {
            const ByteRange header = plan.codecRuns.at(run).codecHeader;
            EXPECT_LE(header.offset + header.size, stagedBytes);
        }
        EXPECT_GT(plan.codecRuns.at(runs.constant).codecHeader.size, 0);
    }
}

TEST(DecodePlannerTest, IndexesEveryWireSlotOfEveryChunk)
{
    // This fails if a chunk omits a declared slot, stores a stream at the wrong
    // index, or leaves a stream unplanned.
    PreparedHostFrame prepared{ makeFloatFrame() };
    DecodePlan plan;
    ASSERT_FALSE(ZL_isError(planDecode(prepared.views(), plan)));

    ASSERT_EQ(plan.chunks.size(), kChunkSizes.size());
    size_t expectedBegin = 0;
    for (size_t chunkIndex = 0; chunkIndex < plan.chunks.size(); ++chunkIndex) {
        const IndexRange range = plan.chunks.at(chunkIndex).streams;
        EXPECT_EQ(range.begin, expectedBegin);
        EXPECT_EQ(
                range.count,
                ZL_DCtx_getNumStreams(prepared.views()[chunkIndex].dctx));
        expectedBegin = range.begin + range.count;
        for (const PlannedStream& stream : plan.streamsOf(chunkIndex)) {
            EXPECT_NE(stream.storageClass, StorageClass::Unreached);
        }
    }
    EXPECT_EQ(expectedBegin, plan.streams.size());
}

TEST(DecodePlannerTest, MatchesThePreparedProducerMapSlotForSlot)
{
    // This fails if a stream is stored at the wrong slot, has the wrong storage
    // class, or points to the wrong producing node.
    PreparedHostFrame prepared{ makeFloatFrame() };
    DecodePlan plan;
    ASSERT_FALSE(ZL_isError(planDecode(prepared.views(), plan)));

    ASSERT_EQ(plan.chunks.size(), kChunkSizes.size());
    for (size_t chunkIndex = 0; chunkIndex < plan.chunks.size(); ++chunkIndex) {
        const ZL_DCtx* const dctx = prepared.views()[chunkIndex].dctx;
        const std::span<const PlannedStream> streams =
                plan.streamsOf(chunkIndex);
        size_t storedCount = 0;
        for (size_t slot = 0; slot < streams.size(); ++slot) {
            const PlannedStream& stream = streams[slot];
            const ZL_IDType producer    = DCTX_getStreamProducerNodeIdx(
                    dctx, static_cast<ZL_IDType>(slot));
            if (producer == ZL_PRODUCER_STORE) {
                ++storedCount;
                EXPECT_EQ(stream.storageClass, StorageClass::Stored);
                EXPECT_EQ(stream.producer, kNoRun);
                const ZL_Data* const data = ZL_DCtx_getConstStream(
                        dctx, static_cast<ZL_IDType>(slot));
                ASSERT_NE(data, nullptr);
                EXPECT_EQ(ZL_Data_type(data), stream.shape.type);
                EXPECT_EQ(ZL_Data_numElts(data), stream.shape.numElts);
                EXPECT_EQ(
                        ZL_Data_contentSize(data),
                        stream.storageSize.dataBytes);
            } else {
                ASSERT_NE(stream.producer, kNoRun);
                EXPECT_EQ(
                        plan.codecRuns.at(stream.producer).chunkNode,
                        (ChunkNodeId{ chunkIndex, producer }));
            }
        }
        EXPECT_GT(storedCount, 0);
    }
}

TEST(DecodePlannerTest, OrdersFinalOutputsFromTheLastSlotBackwards)
{
    // This fails if frame outputs are not mapped from the last slot backward.
    PreparedHostFrame prepared{ makeTwoInputFloatFrame() };
    DecodePlan plan;
    ASSERT_FALSE(ZL_isError(planDecode(prepared.views(), plan)));

    ASSERT_EQ(plan.chunks.size(), 1);
    ASSERT_EQ(plan.numFinalOutputsOf(0), 2);
    const PlannedStreamIndex first  = plan.finalOutputOf(0, 0);
    const PlannedStreamIndex second = plan.finalOutputOf(0, 1);
    const IndexRange range          = plan.chunks.at(0).streams;
    EXPECT_EQ(first, range.begin + range.count - 1);
    EXPECT_EQ(second, first - 1);
    EXPECT_EQ(plan.streams.at(first).storageClass, StorageClass::Destination);
    EXPECT_EQ(plan.streams.at(second).storageClass, StorageClass::Destination);
    // The frame's first compression input is its first output.
    EXPECT_EQ(plan.streams.at(first).shape.numElts, 20000);
    EXPECT_EQ(plan.streams.at(second).shape.numElts, 24000);
}

TEST(DecodePlannerTest, PlanOutlivesThePreparedChunks)
{
    // This fails if the plan keeps a pointer into the frame or staged headers.
    DecodePlan plan;
    {
        PreparedHostFrame prepared{ makeFloatFrame() };
        ASSERT_FALSE(ZL_isError(planDecode(prepared.views(), plan)));
    }

    ASSERT_EQ(plan.chunks.size(), kChunkSizes.size());
    size_t headerBytes = 0;
    for (const PlannedCodecRun& run : plan.codecRuns) {
        headerBytes += run.codecHeader.size;
    }
    EXPECT_GT(headerBytes, 0);
    EXPECT_EQ(
            plan.streams.at(plan.finalOutputOf(0, 0)).storageClass,
            StorageClass::Destination);
}

TEST(DecodePlannerTest, PlansAReferencedFinalOutputForTerminalCopy)
{
    // This fails if a referenced final output is rejected or loses the source
    // range that must be copied to the caller's output.
    PreparedHostFrame prepared{ makeReferencedOutputFrame() };
    DecodePlan plan;
    ASSERT_FALSE(ZL_isError(planDecode(prepared.views(), plan)));

    ASSERT_EQ(plan.chunks.size(), 1);
    const PlannedStream& output = plan.streams.at(plan.finalOutputOf(0, 0));
    ASSERT_EQ(output.storageClass, StorageClass::StreamRef);
    ASSERT_NE(output.aliasStream, kNoStream);
    EXPECT_EQ(output.aliasOffset, 0);
    const PlannedStream& backing = plan.streams.at(output.aliasStream);
    EXPECT_EQ(backing.storageClass, StorageClass::Stored);
    EXPECT_EQ(output.storageSize.dataBytes, backing.storageSize.dataBytes);
    EXPECT_EQ(
            output.storageSize.stringLengthsBytes,
            backing.storageSize.stringLengthsBytes);
}

TEST(DecodePlannerTest, PlansAStoredFinalOutputForTerminalCopy)
{
    // This fails if a stored final output is rejected or marked as a direct
    // destination instead of a stream that must be copied.
    PreparedHostFrame prepared{ makeStoredOutputFrame() };
    DecodePlan plan;
    ASSERT_FALSE(ZL_isError(planDecode(prepared.views(), plan)));

    ASSERT_EQ(plan.chunks.size(), 1);
    const PlannedStream& output = plan.streams.at(plan.finalOutputOf(0, 0));
    EXPECT_EQ(output.storageClass, StorageClass::Stored);
    EXPECT_EQ(output.shape.type, ZL_Type_serial);
    EXPECT_EQ(output.storageSize.dataBytes, 1000);
    EXPECT_EQ(output.producer, kNoRun);
    EXPECT_EQ(output.aliasStream, kNoStream);
}

TEST(DecodePlannerTest, RecordsOneConsumerPerConsumedStream)
{
    // This fails if an input does not name its consuming run or if two runs
    // consume the same stream.
    PreparedHostFrame prepared{ makeFloatFrame() };
    DecodePlan plan;
    ASSERT_FALSE(ZL_isError(planDecode(prepared.views(), plan)));

    const auto consumerOf = [&plan](PlannedStreamIndex stream) {
        return plan.streams.at(stream).consumer;
    };
    for (size_t chunkIndex = 0; chunkIndex < kChunkSizes.size(); ++chunkIndex) {
        const ChunkRuns runs              = findChunkRuns(plan, chunkIndex);
        const PlannedCodecRun& conversion = plan.codecRuns.at(runs.conversion);
        const PlannedCodecRun& constant   = plan.codecRuns.at(runs.constant);
        const PlannedCodecRun& floatDeconstruct =
                plan.codecRuns.at(runs.floatDeconstruct);

        ASSERT_EQ(conversion.inputs.count, 1);
        ASSERT_EQ(constant.inputs.count, 1);
        ASSERT_EQ(floatDeconstruct.inputs.count, 2);
        ASSERT_EQ(floatDeconstruct.outputs.count, 1);
        EXPECT_EQ(consumerOf(plan.inputsOf(conversion)[0]), runs.conversion);
        EXPECT_EQ(consumerOf(plan.inputsOf(constant)[0]), runs.constant);
        EXPECT_EQ(
                consumerOf(plan.inputsOf(floatDeconstruct)[0]),
                runs.floatDeconstruct);
        EXPECT_EQ(
                consumerOf(plan.inputsOf(floatDeconstruct)[1]),
                runs.floatDeconstruct);
        EXPECT_EQ(consumerOf(plan.outputsOf(floatDeconstruct)[0]), kNoRun);
    }
}

TEST(DecodePlannerTest, RecordsCodecRunsInFrameExecutionOrder)
{
    // This fails if runs follow output traversal order instead of decoder
    // execution order.
    PreparedHostFrame prepared{ makeTwoInputFloatFrame() };
    DecodePlan plan;
    ASSERT_FALSE(ZL_isError(planDecode(prepared.views(), plan)));

    ASSERT_EQ(plan.codecRuns.size(), 6);
    for (size_t runIndex = 0; runIndex < plan.codecRuns.size(); ++runIndex) {
        EXPECT_EQ(
                plan.codecRuns[runIndex].chunkNode.nodeIndex,
                static_cast<ZL_IDType>(runIndex));
    }
}

TEST(DecodePlannerTest, UnsupportedCodecLeavesPlanUnchanged)
{
    // This fails if an unsupported codec changes the output plan or returns the
    // wrong error.
    PreparedHostFrame supported{ makeFloatFrame() };
    DecodePlan plan;
    ASSERT_FALSE(ZL_isError(planDecode(supported.views(), plan)));
    const std::vector<size_t> originalSizes        = planArraySizes(plan);
    const std::vector<const void*> originalBuffers = planArrayBuffers(plan);

    PreparedHostFrame unsupported{ makeUnsupportedFrame() };
    std::vector<PreparedGpuChunkView> mixed{
        supported.views().front(),
        unsupported.views().front(),
    };
    ZL_Report const result = planDecode(mixed, plan);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_temporaryLibraryLimitation);
    EXPECT_EQ(planArraySizes(plan), originalSizes);
    EXPECT_EQ(planArrayBuffers(plan), originalBuffers);
}

TEST(DecodePlannerTest, RejectsTruncatedTransformHeaders)
{
    // This fails if a codec header can extend past the staged header bytes or
    // if the error changes the output plan.
    PreparedHostFrame prepared{ makeFloatFrame() };
    std::vector<PreparedGpuChunkView> truncated{ prepared.views().begin(),
                                                 prepared.views().end() };
    ASSERT_FALSE(truncated.back().transformHeaders_h.empty());
    truncated.back().transformHeaders_h =
            truncated.back().transformHeaders_h.first(
                    truncated.back().transformHeaders_h.size() - 1);

    DecodePlan plan;
    ZL_Report const result = planDecode(truncated, plan);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_corruption);
    for (const size_t size : planArraySizes(plan)) {
        EXPECT_EQ(size, 0);
    }
}

TEST(DecodePlannerTest, RejectsNullDecodeContext)
{
    // This fails if a null borrowed context is dereferenced or reported as an
    // error other than an invalid parameter.
    constexpr std::array chunks{
        PreparedGpuChunkView{ .dctx = nullptr, .transformHeaders_h = {} },
    };
    DecodePlan plan;

    ZL_Report const result = planDecode(chunks, plan);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_parameter_invalid);
}

} // namespace
} // namespace openzl::gpu
