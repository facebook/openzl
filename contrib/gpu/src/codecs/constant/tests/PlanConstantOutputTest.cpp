// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>

#include "contrib/gpu/src/codecs/constant/plan_constant_output.hpp"
#include "openzl/common/wire_format.h"

namespace openzl::gpu {
namespace {

TEST(PlanConstantOutputTest, SizesSerialAndFixedOutputs)
{
    // This test verifies constant planning preserves the input stream type and
    // width while expanding the header-specified count; it fails if either
    // constant variant produces the wrong shape or storage size, or aliases
    // its single input instead of materializing the repeated output.
    struct TestCase {
        ZL_IDType transformId;
        ZL_Type type;
        size_t eltWidth;
        size_t expectedBytes;
    };
    constexpr std::array testCases{
        TestCase{
                ZL_StandardTransformID_constant_serial, ZL_Type_serial, 1, 9 },
        TestCase{
                ZL_StandardTransformID_constant_fixed, ZL_Type_struct, 4, 36 },
    };

    for (const TestCase& testCase : testCases) {
        const StreamPlanningInput input{
            .shape = {
                    .type     = testCase.type,
                    .numElts  = 1,
                    .eltWidth = testCase.eltWidth,
            },
            .availableStorage = {
                    .dataBytes          = testCase.eltWidth,
                    .stringLengthsBytes = 0,
            },
        };
        const std::array codecHeader_h{ std::byte{ 9 } };
        const CodecDecodePlanningContext codec{
            .transform = {
                    .trt  = trt_standard,
                    .trid = testCase.transformId,
            },
            .frameFormatVersion = 21,
            .codecHeader_h      = codecHeader_h,
        };
        std::array<CodecDecodeOutputPlan, 1> outputs{};

        const ZL_Report result =
                planConstantOutputs(codec, { &input, 1 }, outputs, nullptr);

        ASSERT_FALSE(ZL_isError(result));
        EXPECT_EQ(outputs[0].shape.type, testCase.type);
        EXPECT_EQ(outputs[0].shape.numElts, 9);
        EXPECT_EQ(outputs[0].shape.eltWidth, testCase.eltWidth);
        EXPECT_EQ(outputs[0].storageSize.dataBytes, testCase.expectedBytes);
        EXPECT_EQ(outputs[0].storageSize.stringLengthsBytes, 0);
        EXPECT_FALSE(outputs[0].alias.has_value());
    }
}

TEST(PlanConstantOutputTest, RejectsOutputCapacityOverflow)
{
    // This test verifies repeated-element sizing uses checked arithmetic; it
    // fails if a corrupt header can wrap into a smaller storage requirement.
    const StreamPlanningInput input{
        .shape = {
                .type     = ZL_Type_struct,
                .numElts  = 1,
                .eltWidth = 8,
        },
        .availableStorage = {
                .dataBytes          = 8,
                .stringLengthsBytes = 0,
        },
    };
    constexpr size_t kOverflowingCount =
            std::numeric_limits<size_t>::max() / 8 + 1;
    std::array<std::byte, 10> codecHeader_h{};
    size_t value      = kOverflowingCount;
    size_t headerSize = 0;
    while (value >= 0x80) {
        codecHeader_h[headerSize++] =
                std::byte{ static_cast<unsigned char>(value | 0x80) };
        value >>= 7;
    }
    codecHeader_h[headerSize++] =
            std::byte{ static_cast<unsigned char>(value) };
    const CodecDecodePlanningContext codec{
        .transform = {
                .trt  = trt_standard,
                .trid = ZL_StandardTransformID_constant_fixed,
        },
        .frameFormatVersion = 21,
        .codecHeader_h =
                std::span<const std::byte>{ codecHeader_h }.first(headerSize),
    };
    std::array<CodecDecodeOutputPlan, 1> outputs{};

    const ZL_Report result =
            planConstantOutputs(codec, { &input, 1 }, outputs, nullptr);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_integerOverflow);
}

TEST(PlanConstantOutputTest, RejectsCustomTransformIdentity)
{
    // This test verifies that a custom transform cannot borrow a standard
    // transform's numeric ID; it fails if planning ignores the core transform
    // namespace and dispatches solely on the numeric ID.
    const StreamPlanningInput input{
        .shape = {
                .type     = ZL_Type_struct,
                .numElts  = 1,
                .eltWidth = 4,
        },
        .availableStorage = {
                .dataBytes          = 4,
                .stringLengthsBytes = 0,
        },
    };
    const std::array codecHeader_h{ std::byte{ 9 } };
    const CodecDecodePlanningContext codec{
        .transform = {
                .trt  = trt_custom,
                .trid = ZL_StandardTransformID_constant_fixed,
        },
        .frameFormatVersion = 21,
        .codecHeader_h      = codecHeader_h,
    };
    std::array<CodecDecodeOutputPlan, 1> outputs{};

    const ZL_Report result =
            planConstantOutputs(codec, { &input, 1 }, outputs, nullptr);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_invalidTransform);
}

} // namespace
} // namespace openzl::gpu
