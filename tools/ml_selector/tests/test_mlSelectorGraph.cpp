// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "openzl/common/a1cbor_helpers.h"
#include "openzl/compress/selectors/ml/gbt.h"
#include "openzl/compress/selectors/ml/ml_selector_graph.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "tests/datagen/DataGen.h"
#include "tests/ml_selector_utils.h"
#include "tests/utils.h"

namespace openzl::tests {
namespace {

class TestMLSelectorGraph : public testing::Test {
   public:
    void SetUp() override
    {
        deltaData_ = generateDeltaData();
        auto dg    = openzl::tests::datagen::DataGen();
        randomData_ =
                dg.template randVector<uint64_t>("randVec", 0, 10000, 10000);

        cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        gbtModel_         = sampleModel_.getModel();
        mlSelectorConfig_ = { .model = ZL_GBT, .runtimeConfig = &gbtModel_ };
    }

    ZL_GraphID getSelectorGraphWithSelectedSuccessor(
            size_t selectedSuccessor,
            openzl::Compressor& compressor,
            ZL_MLSelectorConfig mlSelectorConfig)
    {
        auto graph = ZL_MLSelector_registerGraph(
                compressor.get(),
                &mlSelectorConfig,
                successors_.data(),
                successors_.size());
        EXPECT_ZS_VALID(graph);

        return ZL_RES_value(graph);
    }

    void testSelection(
            const std::vector<uint64_t>& input,
            ZL_GraphID gid,
            ZL_GraphID sgid,
            Compressor& compressor)
    {
        auto compressBound = ZL_compressBound(input.size() * sizeof(input[0]));

        // Compress using selected successor
        std::string cBuffer = std::string(compressBound, '\0');
        compress(compressor, cBuffer, input, sgid);

        // Compress using ml selector graph
        std::string scBuffer = std::string(compressBound, '\0');
        compress(compressor, scBuffer, input, gid);

        // Check that the ml selector graph selects the correct successor
        EXPECT_EQ(cBuffer, scBuffer);
    }

    void testRoundTrip(
            const std::vector<uint64_t>& input,
            ZL_GraphID sgid,
            Compressor& compressor)
    {
        // Compress using ml selector graph
        std::string cBuffer = std::string(
                ZL_compressBound(input.size() * sizeof(input[0])), '\0');
        auto compressedSize = compress(compressor, cBuffer, input, sgid);
        cBuffer.resize(compressedSize);

        // Decompress and verify that the result is the same as the input
        const auto decompressedOutput = dctx_.decompressOne(cBuffer);
        const uint64_t* output = (const uint64_t*)decompressedOutput.ptr();
        std::vector<uint64_t> outputVec(
                output, output + decompressedOutput.numElts());

        EXPECT_EQ(outputVec, input);
    }

    size_t compress(
            Compressor& compressor,
            std::string& dst,
            const std::vector<uint64_t>& input,
            ZL_GraphID sgid)
    {
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor.selectStartingGraph(sgid);
        cctx_.refCompressor(compressor);
        auto s_input = Input::refNumeric(input.data(), input.size());
        return cctx_.compressOne(dst, s_input);
    }

    void assert_model_equal(GBTModel m1, GBTModel m2)
    {
        ASSERT_EQ(m1.predictor->numForests, m2.predictor->numForests);
        ASSERT_EQ(m1.nbSuccessors, m2.nbSuccessors);
        ASSERT_EQ(m1.nbFeatures, m2.nbFeatures);

        for (size_t ind = 0; ind < m1.nbFeatures; ind++) {
            ASSERT_STREQ(m1.featureLabels[ind], m2.featureLabels[ind]);
        }
        assert_pred_equal(*m1.predictor, *m2.predictor);
    }

   private:
    void assert_tree_equal(GBTPredictor_Tree t1, GBTPredictor_Tree t2)
    {
        ASSERT_EQ(t1.numNodes, t2.numNodes);
        for (size_t ind = 0; ind < t1.numNodes; ind++) {
            ASSERT_FLOAT_EQ(t1.nodes[ind].value, t2.nodes[ind].value);
            ASSERT_EQ(t1.nodes[ind].leftChildIdx, t2.nodes[ind].leftChildIdx);
            ASSERT_EQ(t1.nodes[ind].rightChildIdx, t2.nodes[ind].rightChildIdx);
            ASSERT_EQ(
                    t1.nodes[ind].missingChildIdx,
                    t2.nodes[ind].missingChildIdx);
            ASSERT_EQ(t1.nodes[ind].featureIdx, t2.nodes[ind].featureIdx);
        }
    }

    void assert_forest_equal(GBTPredictor_Forest f1, GBTPredictor_Forest f2)
    {
        ASSERT_EQ(f1.numTrees, f2.numTrees);
        for (size_t ind = 0; ind < f1.numTrees; ind++) {
            assert_tree_equal(f1.trees[ind], f2.trees[ind]);
        }
    }
    void assert_pred_equal(GBTPredictor pred1, GBTPredictor pred2)
    {
        ASSERT_EQ(pred1.numForests, pred2.numForests);
        for (size_t ind = 0; ind < pred1.numForests; ind++) {
            assert_forest_equal(pred1.forests[ind], pred2.forests[ind]);
        }
    }

   protected:
    Compressor compressor_;
    DCtx dctx_;
    CCtx cctx_;

