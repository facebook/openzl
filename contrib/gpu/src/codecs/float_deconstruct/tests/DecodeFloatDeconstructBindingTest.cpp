// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "contrib/gpu/src/codecs/float_deconstruct/decode_float_deconstruct_binding.hpp"
#include "contrib/gpu/src/codecs/float_deconstruct/tests/bf16_test_frames.h"
#include "openzl/codecs/float_deconstruct/common_float_deconstruct_binding.h"
#include "openzl/common/wire_format.h"
#include "openzl/zl_version.h"

namespace openzl::gpu {
namespace {

/**
 * The encoded streams of one bf16 float_deconstruct frame, which are the
 * decoder's inputs, and storage for its output, bound into a codec run.
 * Not movable, because the run points into its own members.
 */
struct Bf16RunFixture {
    explicit Bf16RunFixture(const std::vector<uint16_t>& values)
    {
        test::Bf16Streams streams = test::encodeBf16(values);
        signFrac                  = std::move(streams.signFrac);
        exponent                  = std::move(streams.exponent);
        output.resize(values.size());
        bind();
    }

    Bf16RunFixture(const Bf16RunFixture&)            = delete;
    Bf16RunFixture& operator=(const Bf16RunFixture&) = delete;

    void bind()
    {
        const size_t numElts = exponent.size();
        inputInfo[0]         = {
                    .shape            = { ZL_Type_struct, numElts, 1 },
                    .availableStorage = { signFrac.size(), 0 },
        };
        inputInfo[1] = {
            .shape            = { ZL_Type_serial, numElts, 1 },
            .availableStorage = { exponent.size(), 0 },
        };
        outputInfo[0] = {
            .shape       = { ZL_Type_numeric, numElts, sizeof(uint16_t) },
            .storageSize = { output.size() * sizeof(uint16_t), 0 },
        };
        inputs[0]  = { .data_d = signFrac.data() };
        inputs[1]  = { .data_d = exponent.data() };
        outputs[0] = { .data_d = output.data() };
        run        = {
                   .info    = { .transform          = { trt_standard,
                                                        ZL_StandardTransformID_float_deconstruct },
                                .frameFormatVersion = ZL_MAX_FORMAT_VERSION,
                                .codecHeader_h      = test::kBf16Header,
                                .inputs             = inputInfo,
                                .outputs            = outputInfo },
                   .inputs  = inputs,
                   .outputs = outputs,
        };
    }

