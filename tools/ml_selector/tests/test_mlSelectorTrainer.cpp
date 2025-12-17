// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "tests/datagen/DataGen.h"
#include "tests/ml_selector_utils.h"
#include "tools/ml_selector/ml_features.h"
#include "tools/ml_selector/ml_selector_graph.h"
#include "tools/ml_selector/ml_selector_trainer.h"
#include "tools/training/train.h"
#include "tools/training/train_params.h"
#include "tools/training/utils/utils.h"

namespace openzl::tests {
namespace {

class TestMLSelectorTrainer : public testing::Test {
   public:
    void SetUp() override
    {
        trainedCompressor_.setParameter(
                CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        generateTrainData();
        testData_ = generateTestData();

        trainParams_ = {
            .compressorGenFunc =
                    [](std::string_view serialized) {
                        auto compressor =
                                std::make_unique<openzl::Compressor>();
                        compressor->deserialize(serialized);
                        return compressor;
                    },
        };
    }

    std::vector<GraphID> registerSuccessors(
            Compressor& compressor,
            bool multi = false)
    {
        auto deltaGid = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor.get(), ZL_NODE_DELTA_INT, ZL_GRAPH_ZSTD);

        auto tokenizeGid = ZL_Compressor_registerTokenizeGraph(
                compressor.get(),
                ZL_Type_numeric,
                true,
                deltaGid,
                ZL_GRAPH_ZSTD);

        std::vector<ZL_GraphID> successorGraphs = { deltaGid, tokenizeGid };

        if (multi) {
            successorGraphs.push_back(ZL_GRAPH_ZSTD);
        }
        return successorGraphs;
    }

    std::vector<GraphID> setUpCompressor(
            Compressor& compressor,
            bool multi = false)
    {
        std::vector<ZL_GraphID> successorGraphs =
                registerSuccessors(compressor, multi);

        auto mlSelectorGraphId = MLSelector_registerGraphWithEmptyGBTModel(
                compressor.get(),
                successorGraphs.data(),
                successorGraphs.size());

        EXPECT_TRUE(!ZL_RES_isError(mlSelectorGraphId));

        // Wrap with serial-to-numeric conversion so the graph accepts serial
        // input
        ZL_GraphID staticGraph = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor.get(),
                ZL_NODE_CONVERT_SERIAL_TO_NUM_LE64,
                ZL_RES_value(mlSelectorGraphId));

        // Parameterize so ml selector graph can be updated during training
        ZL_GraphParameters const wrapperDesc = {};

        auto sgid = ZL_Compressor_parameterizeGraph(
                compressor.get(), staticGraph, &wrapperDesc);
        EXPECT_TRUE(!ZL_RES_isError(sgid));
        compressor.selectStartingGraph(ZL_RES_value(sgid));

        return successorGraphs;
    }

    std::vector<std::vector<uint64_t>> generateTestData()
    {
        std::vector<std::vector<uint64_t>> data;
        auto dg = openzl::tests::datagen::DataGen();

        data.push_back(generateDeltaData());
        data.push_back(dg.template randLongVector<uint64_t>(
                "randLongVec", 0, 100, 10000, 10000));

        data.push_back(dg.template randLongVector<uint64_t>(
                "randLongVec", 0, UINT64_MAX, 10000, 10000));
        return data;
    }

