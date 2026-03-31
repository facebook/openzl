// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "openzl/openzl.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_public_nodes.h"

using namespace testing;

namespace openzl::tests {
namespace {
class TestDCtx : public testing::Test {
   public:
    std::string compressSerial(poly::string_view input)
    {
        Compressor compressor;
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor.unwrap(ZL_Compressor_selectStartingGraphID(
                compressor.get(), ZL_GRAPH_ZSTD));
        CCtx cctx;
        cctx.refCompressor(compressor);
        return cctx.compressSerial(input);
    }

    std::string compressOne(const Input& input)
    {
        Compressor compressor;
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor.unwrap(ZL_Compressor_selectStartingGraphID(
                compressor.get(), ZL_GRAPH_COMPRESS_GENERIC));
        CCtx cctx;
        cctx.refCompressor(compressor);
        return cctx.compressOne(input);
    }

    std::string compress(poly::span<const Input> inputs)
    {
        Compressor compressor;
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor.unwrap(ZL_Compressor_selectStartingGraphID(
                compressor.get(), ZL_GRAPH_COMPRESS_GENERIC));
        CCtx cctx;
        cctx.refCompressor(compressor);
        return cctx.compress(inputs);
    }

    void SetUp() override
    {
        serialInput_.clear();
        serialInput_ += std::string(1000, 'a');
        serialInput_ += "hello world i am a string";
        serialInput_ += std::string(300, 'b');
        serialInput_ += std::string(1000, 'a');
        serialInput_ += "hello world I am a string that is different";

        numericInput_ = std::vector<int>(1000, 42);
        numericInput_.push_back(32);

        lengths_.clear();
        lengths_.push_back(0);
        lengths_.push_back(2);
        lengths_.push_back(20);
        lengths_.push_back(200);
        lengths_.push_back(2000);

        inputs_.clear();
        inputs_.push_back(Input::refSerial(serialInput_));
        inputs_.push_back(
                Input::refNumeric(poly::span<const int>(numericInput_)));
        inputs_.push_back(
                Input::refStruct(poly::span<const int>(numericInput_)));
        inputs_.push_back(
                Input::refString(
                        poly::string_view(serialInput_).substr(0, 2222),
                        lengths_));
    }

    std::string serialInput_;
    std::vector<int> numericInput_;
    std::vector<uint32_t> lengths_;
    std::vector<Input> inputs_;
    DCtx dctx_;
};
} // namespace

TEST_F(TestDCtx, get)
{
    openzl::DCtx dctx;
    ASSERT_NE(dctx.get(), nullptr);
}

TEST_F(TestDCtx, parameters)
{
    DCtx dctx;
    ASSERT_EQ(dctx.getParameter(DParam::CheckCompressedChecksum), 0);
    dctx.setParameter(DParam::CheckCompressedChecksum, 1);
    ASSERT_EQ(dctx.getParameter(DParam::CheckCompressedChecksum), 1);
    dctx.resetParameters();
    ASSERT_EQ(dctx.getParameter(DParam::CheckCompressedChecksum), 0);
}

TEST_F(TestDCtx, decompressSerial)
{
    auto compressed   = compressSerial(serialInput_);
    auto decompressed = dctx_.decompressSerial(compressed);
    ASSERT_TRUE(decompressed == serialInput_);
}

TEST_F(TestDCtx, decompressOne)
{
    for (const auto& input : inputs_) {
        auto compressed   = compressOne(input);
        auto decompressed = dctx_.decompressOne(compressed);
        ASSERT_EQ(input, decompressed);
    }
}

TEST_F(TestDCtx, decompress)
{
    auto compressed   = compress(inputs_);
    auto decompressed = dctx_.decompress(compressed);
    ASSERT_EQ(decompressed.size(), inputs_.size());
    for (size_t i = 0; i < decompressed.size(); ++i) {
        ASSERT_EQ(decompressed[i], inputs_[i]);
    }
}

TEST_F(TestDCtx, accessorsOnOutputWorkAsExpected)
{
    auto input = Input::refString(
            poly::string_view(serialInput_).substr(0, 2222), lengths_);
    auto compressed   = compressOne(input);
    auto decompressed = dctx_.decompressOne(compressed);
    ASSERT_EQ(decompressed, input);
    Output& mut       = decompressed;
    const Output& con = decompressed;
    /* const pointers are at the beginning of the buffer,
     * writable pointers are at the place to continue writing */
    EXPECT_GE(mut.stringLens(), con.stringLens());
    EXPECT_GE(mut.ptr(), con.ptr());
}

TEST_F(TestDCtx, decoderFailureHasCodecName)
{
    ZL_Type type = ZL_Type_serial;
    std::string compressed;
    {
        Compressor compressor;
        ZL_MIEncoderDesc desc = {
            .gd = {
                .CTid = 0,
                .inputTypes = &type,
                .nbInputs = 1,
                .soTypes =  &type,
                .nbSOs = 1,
            },
            .transform_f = [](ZL_Encoder* encoder, const ZL_Input** inputs, size_t numInputs) noexcept -> ZL_Report {
                ZL_RESULT_DECLARE_SCOPE_REPORT(encoder);
                auto input = inputs[0];
                auto output = ZL_Encoder_createTypedStream(encoder, 0, ZL_Input_numElts(input), ZL_Input_eltWidth(input));
                ZL_ERR_IF_NULL(output, allocation);
                memcpy(ZL_Output_ptr(output), ZL_Input_ptr(input), ZL_Input_contentSize(input));
                return ZL_Output_commit(output, ZL_Input_numElts(input));
            },
        };
        auto node  = compressor.registerCustomEncoder(desc);
        auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor.get(), node, ZL_GRAPH_ZSTD);
        compressor.unwrap(
                ZL_Compressor_selectStartingGraphID(compressor.get(), graph));
        CCtx cctx;
        cctx.refCompressor(compressor);
        cctx.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressed = cctx.compressSerial(
                "this is some data that i want to compress data data data data data data");
    }
    DCtx dctx;
    {
        auto name = std::unique_ptr<char[]>(new char[100]);
        strcpy(name.get(), "my_custom_decoder");
        ZL_MIDecoderDesc desc = {
            .gd = {
                .CTid = 0,
                .inputTypes = &type,
                .nbInputs = 1,
                .soTypes =  &type,
                .nbSOs = 1,
            },
            .transform_f = [](ZL_Decoder*, const ZL_Input**, size_t, const ZL_Input**, size_t) noexcept -> ZL_Report {
                ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
                return ZL_REPORT_ERROR(GENERIC, "my codec failed for some reason");
            },
            .name = name.get(),
        };
        dctx.registerCustomDecoder(desc);
    }
    // name is not out of scope
    try {
        (void)dctx.decompressSerial(compressed);
        ASSERT_FALSE(true) << "Must throw";
    } catch (const Exception& e) {
        ASSERT_NE(
                std::string(e.what()).find("my_custom_decoder"),
                std::string::npos);
    }
}
} // namespace openzl::tests
