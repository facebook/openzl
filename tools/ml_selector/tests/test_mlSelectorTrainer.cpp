// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "openzl/cpp/Compressor.hpp"
#include "tests/ml_selector_utils.h"
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
        deltaData_ = generateDeltaData();
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        training::MultiInput multiInput1;
        multiInput1.add(
                Input::refNumeric(deltaData_.data(), deltaData_.size()));
        inputs.push_back(std::move(multiInput1));

        SetUpCompressorWithStaticSuccessors();
    }

    void SetUpCompressorWithStaticSuccessors()
    {
        ZL_GraphID fieldlz =
                ZL_Compressor_registerFieldLZGraph(compressor_.get());
        ZL_GraphID range_pack = ZL_Compressor_registerStaticGraph_fromNode(
                compressor_.get(), ZL_NODE_RANGE_PACK, &fieldlz, 1);
        ZL_GraphID zstd = ZL_GRAPH_ZSTD;

        ZL_GraphID successors[3] = { fieldlz, range_pack, zstd };

        ZL_RESULT_OF(ZL_GraphID)
        mlSelectorResult = MLSelector_registerGraphWithEmptyGBTModel(
                compressor_.get(), successors, 3);
        ASSERT_FALSE(ZL_RES_isError(mlSelectorResult));

        ZL_GraphID mlSelectorGraphId = ZL_RES_value(mlSelectorResult);

        // Wrap with serial-to-numeric conversion so the graph accepts serial
        // input
        ZL_NodeID conversionNode = ZL_Node_convertSerialToNumLE(bitWidth);
        ZL_GraphID _             = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_.get(), conversionNode, mlSelectorGraphId);
    }

   protected:
    Compressor compressor_;
    size_t bitWidth = 64;

    std::vector<uint64_t> deltaData_;
    std::vector<training::MultiInput> inputs;
};

TEST_F(TestMLSelectorGraph, TestRoundTrip)
{
    openzl::training::TrainParams trainParams = {
        .compressorGenFunc =
                [](std::string_view serialized) {
                    auto compressor = std::make_unique<openzl::Compressor>();
                    compressor->deserialize(serialized);
                    return compressor;
                },
    };

    auto serializedTrainedCompressors =
            openzl::training::train(inputs, compressor_, trainParams);

    ASSERT_FALSE(serializedTrainedCompressors.empty());
    ASSERT_NE(serializedTrainedCompressors[0], nullptr);
}

} // namespace
} // namespace openzl::tests