    std::vector<uint64_t> deltaData_;
    std::vector<uint64_t> randomData_;
    SampleBinaryGBTModel sampleModel_;
    GBTModel gbtModel_{};
    ZL_MLSelectorConfig mlSelectorConfig_ = {};
    std::vector<ZL_GraphID> successors_   = { ZL_GRAPH_COMPRESS_GENERIC,
                                              ZL_GRAPH_FIELD_LZ };
};

TEST_F(TestMLSelectorGraph, TestMLSelectorGraphRoundtrip)
{
    auto compressMLSelectorGraph = getSelectorGraphWithSelectedSuccessor(
            1, compressor_, mlSelectorConfig_);

    testRoundTrip(deltaData_, compressMLSelectorGraph, compressor_);
}

TEST_F(TestMLSelectorGraph, TestMLSelectorGraphSelection)
{
    // ML Selector should select class 2 because skewness < 0.001 (since data
    // have same delta)
    auto compressDeltaMLSelectorGraph = getSelectorGraphWithSelectedSuccessor(
            1, compressor_, mlSelectorConfig_);

    testSelection(
            deltaData_,
            compressDeltaMLSelectorGraph,
            successors_[1],
            compressor_);

    // ML Selector should select class 1 because skewness > 0.001 (since data is
    // random and not forced to have same delta)
    auto compressMLSelectorGraph = getSelectorGraphWithSelectedSuccessor(
            0, compressor_, mlSelectorConfig_);

    testSelection(
            randomData_, compressMLSelectorGraph, successors_[0], compressor_);
}

TEST_F(TestMLSelectorGraph, TestMLSelectorConfigSerializable)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor_.get());

    // Serialize config
    ZL_MLSelectorConfig config = { .model         = ZL_GBT,
                                   .runtimeConfig = &gbtModel_ };
    Arena* arena               = ALLOC_HeapArena_create();
    A1C_Arena a1cArena         = A1C_Arena_wrap(arena);

    ZL_RESULT_OF(ZL_SerializedMLConfig)
    serializedResult = MLSelector_serializeMLSelectorConfig(
            ZL_ERR_CTX_PTR, &config, &a1cArena);
    EXPECT_ZS_VALID(serializedResult);

    auto serializedConfig = ZL_RES_value(serializedResult);

    // Deserialize config
    auto result = MLSelector_deserializeMLSelectorConfig(
            ZL_ERR_CTX_PTR,
            serializedConfig.data,
            serializedConfig.size,
            &a1cArena);
    EXPECT_ZS_VALID(result);

    // Check that the deserialized config contains the same predictor labels
    const GBTModel* originalModel = (GBTModel*)(config.runtimeConfig);
    const GBTModel* deserializedModel =
            (GBTModel*)(ZL_RES_value(result).runtimeConfig);

    EXPECT_TRUE(deserializedModel != NULL);

    assert_model_equal(*originalModel, *deserializedModel);
    ALLOC_Arena_freeArena(arena);
}

TEST_F(TestMLSelectorGraph, TestMLSelectorGraphSerializable)
{
    Compressor compressor;
    size_t selectedSuccessor     = 1;
    auto compressMLSelectorGraph = getSelectorGraphWithSelectedSuccessor(
            selectedSuccessor, compressor, mlSelectorConfig_);

    // Make sure selection works before serialization
    testSelection(
            deltaData_,
            compressMLSelectorGraph,
            successors_[selectedSuccessor],
            compressor);

    std::string serialCompress = compressor.serialize();

    Compressor deserializedCompressor;

    deserializedCompressor.deserialize(serialCompress);

    // Make sure selection works after deserialization
    testSelection(
            deltaData_,
            compressMLSelectorGraph,
            successors_[selectedSuccessor],
            deserializedCompressor);
    // Make sure round trip works after deserialization
    testRoundTrip(deltaData_, compressMLSelectorGraph, deserializedCompressor);
}

TEST_F(TestMLSelectorGraph, TestEmptyConfig)
{
    // Run with empty config to make sure there is no crash
    ZL_MLSelectorConfig emptyConfig = {};
    Compressor compressor;
    auto graph = ZL_MLSelector_registerGraph(
            compressor.get(),
            &emptyConfig,
            successors_.data(),
            successors_.size());
    EXPECT_ZS_ERROR(graph);
}

TEST_F(TestMLSelectorGraph, TestInvalidGBTModel)
{
    SampleCyclicGBTModel sampleCyclicModel;
    GBTModel cyclicGTModel_           = sampleCyclicModel.getModel();
    ZL_MLSelectorConfig invalidConfig = {
        .model         = ZL_GBT,
        .runtimeConfig = &cyclicGTModel_,
    };

    Compressor compressor;
    auto graph = ZL_MLSelector_registerGraph(
            compressor.get(),
            &invalidConfig,
            successors_.data(),
            successors_.size());
    EXPECT_ZS_ERROR(graph);
}

TEST_F(TestMLSelectorGraph, TestInvalidFeatureGenerator)
{
    gbtModel_.featureGenerator = [](const ZL_Input* inputStream,
                                    VECTOR(LabeledFeature)
                                            * features) -> ZL_Report {
        (void)inputStream;
        (void)features;
        return ZL_returnSuccess();
    };

    Compressor compressor;
    auto graph = ZL_MLSelector_registerGraph(
            compressor.get(),
            &mlSelectorConfig_,
            successors_.data(),
            successors_.size());
    EXPECT_ZS_ERROR(graph);
}
} // namespace
} // namespace openzl::tests
