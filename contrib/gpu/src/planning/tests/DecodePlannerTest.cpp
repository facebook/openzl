// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "contrib/gpu/src/planning/decode_planner.hpp"
#include "contrib/gpu/testkit/frame_factory.h"
#include "contrib/gpu/testkit/multichunk_frame.h"
#include "openzl/common/errors_internal.h"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/FrameInfo.hpp"
#include "openzl/cpp/poly/Span.hpp"
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

    poly::span<const PreparedGpuChunkView> views() const
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

PlannedCodecRunIndex
findCodecRun(const DecodePlan& plan, size_t chunkIndex, ZL_IDType transformId)
{
    std::optional<PlannedCodecRunIndex> found;
    for (size_t index = 0; index < plan.codecRuns.size(); ++index) {
        const PlannedCodecRun& codecRun = plan.codecRuns.at(index);
        if (codecRun.chunkNode.chunkIndex == chunkIndex
            && codecRun.transform.trt == trt_standard
            && codecRun.transform.trid == transformId) {
            if (found.has_value()) {
                throw Exception("planned codec run is not unique");
            }
            found = index;
        }
    }
    if (!found.has_value()) {
        throw Exception("planned codec run not found");
    }
    return *found;
}

/// Codec runs in each chunk of the float test frames.
struct ChunkCodecRuns {
    PlannedCodecRunIndex conversion;
    PlannedCodecRunIndex constant;
    PlannedCodecRunIndex floatDeconstruct;
};

ChunkCodecRuns findChunkCodecRuns(const DecodePlan& plan, size_t chunkIndex)
{
    return {
        .conversion = findCodecRun(
                plan,
                chunkIndex,
                ZL_StandardTransformID_convert_struct_to_serial),
        .constant = findCodecRun(
                plan, chunkIndex, ZL_StandardTransformID_constant_serial),
        .floatDeconstruct = findCodecRun(
                plan, chunkIndex, ZL_StandardTransformID_float_deconstruct),
    };
}

/// @returns The plan for @p chunks. Throws if planning fails.
DecodePlan planOrThrow(poly::span<const PreparedGpuChunkView> chunks)
{
    Result<DecodePlan> result = planDecode(chunks);
    if (result.isError()) {
        throw std::runtime_error(ZL_E_str(ZL_RES_error(result.report())));
    }
    return std::move(result).value();
}

