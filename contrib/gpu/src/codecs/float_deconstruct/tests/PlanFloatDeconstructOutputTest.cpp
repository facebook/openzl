// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>

#include "contrib/gpu/src/codecs/float_deconstruct/plan_float_deconstruct_output.hpp"

namespace openzl::gpu {
namespace {

constexpr PublicTransformInfo kFloatDeconstructTransform{
    .trt  = trt_standard,
    .trid = ZL_StandardTransformID_float_deconstruct,
};

TEST(PlanFloatDeconstructOutputTest, SizesEveryVariant)
{
    // This test verifies that one codec-family planner sizes all current float
    // deconstruct wire variants; it fails if variant dispatch produces the
    // wrong input or output width, element count, type, allocation size, or
    // incorrectly claims that the reconstructed output references an input.
    struct TestCase {
        std::byte header;
        size_t signFracWidth;
        size_t outputWidth;
    };
    constexpr std::array testCases{
        TestCase{ .header        = std::byte{ 0 },
                  .signFracWidth = 3,
                  .outputWidth   = 4 },
        TestCase{ .header        = std::byte{ 1 },
                  .signFracWidth = 1,
                  .outputWidth   = 2 },
        TestCase{ .header        = std::byte{ 2 },
                  .signFracWidth = 2,
                  .outputWidth   = 2 },
    };
    constexpr size_t kNumElts = 17;

    for (const TestCase& testCase : testCases) {
        const std::array<StreamPlanningInput, 2> inputs{
            StreamPlanningInput{
                    .shape = {
                            .type     = ZL_Type_struct,
                            .numElts  = kNumElts,
                            .eltWidth = testCase.signFracWidth,
                    },
                    .availableStorage = {
                            .dataBytes = kNumElts * testCase.signFracWidth,
                            .stringLengthsBytes = 0,
                    },
            },
            StreamPlanningInput{
                    .shape = {
                            .type     = ZL_Type_serial,
                            .numElts  = kNumElts,
                            .eltWidth = 1,
                    },
                    .availableStorage = {
                            .dataBytes          = kNumElts,
                            .stringLengthsBytes = 0,
                    },
            },
        };
        const std::array codecHeader_h{ testCase.header };
        const CodecDecodePlanningContext codec{
            .transform          = kFloatDeconstructTransform,
            .frameFormatVersion = 21,
            .codecHeader_h      = codecHeader_h,
        };
        std::array<CodecDecodeOutputPlan, 1> outputs{};

        const ZL_Report result =
                planFloatDeconstructOutputs(codec, inputs, outputs, nullptr);

        ASSERT_FALSE(ZL_isError(result));
        EXPECT_EQ(ZL_validResult(result), 1);
        EXPECT_EQ(outputs[0].shape.type, ZL_Type_numeric);
        EXPECT_EQ(outputs[0].shape.numElts, kNumElts);
        EXPECT_EQ(outputs[0].shape.eltWidth, testCase.outputWidth);
        EXPECT_EQ(
                outputs[0].storageSize.dataBytes,
                kNumElts * testCase.outputWidth);
        EXPECT_EQ(outputs[0].storageSize.stringLengthsBytes, 0);
        EXPECT_FALSE(outputs[0].alias.has_value());
    }
}

TEST(PlanFloatDeconstructOutputTest, RejectsUndersizedInputCapacity)
{
    // This test verifies that planning rejects metadata which would let a
    // kernel read beyond an input allocation; it fails if an undersized stream
    // is accepted or if output metadata changes on error.
    const std::array<StreamPlanningInput, 2> inputs{
        StreamPlanningInput{
                .shape = {
                        .type     = ZL_Type_struct,
                        .numElts  = 4,
                        .eltWidth = 1,
                },
                .availableStorage = {
                        .dataBytes          = 3,
                        .stringLengthsBytes = 0,
                },
        },
        StreamPlanningInput{
                .shape = {
                        .type     = ZL_Type_serial,
                        .numElts  = 4,
                        .eltWidth = 1,
                },
                .availableStorage = {
                        .dataBytes          = 4,
                        .stringLengthsBytes = 0,
                },
        },
    };
    const std::array codecHeader_h{ std::byte{ 1 } };
    const CodecDecodePlanningContext codec{
        .transform          = kFloatDeconstructTransform,
        .frameFormatVersion = 21,
        .codecHeader_h      = codecHeader_h,
    };
    const std::array originalOutputs{
        CodecDecodeOutputPlan{
                .shape = {
                        .type     = ZL_Type_string,
                        .numElts  = 9,
                        .eltWidth = 1,
                },
                .storageSize = {
                        .dataBytes          = 63,
                        .stringLengthsBytes = 36,
                },
                .alias = StreamInputAlias{
                        .inputIndex  = 7,
                        .offsetBytes = 11,
                },
        },
    };
    auto outputs = originalOutputs;

    const ZL_Report result =
            planFloatDeconstructOutputs(codec, inputs, outputs, nullptr);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_corruption);
    EXPECT_EQ(outputs[0].shape.type, originalOutputs[0].shape.type);
    EXPECT_EQ(outputs[0].shape.numElts, originalOutputs[0].shape.numElts);
    EXPECT_EQ(outputs[0].shape.eltWidth, originalOutputs[0].shape.eltWidth);
    EXPECT_EQ(
            outputs[0].storageSize.dataBytes,
            originalOutputs[0].storageSize.dataBytes);
    EXPECT_EQ(
            outputs[0].storageSize.stringLengthsBytes,
            originalOutputs[0].storageSize.stringLengthsBytes);
    ASSERT_TRUE(outputs[0].alias.has_value());
    EXPECT_EQ(
            outputs[0].alias->inputIndex, originalOutputs[0].alias->inputIndex);
    EXPECT_EQ(
            outputs[0].alias->offsetBytes,
            originalOutputs[0].alias->offsetBytes);
}

TEST(PlanFloatDeconstructOutputTest, RejectsMalformedHeaderAsCorruption)
{
    // This test verifies the core wire-format contract for the element-type
    // enum; it fails if corrupt frame metadata reaches allocation planning.
    const std::array<StreamPlanningInput, 2> inputs{
        StreamPlanningInput{
                .shape = {
                        .type     = ZL_Type_struct,
                        .numElts  = 1,
                        .eltWidth = 1,
                },
                .availableStorage = {
                        .dataBytes          = 1,
                        .stringLengthsBytes = 0,
                },
        },
        StreamPlanningInput{
                .shape = {
                        .type     = ZL_Type_serial,
                        .numElts  = 1,
                        .eltWidth = 1,
                },
                .availableStorage = {
                        .dataBytes          = 1,
                        .stringLengthsBytes = 0,
                },
        },
    };
    const std::array codecHeader_h{ std::byte{ 255 } };
    const CodecDecodePlanningContext codec{
        .transform          = kFloatDeconstructTransform,
        .frameFormatVersion = 21,
        .codecHeader_h      = codecHeader_h,
    };
    std::array<CodecDecodeOutputPlan, 1> outputs{};

    const ZL_Report result =
            planFloatDeconstructOutputs(codec, inputs, outputs, nullptr);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_corruption);
}