    void generateTrainData(int size = 500)
    {
        for (auto i = 0; i < size; ++i) {
            auto dg = openzl::tests::datagen::DataGen();

            deltaData_.push_back(generateDeltaData());

            tokenizeData_.push_back(dg.template randLongVector<uint64_t>(
                    "randLongVec", 0, 100, 10000, 10000));

            zstd_.push_back(dg.template randLongVector<uint64_t>(
                    "randLongVec", 0, UINT64_MAX, 10000, 10000));

            // For binary classification, only add delta and tokenize data
            training::MultiInput binaryInput1;
            binaryInput1.add(
                    Input::refSerial(
                            deltaData_.back().data(),
                            deltaData_.back().size() * sizeof(uint64_t)));
            binaryInputs_.push_back(std::move(binaryInput1));

            training::MultiInput binaryInput2;
            binaryInput2.add(
                    Input::refSerial(
                            tokenizeData_.back().data(),
                            tokenizeData_.back().size() * sizeof(uint64_t)));
            binaryInputs_.push_back(std::move(binaryInput2));

            // For multiclass classification, add delta, tokenize, and zstd data
            training::MultiInput multiInput1;
            multiInput1.add(
                    Input::refSerial(
                            deltaData_.back().data(),
                            deltaData_.back().size() * sizeof(uint64_t)));
            multiInputs_.push_back(std::move(multiInput1));

            training::MultiInput multiInput2;
            multiInput2.add(
                    Input::refSerial(
                            tokenizeData_.back().data(),
                            tokenizeData_.back().size() * sizeof(uint64_t)));
            multiInputs_.push_back(std::move(multiInput2));

            training::MultiInput multiInput3;
            multiInput3.add(
                    Input::refSerial(
                            zstd_.back().data(),
                            zstd_.back().size() * sizeof(uint64_t)));
            multiInputs_.push_back(std::move(multiInput3));
        }
    }
    size_t compress(
            Compressor& compressor,
            std::string& dst,
            const std::vector<uint64_t>& input)
    {
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        cctx_.refCompressor(compressor);
        // Use refSerial since ./zli assumes all data is serial
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
        // Since we compress with refSerial, we use decompressSerial
        const auto decompressedOutput = dctx_.decompressSerial(cBuffer);

        const uint64_t* output =
                reinterpret_cast<const uint64_t*>(decompressedOutput.data());
        std::vector<uint64_t> outputVec(
                output, output + decompressedOutput.size() / sizeof(uint64_t));

        EXPECT_EQ(outputVec, input);
    }