    std::vector<uint8_t> signFrac;
    std::vector<uint8_t> exponent;
    std::vector<uint16_t> output;
    std::array<InputStreamInfo, 2> inputInfo{};
    std::array<OutputStreamInfo, 1> outputInfo{};
    std::array<BoundInputStream, 2> inputs{};
    std::array<BoundOutputStream, 1> outputs{};
    BoundCodecRun run{};
};

TEST(DecodeFloatDeconstructBindingTest, DescribesBoundBf16Run)
{
    // This fails if a valid run is rejected, or if its descriptor swaps the two
    // inputs or takes the wrong element count.
    const std::vector<uint16_t> values = test::makeBf16Values(1000, 1);
    Bf16RunFixture fixture{ values };
    ASSERT_EQ(fixture.exponent.size(), values.size());

    const std::optional<Bf16DeconElementRange> range =
            asBf16CodecRun(fixture.run);

    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->exponent, fixture.exponent.data());
    EXPECT_EQ(range->signFrac, fixture.signFrac.data());
    EXPECT_EQ(range->dst, fixture.output.data());
    EXPECT_EQ(range->nbElts, values.size());
}

TEST(DecodeFloatDeconstructBindingTest, RejectsRunsItCannotDecode)
{
    // Each case breaks one property of a valid run. This fails if any of them
    // is still described as a bf16 run.
    const std::vector<uint16_t> values = test::makeBf16Values(1000, 2);
    const std::byte f32Header[1]       = { std::byte{
            FLTDECON_ElementType_float32 } };
    const std::byte longHeader[2]      = {
        std::byte{ FLTDECON_ElementType_bfloat16 }, std::byte{ 0 }
    };
    const std::vector<
            std::pair<const char*, std::function<void(Bf16RunFixture&)>>>
            cases{
                { "f32 header",
                  [&](Bf16RunFixture& f) {
                      f.run.info.codecHeader_h = f32Header;
                  } },
                { "other transform",
                  [](Bf16RunFixture& f) { f.run.info.transform.trid += 1; } },
                { "one input",
                  [](Bf16RunFixture& f) {
                      f.run.info.inputs = poly::span<const InputStreamInfo>{
                          f.inputInfo.data(), 1
                      };
                  } },
                { "wide signFrac",
                  [](Bf16RunFixture& f) {
                      f.inputInfo[0].shape.eltWidth = 2;
                  } },
                { "count mismatch",
                  [](Bf16RunFixture& f) {
                      f.inputInfo[0].shape.numElts -= 1;
                  } },
                { "short output",
                  [](Bf16RunFixture& f) {
                      f.outputInfo[0].storageSize.dataBytes -= 1;
                  } },
                { "null exponent",
                  [](Bf16RunFixture& f) { f.inputs[1].data_d = nullptr; } },
                { "2-byte header",
                  [&](Bf16RunFixture& f) {
                      f.run.info.codecHeader_h = longHeader;
                  } },
                { "serial signFrac",
                  [](Bf16RunFixture& f) {
                      f.inputInfo[0].shape.type = ZL_Type_serial;
                  } },
                { "struct exponent",
                  [](Bf16RunFixture& f) {
                      f.inputInfo[1].shape.type = ZL_Type_struct;
                  } },
                { "wide exponent",
                  [](Bf16RunFixture& f) {
                      f.inputInfo[1].shape.eltWidth = 2;
                  } },
                { "serial output",
                  [](Bf16RunFixture& f) {
                      f.outputInfo[0].shape.type = ZL_Type_serial;
                  } },
                { "f32 output",
                  [](Bf16RunFixture& f) {
                      f.outputInfo[0].shape.eltWidth = 4;
                  } },
                { "short signFrac",
                  [](Bf16RunFixture& f) {
                      f.inputInfo[0].availableStorage.dataBytes -= 1;
                  } },
                { "short exponent",
                  [](Bf16RunFixture& f) {
                      f.inputInfo[1].availableStorage.dataBytes -= 1;
                  } },
                { "null signFrac",
                  [](Bf16RunFixture& f) { f.inputs[0].data_d  = nullptr; } },
                { "null output",
                  [](Bf16RunFixture& f) { f.outputs[0].data_d = nullptr; } },
                { "output bytes overflow",
                  [](Bf16RunFixture& f) {
                      // Only the overflow check can reject this: every
                      // storage size holds the count, and count * 2 wraps.
                      const size_t numElts          = SIZE_MAX / 2 + 1;
                      f.inputInfo[0].shape.numElts  = numElts;
                      f.inputInfo[1].shape.numElts  = numElts;
                      f.outputInfo[0].shape.numElts = numElts;
                      f.inputInfo[0].availableStorage.dataBytes = SIZE_MAX;
                      f.inputInfo[1].availableStorage.dataBytes = SIZE_MAX;
                      f.outputInfo[0].storageSize.dataBytes     = SIZE_MAX;
                  } },
            };

    for (const auto& [name, breakRun] : cases) {
        Bf16RunFixture fixture{ values };
        breakRun(fixture);
        EXPECT_FALSE(asBf16CodecRun(fixture.run).has_value()) << name;
    }
}

TEST(DecodeFloatDeconstructBindingTest, ChecksTheRunInfoWithoutAddresses)
{
    // Binding checks the run info before any memory exists. This fails if the
    // check of a valid run's info depends on an address.
    Bf16RunFixture fixture{ test::makeBf16Values(1000, 4) };
    fixture.inputs[0].data_d  = nullptr;
    fixture.inputs[1].data_d  = nullptr;
    fixture.outputs[0].data_d = nullptr;

    EXPECT_TRUE(isBf16CodecRun(fixture.run.info));
    EXPECT_FALSE(asBf16CodecRun(fixture.run).has_value());
}

TEST(DecodeFloatDeconstructBindingTest, AcceptsAnEmptyRunWithNullAddresses)
{
    // An empty stream may have no storage. This fails if a run with no
    // elements is rejected for its null addresses.
    Bf16RunFixture fixture{ test::makeBf16Values(1000, 3) };
    for (size_t i = 0; i < fixture.inputs.size(); ++i) {
        fixture.inputInfo[i].shape.numElts    = 0;
        fixture.inputInfo[i].availableStorage = {};
        fixture.inputs[i].data_d              = nullptr;
    }
    fixture.outputInfo[0].shape.numElts = 0;
    fixture.outputInfo[0].storageSize   = {};
    fixture.outputs[0].data_d           = nullptr;

    const std::optional<Bf16DeconElementRange> range =
            asBf16CodecRun(fixture.run);

    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->nbElts, 0);
}

} // namespace
} // namespace openzl::gpu
