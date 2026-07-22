// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "openzl/cpp/Compressor.hpp"
#include "tools/training/train.h"

namespace openzl::tests {
namespace {

TEST(TrainTest, ThrowsTypedErrorWithoutTrainableGraph)
{
    const std::vector<training::MultiInput> inputs;
    Compressor compressor;
    compressor.selectStartingGraph(ZL_GRAPH_STORE);
    const training::TrainParams trainParams = {
        .compressorGenFunc =
                [](poly::string_view, poly::string_view) {
                    return std::make_unique<Compressor>();
                },
    };

    // This verifies train() exposes a typed signal for a compressor with no
    // trainable graph; it fails if that condition becomes a generic exception.
    EXPECT_THROW(
            training::train(inputs, compressor, trainParams),
            training::NoTrainableGraphError);
}

} // namespace
} // namespace openzl::tests