TEST(DecodePlannerTest, PlansCrossChunkDependenciesOnce)
{
    // This fails if graph planning duplicates a producer, crosses chunk
    // boundaries, drops wire identity, reverses decoder inputs, or computes
    // incorrect stream shapes, storage sizes, or aliases for uneven chunks.
    PreparedHostFrame prepared{ makeFloatFrame() };
    const DecodePlan plan = planOrThrow(prepared.views());

    ASSERT_EQ(plan.codecRuns.size(), 6);
    ASSERT_EQ(plan.chunks.size(), kChunkSizes.size());
    for (size_t chunkIndex = 0; chunkIndex < kChunkSizes.size(); ++chunkIndex) {
        const ChunkCodecRuns codecRuns = findChunkCodecRuns(plan, chunkIndex);
        const PlannedCodecRun& conversion =
                plan.codecRuns.at(codecRuns.conversion);
        const PlannedCodecRun& constant = plan.codecRuns.at(codecRuns.constant);
        const PlannedCodecRun& floatDeconstruct =
                plan.codecRuns.at(codecRuns.floatDeconstruct);

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
        EXPECT_EQ(conversionOutput.producerCodecRun, codecRuns.conversion);
        EXPECT_EQ(conversionOutput.storageClass, StorageClass::StreamRef);
        // The alias must fit in its backing stream.
        ASSERT_TRUE(conversionOutput.aliasStream.has_value());
        const PlannedStream& backing =
                plan.streams.at(*conversionOutput.aliasStream);
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
        EXPECT_FALSE(storedInput.producerCodecRun.has_value());
        EXPECT_FALSE(storedInput.aliasStream.has_value());

        ASSERT_EQ(constant.outputs.count, 1);
        const PlannedStreamIndex constantOutputIndex =
                plan.outputsOf(constant)[0];
        const PlannedStream& constantOutput =
                plan.streams.at(constantOutputIndex);
        EXPECT_EQ(constantOutput.shape.type, ZL_Type_serial);
        EXPECT_EQ(
                constantOutput.storageSize.dataBytes, kChunkSizes[chunkIndex]);
        EXPECT_EQ(constantOutput.producerCodecRun, codecRuns.constant);
        EXPECT_EQ(constantOutput.storageClass, StorageClass::StreamArena);

        ASSERT_EQ(floatDeconstruct.inputs.count, 2);
        ASSERT_EQ(floatDeconstruct.outputs.count, 1);
        EXPECT_EQ(plan.inputsOf(floatDeconstruct)[0], conversionOutputIndex);
        EXPECT_EQ(plan.inputsOf(floatDeconstruct)[1], constantOutputIndex);
        ASSERT_EQ(floatDeconstruct.dependencies.count, 2);
        EXPECT_EQ(
                plan.dependenciesOf(floatDeconstruct)[0], codecRuns.conversion);
        EXPECT_EQ(plan.dependenciesOf(floatDeconstruct)[1], codecRuns.constant);

        const PlannedStreamIndex floatOutputIndex =
                plan.outputsOf(floatDeconstruct)[0];
        const PlannedStream& floatOutput = plan.streams.at(floatOutputIndex);
        EXPECT_EQ(floatOutput.producerCodecRun, codecRuns.floatDeconstruct);
        EXPECT_EQ(floatOutput.shape.type, ZL_Type_numeric);
        EXPECT_EQ(floatOutput.shape.eltWidth, 4);
        EXPECT_EQ(
                floatOutput.storageSize.dataBytes,
                kChunkSizes[chunkIndex] * sizeof(float));
        EXPECT_EQ(floatOutput.storageClass, StorageClass::Destination);
        EXPECT_FALSE(floatOutput.aliasStream.has_value());

        ASSERT_EQ(plan.numFinalOutputsOf(chunkIndex), 1);
        EXPECT_EQ(plan.finalOutputOf(chunkIndex, 0), floatOutputIndex);

        // Each codec header must fit in this chunk's staged headers.
        const size_t stagedBytes =
                prepared.views()[chunkIndex].transformHeaders_h.size();
        for (const PlannedCodecRunIndex codecRun :
             { codecRuns.conversion,
               codecRuns.constant,
               codecRuns.floatDeconstruct }) {
            const ByteRange header = plan.codecRuns.at(codecRun).codecHeader;
            EXPECT_LE(header.offset + header.size, stagedBytes);
        }
        EXPECT_GT(plan.codecRuns.at(codecRuns.constant).codecHeader.size, 0);
    }
}