    void testSelection(
            const std::vector<uint64_t>& input,
            Compressor& compressor,
            Compressor& mlCompressor)
    {
        auto compressBound = ZL_compressBound(input.size() * sizeof(uint64_t));

        // Compress using selected successor
        std::string cBuffer = std::string(compressBound, '\0');
        auto compressedSize = compress(compressor, cBuffer, input);
        cBuffer.resize(compressedSize);

        // Compress using ml selector graph
        std::string scBuffer  = std::string(compressBound, '\0');
        auto mlCompressedSize = compress(mlCompressor, scBuffer, input);
        scBuffer.resize(mlCompressedSize);

        // Check that the ml selector graph selects the correct successor
        EXPECT_EQ(cBuffer, scBuffer);
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

   protected:
    Compressor compressor_;
    Compressor trainedCompressor_;

    std::vector<std::vector<uint64_t>> deltaData_;
    std::vector<std::vector<uint64_t>> tokenizeData_;
    std::vector<std::vector<uint64_t>> zstd_;

    std::vector<std::vector<uint64_t>> testData_;

    std::vector<training::MultiInput> binaryInputs_;
    std::vector<training::MultiInput> multiInputs_;

    std::vector<ZL_GraphID> multiSuccessors_;
    std::vector<std::string> multiSuccessorLabels_ = { "delta",
                                                       "tokenize",
                                                       "zstd" };

    std::vector<ZL_GraphID> binarySuccessors_;
    std::vector<std::string> binarySuccessorsLabels_ = { "delta", "tokenize" };

    openzl::training::TrainParams trainParams_;

    CCtx cctx_;
    DCtx dctx_;
};

TEST_F(TestMLSelectorTrainer, MultiClassSelection)
{
    multiSuccessors_ = registerSuccessors(compressor_);
    setUpCompressor(trainedCompressor_, true);
    auto serializedCompressor = openzl::training::trainMLSelectorGraph(
            multiInputs_, trainedCompressor_, trainParams_);

    // Deserialize the trained compressor
    Compressor mlCompressor = deserializeCompressor(serializedCompressor);

    // Check that the ml selector graph selects the correct successor
    for (size_t i = 0; i < multiSuccessors_.size(); i++) {
        auto gid = compressor_.buildStaticGraph(
                ZL_NODE_CONVERT_SERIAL_TO_NUM_LE64, { multiSuccessors_[i] });
        compressor_.selectStartingGraph(gid);
        testSelection(testData_[i], compressor_, mlCompressor);
    }
}

TEST_F(TestMLSelectorTrainer, BinaryClassSelection)
{
    binarySuccessors_ = registerSuccessors(compressor_);
    setUpCompressor(trainedCompressor_);

    auto serializedCompressor = openzl::training::trainMLSelectorGraph(
            binaryInputs_, trainedCompressor_, trainParams_);

    // Deserialize the trained compressor
    Compressor mlCompressor = deserializeCompressor(serializedCompressor);

    // Check that the ml selector graph selects the correct successor
    for (size_t i = 0; i < binarySuccessors_.size(); i++) {
        auto gid = compressor_.buildStaticGraph(
                ZL_NODE_CONVERT_SERIAL_TO_NUM_LE64, { binarySuccessors_[i] });
        compressor_.selectStartingGraph(gid);
        testSelection(testData_[i], compressor_, mlCompressor);
    }
}

TEST_F(TestMLSelectorTrainer, BinaryClassRoundTrip)
{
    binarySuccessors_ = setUpCompressor(trainedCompressor_, true);

    auto serializedBinaryClassCompressors =
            openzl::training::trainMLSelectorGraph(
                    binaryInputs_, trainedCompressor_, trainParams_);

    Compressor mlCompressor =
            deserializeCompressor(serializedBinaryClassCompressors);

    // Make sure deserialized data is same as original data
    for (size_t i = 0; i < testData_.size(); i++) {
        testRoundTrip(testData_[i], mlCompressor);
    }
}

TEST_F(TestMLSelectorTrainer, MultiClassRoundTrip)
{
    multiSuccessors_ = setUpCompressor(trainedCompressor_, true);

    auto serializedMultiClassCompressors =
            openzl::training::trainMLSelectorGraph(
                    multiInputs_, trainedCompressor_, trainParams_);

    Compressor mlCompressor =
            deserializeCompressor(serializedMultiClassCompressors);

    // Make sure deserialized data is same as original data
    for (size_t i = 0; i < testData_.size(); i++) {
        testRoundTrip(testData_[i], mlCompressor);
    }
}

TEST_F(TestMLSelectorTrainer, TrainRoundTrip)
{
    multiSuccessors_ = setUpCompressor(trainedCompressor_, true);

    auto serializedCompressor = openzl::training::train(
            multiInputs_, trainedCompressor_, trainParams_);

    Compressor mlCompressor =
            deserializeCompressor(serializedCompressor.front());

    testRoundTrip(testData_.front(), mlCompressor);
}

TEST_F(TestMLSelectorTrainer, TestFeatureExtraction)
{
    // Use refNumeric here since extactMLFeatures expects numeric data
    training::MultiInput multiInput1;
    multiInput1.add(
            Input::refNumeric(
                    deltaData_.front().data(), deltaData_.front().size()));

    training::MultiInput multiInput2;
    multiInput2.add(
            Input::refNumeric(
                    tokenizeData_.front().data(),
                    tokenizeData_.front().size()));

    std::vector<training::MultiInput> featureInputs = { multiInput1,
                                                        multiInput2 };

    binarySuccessors_ = setUpCompressor(trainedCompressor_);
    training::ProcessedMLTrainingSamples trainingSample = extractMLFeatures(
            featureInputs,
            trainedCompressor_,
            cctx_,
            binarySuccessors_,
            binarySuccessorsLabels_);

    EXPECT_EQ(trainingSample.labels[0], "delta");
    EXPECT_EQ(trainingSample.labels[1], "tokenize");

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

    // Swap to verify that we are getting correct labels
    std::swap(featureInputs[0], featureInputs[1]);

    training::ProcessedMLTrainingSamples trainingSample2 = extractMLFeatures(
            featureInputs,
            trainedCompressor_,
            cctx_,
            binarySuccessors_,
            binarySuccessorsLabels_);

    EXPECT_EQ(trainingSample2.labels[0], "tokenize");
    EXPECT_EQ(trainingSample2.labels[1], "delta");
}

} // namespace
} // namespace openzl::tests
