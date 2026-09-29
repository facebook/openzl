// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>

#include "contrib/gpu/src/codecs/conversion/plan_conversion_output.hpp"
#include "openzl/common/wire_format.h"

namespace openzl::gpu {
namespace {

TEST(PlanConversionOutputTest, SizesEveryFixedWidthConversion)
{
    // This test verifies every fixed-width conversion shares one planning
    // contract; it fails if transform dispatch changes the decoded type,
    // element shape, required bytes, or input-alias eligibility.
    struct TestCase {
        ZL_IDType transformId;
        ZL_Type inputType;
        size_t inputNumElts;
        size_t inputEltWidth;
        std::byte header;
        size_t headerSize;
        ZL_Type outputType;
        size_t outputNumElts;
        size_t outputEltWidth;
        bool aliasesInput;
    };
    constexpr std::array testCases{
        TestCase{ ZL_StandardTransformID_convert_serial_to_struct,
                  ZL_Type_struct,
                  3,
                  4,
                  std::byte{ 0 },
                  0,
                  ZL_Type_serial,
                  12,
                  1,
                  true },
        TestCase{ ZL_StandardTransformID_convert_struct_to_serial,
                  ZL_Type_serial,
                  12,
                  1,
                  std::byte{ 4 },
                  1,
                  ZL_Type_struct,
                  3,
                  4,
                  true },
        TestCase{ ZL_StandardTransformID_convert_struct_to_num_le,
                  ZL_Type_numeric,
                  3,
                  4,
                  std::byte{ 0 },
                  0,
                  ZL_Type_struct,
                  3,
                  4,
                  true },
        TestCase{ ZL_StandardTransformID_convert_struct_to_num_be,
                  ZL_Type_numeric,
                  3,
                  4,
                  std::byte{ 0 },
                  0,
                  ZL_Type_struct,
                  3,
                  4,
                  false },
        TestCase{ ZL_StandardTransformID_convert_struct_to_num_be,
                  ZL_Type_numeric,
                  12,
                  1,
                  std::byte{ 0 },
                  0,
                  ZL_Type_struct,
                  12,
                  1,
                  true },
        TestCase{ ZL_StandardTransformID_convert_num_to_struct_le,
                  ZL_Type_struct,
                  3,
                  4,
                  std::byte{ 0 },
                  0,
                  ZL_Type_numeric,
                  3,
                  4,
                  true },
        TestCase{ ZL_StandardTransformID_convert_serial_to_num_le,
                  ZL_Type_numeric,
                  3,
                  4,
                  std::byte{ 0 },
                  0,
                  ZL_Type_serial,
                  12,
                  1,
                  true },
        TestCase{ ZL_StandardTransformID_convert_serial_to_num_be,
                  ZL_Type_numeric,
                  3,
                  4,
                  std::byte{ 0 },
                  0,
                  ZL_Type_serial,
                  12,
                  1,
                  false },
        TestCase{ ZL_StandardTransformID_convert_serial_to_num_be,
                  ZL_Type_numeric,
                  12,
                  1,
                  std::byte{ 0 },
                  0,
                  ZL_Type_serial,
                  12,
                  1,
                  true },
        TestCase{ ZL_StandardTransformID_convert_num_to_serial_le,
                  ZL_Type_serial,
                  12,
                  1,
                  std::byte{ 2 },
                  1,
                  ZL_Type_numeric,
                  3,
                  4,
                  true },
    };

    for (const TestCase& testCase : testCases) {
        const StreamPlanningInput input{
            .shape = {
                    .type     = testCase.inputType,
                    .numElts  = testCase.inputNumElts,
                    .eltWidth = testCase.inputEltWidth,
            },
            .availableStorage = {
                    .dataBytes          = 12,
                    .stringLengthsBytes = 0,
            },
        };
        const std::array headerStorage_h{ testCase.header };
        const CodecDecodePlanningContext codec{
            .transform = {
                    .trt  = trt_standard,
                    .trid = testCase.transformId,
            },
            .frameFormatVersion = 21,
            .codecHeader_h =
                    std::span<const std::byte>{ headerStorage_h }.first(
                            testCase.headerSize),
        };
        std::array<CodecDecodeOutputPlan, 1> outputs{};

        const ZL_Report result =
                planConversionOutputs(codec, { &input, 1 }, outputs, nullptr);

        ASSERT_FALSE(ZL_isError(result));
        EXPECT_EQ(outputs[0].shape.type, testCase.outputType);
        EXPECT_EQ(outputs[0].shape.numElts, testCase.outputNumElts);
        EXPECT_EQ(outputs[0].shape.eltWidth, testCase.outputEltWidth);
        EXPECT_EQ(outputs[0].storageSize.dataBytes, 12);
        EXPECT_EQ(outputs[0].storageSize.stringLengthsBytes, 0);
        EXPECT_EQ(outputs[0].alias.has_value(), testCase.aliasesInput);
        if (testCase.aliasesInput) {
            EXPECT_EQ(outputs[0].alias->inputIndex, 0);
            EXPECT_EQ(outputs[0].alias->offsetBytes, 0);
        }
    }
}

TEST(PlanConversionOutputTest, RejectsPartialOutputElement)
{
    // This test verifies serialized bytes must form complete numeric elements;
    // it fails if planning truncates a remainder into an undersized output.
    const StreamPlanningInput input{
        .shape = {
                .type     = ZL_Type_serial,
                .numElts  = 7,
                .eltWidth = 1,
        },
        .availableStorage = {
                .dataBytes          = 7,
                .stringLengthsBytes = 0,
        },
    };
    const std::array codecHeader_h{ std::byte{ 2 } };
    const CodecDecodePlanningContext codec{
        .transform = {
                .trt  = trt_standard,
                .trid = ZL_StandardTransformID_convert_num_to_serial_le,
        },
        .frameFormatVersion = 21,
        .codecHeader_h      = codecHeader_h,
    };
    std::array<CodecDecodeOutputPlan, 1> outputs{};

    const ZL_Report result =
            planConversionOutputs(codec, { &input, 1 }, outputs, nullptr);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_corruption);
}

} // namespace
} // namespace openzl::gpu