TEST(PlanFloatDeconstructOutputTest, RejectsOutputCapacityOverflow)
{
    // This test verifies that output allocation size arithmetic is checked;
    // it fails if an element count whose byte size exceeds size_t wraps into a
    // smaller allocation or changes the caller's output metadata.
    constexpr size_t kNumElts = std::numeric_limits<size_t>::max() / 2 + 1;
    const std::array<StreamPlanningInput, 2> inputs{
        StreamPlanningInput{
                .shape = {
                        .type     = ZL_Type_struct,
                        .numElts  = kNumElts,
                        .eltWidth = 1,
                },
                .availableStorage = {
                        .dataBytes          = kNumElts,
                        .stringLengthsBytes = 0,
                },
        },
        StreamPlanningInput{
                .shape = {
                        .type     = ZL_Type_serial,
                        .numElts  = kNumElts,
                        .eltWidth = 1,
                },
                .availableStorage = {
                        .dataBytes          = kNumElts,
                        .stringLengthsBytes = 0,
                },
        },
    };
    const std::array codecHeader_h{ std::byte{ 1 } };
    const CodecDecodePlanningContext codec{
        .transform          = kFloatDeconstructTransform,
        .frameFormatVersion = 21,
        .codecHeader_h      = codecHeader_h,
    };
    const std::array originalOutputs{
        CodecDecodeOutputPlan{
                .shape = {
                        .type     = ZL_Type_string,
                        .numElts  = 9,
                        .eltWidth = 1,
                },
                .storageSize = {
                        .dataBytes          = 63,
                        .stringLengthsBytes = 36,
                },
                .alias = StreamInputAlias{
                        .inputIndex  = 7,
                        .offsetBytes = 11,
                },
        },
    };
    auto outputs = originalOutputs;

    const ZL_Report result =
            planFloatDeconstructOutputs(codec, inputs, outputs, nullptr);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_integerOverflow);
    EXPECT_EQ(outputs[0].shape.type, originalOutputs[0].shape.type);
    EXPECT_EQ(outputs[0].shape.numElts, originalOutputs[0].shape.numElts);
    EXPECT_EQ(outputs[0].shape.eltWidth, originalOutputs[0].shape.eltWidth);
    EXPECT_EQ(
            outputs[0].storageSize.dataBytes,
            originalOutputs[0].storageSize.dataBytes);
    EXPECT_EQ(
            outputs[0].storageSize.stringLengthsBytes,
            originalOutputs[0].storageSize.stringLengthsBytes);
    ASSERT_TRUE(outputs[0].alias.has_value());
    EXPECT_EQ(
            outputs[0].alias->inputIndex, originalOutputs[0].alias->inputIndex);
    EXPECT_EQ(
            outputs[0].alias->offsetBytes,
            originalOutputs[0].alias->offsetBytes);
}

} // namespace
} // namespace openzl::gpu
