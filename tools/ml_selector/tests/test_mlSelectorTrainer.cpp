// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "tests/datagen/DataGen.h"
#include "tests/ml_selector_utils.h"
#include "tools/ml_selector/ml_features.h"
#include "tools/ml_selector/ml_selector_graph.h"
#include "tools/training/train.h"
#include "tools/training/train_params.h"
#include "tools/training/utils/utils.h"

namespace openzl::tests {
namespace {

class TestMLSelectorGraph : public testing::Test {
   public:
    void SetUp() override
    {
        trainParams_ = {
            .compressorGenFunc =
                    [](std::string_view serialized) {
                        auto compressor =
                                std::make_unique<openzl::Compressor>();
                        compressor->deserialize(serialized);
                        return compressor;
                    },
        };
        deltaData_    = generateDeltaData();
        auto dg       = openzl::tests::datagen::DataGen();
        tokenizeData_ = dg.template randLongVector<uint64_t>(
                "randLongVec", 0, 500, 10000, 10000);
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        numericInputs_.emplace_back();
        numericInputs_.back().add(
                Input::refNumeric(deltaData_.data(), deltaData_.size()));

        numericInputs_.emplace_back();
        numericInputs_.back().add(
                Input::refNumeric(tokenizeData_.data(), tokenizeData_.size()));

        serialInputs_.emplace_back();
        serialInputs_.back().add(
                Input::refNumeric(deltaData_.data(), deltaData_.size()));

        serialInputs_.emplace_back();
        serialInputs_.back().add(
                Input::refNumeric(tokenizeData_.data(), tokenizeData_.size()));

        SetUpCompressorWithStaticSuccessors();
    }

    void SetUpCompressorWithStaticSuccessors()
    {
        auto deltaGid_ = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_.get(), ZL_NODE_DELTA_INT, ZL_GRAPH_ZSTD);

        auto tokenizeGid_ = ZL_Compressor_registerTokenizeGraph(
                compressor_.get(),
                ZL_Type_numeric,
                true,
                deltaGid_,
                ZL_GRAPH_ZSTD);

        successors_ = { deltaGid_, tokenizeGid_ };

        ZL_RESULT_OF(ZL_GraphID)
        mlSelectorResult = MLSelector_registerGraphWithEmptyGBTModel(
                compressor_.get(), successors_.data(), successors_.size());
        ASSERT_FALSE(ZL_RES_isError(mlSelectorResult));

        ZL_GraphID mlSelectorGraphId = ZL_RES_value(mlSelectorResult);

        ZL_GraphID _ = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_.get(),
                ZL_NODE_CONVERT_SERIAL_TO_NUM_LE64,
                mlSelectorGraphId);
    }

    Compressor deserializeCompressor(
            std::shared_ptr<const std::string_view>& serializedCompressor)
    {
        Compressor mlCompressor;
        auto graphid = ZL_MLSelector_registerBaseGraph(mlCompressor.get());
        EXPECT_TRUE(!ZL_RES_isError(graphid));
        mlCompressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        mlCompressor.deserialize(*serializedCompressor.get());

        return mlCompressor;
    }

    size_t compress(
            Compressor& compressor,
            std::string& dst,
            const std::vector<uint64_t>& input)
    {
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        cctx_.refCompressor(compressor);
        auto s_input =
                Input::refSerial(input.data(), input.size() * sizeof(uint64_t));
        return cctx_.compressOne(dst, s_input);
    }

    void testRoundTrip(
            const std::vector<uint64_t>& input,
            Compressor& compressor)
    {
        // Compress using ml selector graph
        std::string cBuffer = std::string(
                ZL_compressBound(input.size() * sizeof(uint64_t)), '\0');
        auto compressedSize = compress(compressor, cBuffer, input);
        cBuffer.resize(compressedSize);

        // Decompress and verify that the result is the same as the input
        const auto decompressedOutput = dctx_.decompressSerial(cBuffer);

        const uint64_t* output =
                reinterpret_cast<const uint64_t*>(decompressedOutput.data());
        std::vector<uint64_t> outputVec(
                output, output + decompressedOutput.size() / sizeof(uint64_t));

        EXPECT_EQ(outputVec, input);
    }

   protected:
    Compressor compressor_;
    CCtx cctx_;
    DCtx dctx_;

    std::vector<uint64_t> deltaData_;
    std::vector<uint64_t> tokenizeData_;
    std::vector<uint64_t> serialData_;
    std::vector<training::MultiInput> numericInputs_;
    std::vector<training::MultiInput> serialInputs_;
    openzl::training::TrainParams trainParams_;

    std::vector<ZL_GraphID> successors_;
    std::vector<std::string> successorLabels_ = { "delta", "tokenize" };
};

TEST_F(TestMLSelectorGraph, TestNumericRoundTrip)
{
    auto serializedTrainedCompressors =
            openzl::training::train(numericInputs_, compressor_, trainParams_);

    ASSERT_FALSE(serializedTrainedCompressors.empty());
    ASSERT_NE(serializedTrainedCompressors[0], nullptr);

    Compressor comp = deserializeCompressor(serializedTrainedCompressors[0]);
    testRoundTrip(deltaData_, comp);
}

TEST_F(TestMLSelectorGraph, TestSerialRoundTrip)
{
    auto serializedTrainedCompressors =
            openzl::training::train(serialInputs_, compressor_, trainParams_);

    ASSERT_FALSE(serializedTrainedCompressors.empty());
    ASSERT_NE(serializedTrainedCompressors[0], nullptr);

    Compressor comp = deserializeCompressor(serializedTrainedCompressors[0]);
    testRoundTrip(deltaData_, comp);
}

TEST_F(TestMLSelectorGraph, TestFeatureExtraction)
{
    training::ProcessedMLTrainingSamples trainingSample = extractMLFeatures(
            numericInputs_, compressor_, cctx_, successors_, successorLabels_);

    std::vector<std::string> expectedLabels = { "delta", "tokenize" };

    EXPECT_EQ(trainingSample.labels.size(), 2);
    EXPECT_EQ(trainingSample.labels, expectedLabels);

    EXPECT_EQ(trainingSample.features.size(), 2);
    // featureGen_int should generate 11 features
    EXPECT_EQ(trainingSample.features[0].size(), 11);

    std::vector<std::string> expectedFeatureNames = { "nbElts",
                                                      "eltWidth",
                                                      "cardinality",
                                                      "cardinality_upper",
                                                      "cardinality_lower",
                                                      "range_size",
                                                      "mean",
                                                      "variance",
                                                      "stddev",
                                                      "skewness",
                                                      "kurtosis" };

    EXPECT_EQ(trainingSample.featureNames, expectedFeatureNames);
    std::swap(numericInputs_[0], numericInputs_[1]);
    std::swap(expectedLabels[0], expectedLabels[1]);

    training::ProcessedMLTrainingSamples trainingSample2 = extractMLFeatures(
            numericInputs_, compressor_, cctx_, successors_, successorLabels_);

    EXPECT_EQ(trainingSample2.labels.size(), 2);
    EXPECT_EQ(trainingSample2.labels, expectedLabels);
}

} // namespace
} // namespace openzl::tests