TEST(DecodePlannerTest, IndexesEveryWireSlotOfEveryChunk)
{
    // This fails if a chunk omits a declared slot, stores a stream at the wrong
    // index, or leaves a stream unplanned.
    PreparedHostFrame prepared{ makeFloatFrame() };
    const DecodePlan plan = planOrThrow(prepared.views());

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

TEST(DecodePlannerTest, FindsTheChunkOfEveryStream)
{
    // This fails if chunkOf gives a stream at a range boundary to the chunk
    // next to its own.
    PreparedHostFrame prepared{ makeFloatFrame() };
    const DecodePlan plan = planOrThrow(prepared.views());

    ASSERT_GT(plan.chunks.size(), 1);
    for (size_t chunkIndex = 0; chunkIndex < plan.chunks.size(); ++chunkIndex) {
        const IndexRange range = plan.chunks.at(chunkIndex).streams;
        for (PlannedStreamIndex s = range.begin; s < range.begin + range.count;
             ++s) {
            EXPECT_EQ(plan.chunkOf(s), chunkIndex) << "stream " << s;
        }
    }
}

TEST(DecodePlannerTest, MatchesThePreparedProducerMapSlotForSlot)
{
    // This fails if a stream is stored at the wrong slot, has the wrong storage
    // class, or points to the wrong producing node.
    PreparedHostFrame prepared{ makeFloatFrame() };
    const DecodePlan plan = planOrThrow(prepared.views());

    ASSERT_EQ(plan.chunks.size(), kChunkSizes.size());
    for (size_t chunkIndex = 0; chunkIndex < plan.chunks.size(); ++chunkIndex) {
        const ZL_DCtx* const dctx = prepared.views()[chunkIndex].dctx;
        const poly::span<const PlannedStream> streams =
                plan.streamsOf(chunkIndex);
        size_t storedCount = 0;
        for (size_t slot = 0; slot < streams.size(); ++slot) {
            const PlannedStream& stream = streams[slot];
            const ZL_IDType producer    = DCTX_getStreamProducerNodeIdx(
                    dctx, static_cast<ZL_IDType>(slot));
            if (producer == ZL_PRODUCER_STORE) {
                ++storedCount;
                EXPECT_EQ(stream.storageClass, StorageClass::Stored);
                EXPECT_FALSE(stream.producerCodecRun.has_value());
                const ZL_Data* const data = ZL_DCtx_getConstStream(
                        dctx, static_cast<ZL_IDType>(slot));
                ASSERT_NE(data, nullptr);
                EXPECT_EQ(ZL_Data_type(data), stream.shape.type);
                EXPECT_EQ(ZL_Data_numElts(data), stream.shape.numElts);
                EXPECT_EQ(
                        ZL_Data_contentSize(data),
                        stream.storageSize.dataBytes);
            } else {
                ASSERT_TRUE(stream.producerCodecRun.has_value());
                EXPECT_EQ(
                        plan.codecRuns.at(*stream.producerCodecRun).chunkNode,
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
    const DecodePlan plan = planOrThrow(prepared.views());

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
        plan = planOrThrow(prepared.views());
    }

    ASSERT_EQ(plan.chunks.size(), kChunkSizes.size());
    size_t headerBytes = 0;
    for (const PlannedCodecRun& codecRun : plan.codecRuns) {
        headerBytes += codecRun.codecHeader.size;
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
    const DecodePlan plan = planOrThrow(prepared.views());

    ASSERT_EQ(plan.chunks.size(), 1);
    const PlannedStream& output = plan.streams.at(plan.finalOutputOf(0, 0));
    ASSERT_EQ(output.storageClass, StorageClass::StreamRef);
    ASSERT_TRUE(output.aliasStream.has_value());
    EXPECT_EQ(output.aliasOffset, 0);
    const PlannedStream& backing = plan.streams.at(*output.aliasStream);
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
    const DecodePlan plan = planOrThrow(prepared.views());

    ASSERT_EQ(plan.chunks.size(), 1);
    const PlannedStream& output = plan.streams.at(plan.finalOutputOf(0, 0));
    EXPECT_EQ(output.storageClass, StorageClass::Stored);
    EXPECT_EQ(output.shape.type, ZL_Type_serial);
    EXPECT_EQ(output.storageSize.dataBytes, 1000);
    EXPECT_FALSE(output.producerCodecRun.has_value());
    EXPECT_FALSE(output.aliasStream.has_value());
}

TEST(DecodePlannerTest, RecordsOneConsumerPerConsumedStream)
{
    // This fails if an input does not name its consuming codec run or if two
    // codec runs consume the same stream.
    PreparedHostFrame prepared{ makeFloatFrame() };
    const DecodePlan plan = planOrThrow(prepared.views());

    const auto consumerCodecRunOf = [&plan](PlannedStreamIndex stream) {
        return plan.streams.at(stream).consumerCodecRun;
    };
    for (size_t chunkIndex = 0; chunkIndex < kChunkSizes.size(); ++chunkIndex) {
        const ChunkCodecRuns codecRuns = findChunkCodecRuns(plan, chunkIndex);
        const PlannedCodecRun& conversion =
                plan.codecRuns.at(codecRuns.conversion);
        const PlannedCodecRun& constant = plan.codecRuns.at(codecRuns.constant);
        const PlannedCodecRun& floatDeconstruct =
                plan.codecRuns.at(codecRuns.floatDeconstruct);

        ASSERT_EQ(conversion.inputs.count, 1);
        ASSERT_EQ(constant.inputs.count, 1);
        ASSERT_EQ(floatDeconstruct.inputs.count, 2);
        ASSERT_EQ(floatDeconstruct.outputs.count, 1);
        EXPECT_EQ(
                consumerCodecRunOf(plan.inputsOf(conversion)[0]),
                codecRuns.conversion);
        EXPECT_EQ(
                consumerCodecRunOf(plan.inputsOf(constant)[0]),
                codecRuns.constant);
        EXPECT_EQ(
                consumerCodecRunOf(plan.inputsOf(floatDeconstruct)[0]),
                codecRuns.floatDeconstruct);
        EXPECT_EQ(
                consumerCodecRunOf(plan.inputsOf(floatDeconstruct)[1]),
                codecRuns.floatDeconstruct);
        EXPECT_FALSE(consumerCodecRunOf(plan.outputsOf(floatDeconstruct)[0])
                             .has_value());
    }
}

TEST(DecodePlannerTest, RecordsCodecRunsInFrameExecutionOrder)
{
    // This fails if codec runs follow output traversal order instead of
    // decoder execution order.
    PreparedHostFrame prepared{ makeTwoInputFloatFrame() };
    const DecodePlan plan = planOrThrow(prepared.views());

    ASSERT_EQ(plan.codecRuns.size(), 6);
    for (size_t runIndex = 0; runIndex < plan.codecRuns.size(); ++runIndex) {
        EXPECT_EQ(
                plan.codecRuns[runIndex].chunkNode.nodeIndex,
                static_cast<ZL_IDType>(runIndex));
    }
}

TEST(DecodePlannerTest, RejectsUnsupportedCodec)
{
    // This fails if an unsupported codec returns the wrong error.
    PreparedHostFrame supported{ makeFloatFrame() };
    PreparedHostFrame unsupported{ makeUnsupportedFrame() };
    std::vector<PreparedGpuChunkView> mixed{
        supported.views().front(),
        unsupported.views().front(),
    };
    EXPECT_EQ(
            ZL_errorCode(planDecode(mixed).report()),
            ZL_ErrorCode_temporaryLibraryLimitation);
}

TEST(DecodePlannerTest, RejectsTruncatedTransformHeaders)
{
    // This fails if a codec header can extend past the staged header bytes.
    PreparedHostFrame prepared{ makeFloatFrame() };
    std::vector<PreparedGpuChunkView> truncated{ prepared.views().begin(),
                                                 prepared.views().end() };
    ASSERT_FALSE(truncated.back().transformHeaders_h.empty());
    truncated.back().transformHeaders_h =
            truncated.back().transformHeaders_h.first(
                    truncated.back().transformHeaders_h.size() - 1);

    EXPECT_EQ(
            ZL_errorCode(planDecode(truncated).report()),
            ZL_ErrorCode_corruption);
}

TEST(DecodePlannerTest, RejectsNullDecodeContext)
{
    // This fails if a null borrowed context is dereferenced or reported as an
    // error other than an invalid parameter.
    constexpr std::array chunks{
        PreparedGpuChunkView{ .dctx = nullptr, .transformHeaders_h = {} },
    };

    EXPECT_EQ(
            ZL_errorCode(planDecode(chunks).report()),
            ZL_ErrorCode_parameter_invalid);
}

TEST(DecodePlannerTest, RejectsAnEmptyChunkList)
{
    // This fails if planning no chunks returns an empty plan, which would
    // decode nothing and report success.
    EXPECT_EQ(
            ZL_errorCode(planDecode({}).report()),
            ZL_ErrorCode_parameter_invalid);
}

TEST(DecodePlannerTest, RejectsChunksOfMoreThanOneFrame)
{
    // This fails if chunks of two frames share one plan, where their final
    // outputs would share output indexes and land in the same buffers.
    PreparedHostFrame first{ makeStoredOutputFrame() };
    PreparedHostFrame second{ makeStoredOutputFrame() };
    std::vector<PreparedGpuChunkView> chunks{
        first.views().front(),
        second.views().front(),
    };
    chunks.back().frameIndex = 1;

    EXPECT_EQ(
            ZL_errorCode(planDecode(chunks).report()),
            ZL_ErrorCode_temporaryLibraryLimitation);
}

/// @returns The message of the error that planning @p chunks returns.
std::string planErrorMessage(poly::span<const PreparedGpuChunkView> chunks)
{
    return ZL_E_str(ZL_RES_error(planDecode(chunks).report()));
}

TEST(DecodePlannerTest, RejectsFinalOutputOfTheWrongType)
{
    // This fails if a chunk's final output can have a type other than the
    // frame's output type, which core rejects.
    PreparedHostFrame serial{ makeStoredOutputFrame() };
    PreparedHostFrame numeric{ makeReferencedOutputFrame() };
    const std::vector<PreparedGpuChunkView> chunks{
        serial.views().front(),
        numeric.views().front(),
    };

    EXPECT_EQ(
            ZL_errorCode(planDecode(chunks).report()), ZL_ErrorCode_corruption);
    EXPECT_NE(
            planErrorMessage(chunks).find("Final stream type mismatch"),
            std::string::npos);
}

std::string makeStoredInt16Frame()
{
    std::vector<int16_t> values(1000, 7);
    Compressor compressor;
    return testkit::makeFrameWithGraph(
            compressor,
            graphs::Store{}(),
            Input::refNumeric<int16_t>(values.data(), values.size()));
}

TEST(DecodePlannerTest, RejectsChunksWithDifferentOutputCounts)
{
    // This fails if a chunk with more final outputs than the first chunk is
    // accepted, which would check the wrong streams against the frame header.
    PreparedHostFrame oneOutput{ makeStoredOutputFrame() };
    PreparedHostFrame twoOutputs{ makeTwoInputFloatFrame() };
    const std::vector<PreparedGpuChunkView> chunks{
        oneOutput.views().front(),
        twoOutputs.views().front(),
    };

    EXPECT_EQ(
            ZL_errorCode(planDecode(chunks).report()), ZL_ErrorCode_corruption);
    EXPECT_NE(
            planErrorMessage(chunks).find("disagree on the number of final"),
            std::string::npos);
}

TEST(DecodePlannerTest, RejectsFinalOutputWithAnotherElementWidth)
{
    // This fails if one final output can have a different element width in
    // each chunk, which core rejects when it appends the chunks.
    PreparedHostFrame wide{ makeReferencedOutputFrame() };
    PreparedHostFrame narrow{ makeStoredInt16Frame() };
    const std::vector<PreparedGpuChunkView> chunks{
        wide.views().front(),
        narrow.views().front(),
    };

    EXPECT_EQ(
            ZL_errorCode(planDecode(chunks).report()), ZL_ErrorCode_corruption);
    EXPECT_NE(
            planErrorMessage(chunks).find("element width mismatch"),
            std::string::npos);
}

TEST(DecodePlannerTest, RejectsFinalOutputsLargerThanTheFrameDeclares)
{
    // This fails if the chunks' outputs can add up to more than the frame's
    // declared size, which core rejects.
    PreparedHostFrame prepared{ makeStoredOutputFrame() };
    const std::vector<PreparedGpuChunkView> chunks{
        prepared.views().front(),
        prepared.views().front(),
    };

    EXPECT_EQ(
            ZL_errorCode(planDecode(chunks).report()), ZL_ErrorCode_corruption);
    EXPECT_NE(
            planErrorMessage(chunks).find("Regenerated size"),
            std::string::npos);
}

TEST(DecodePlannerTest, RejectsFinalOutputsSmallerThanTheFrameDeclares)
{
    // This fails if a partial chunk list is accepted, which core rejects as
    // corruption because the outputs fall short of the declared size.
    PreparedHostFrame prepared{ makeFloatFrame() };
    ASSERT_GT(prepared.views().size(), 1);
    const std::vector<PreparedGpuChunkView> chunks{ prepared.views().front() };

    EXPECT_EQ(
            ZL_errorCode(planDecode(chunks).report()), ZL_ErrorCode_corruption);
    EXPECT_NE(
            planErrorMessage(chunks).find("Regenerated size"),
            std::string::npos);
}

} // namespace
} // namespace openzl::gpu
