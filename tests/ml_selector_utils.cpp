// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "tests/ml_selector_utils.h"
#include <algorithm>
#include <random>
#include "openzl/compress/selectors/ml/features.h"

namespace openzl {
namespace tests {

SampleBinaryGBTModel::SampleBinaryGBTModel()
{
    nodes_ = { /* If eltWidth > 2 evaluate skewness */
               { .featureIdx      = 0,
                 .value           = 2,
                 .leftChildIdx    = 1,
                 .rightChildIdx   = 2,
                 .missingChildIdx = 2 },
               { .featureIdx      = -1,
                 .value           = -0.1f,
                 .leftChildIdx    = 0,
                 .rightChildIdx   = 0,
                 .missingChildIdx = 0 },
               /* If skewness > 0.001 select class 1
               otherwise select class 2
               Binary classification threshold is 0
               */
               { .featureIdx      = 1,
                 .value           = 0.001f,
                 .leftChildIdx    = 3,
                 .rightChildIdx   = 4,
                 .missingChildIdx = 4 },
               { .featureIdx      = -1,
                 .value           = 0.75f,
                 .leftChildIdx    = 0,
                 .rightChildIdx   = 0,
                 .missingChildIdx = 0 },
               { .featureIdx      = -1,
                 .value           = -0.25f,
                 .leftChildIdx    = 0,
                 .rightChildIdx   = 0,
                 .missingChildIdx = 0 }
    };
    tree_      = { .numNodes = nodes_.size(), .nodes = nodes_.data() };
    forest_    = { .numTrees = 1, .trees = &tree_ };
    predictor_ = {
        .numForests = 1,
        .forests    = &forest_,
    };

    // Only take these features from feature generator
    featureLabels_ = {
        Label("eltWidth"),
        Label("skewness"),
    };

    gbtModel_ = {
        .predictor        = &predictor_,
        .featureGenerator = FeatureGen_integer,
        .nbSuccessors     = 2,
        .nbFeatures       = featureLabels_.size(),
        .featureLabels    = featureLabels_.data(),
    };
}

SampleCyclicGBTModel::SampleCyclicGBTModel()
{
    nodes_     = { { .featureIdx      = 0,
                     .value           = 2,
                     .leftChildIdx    = 0, // point to self for cycle
                     .rightChildIdx   = 2,
                     .missingChildIdx = 2 } };
    tree_      = { .numNodes = nodes_.size(), .nodes = nodes_.data() };
    forest_    = { .numTrees = 1, .trees = &tree_ };
    predictor_ = {
        .numForests = 1,
        .forests    = &forest_,
    };

    featureLabels_ = {
        Label("eltWidth"),
    };

    gbtModel_ = {
        .predictor        = &predictor_,
        .featureGenerator = FeatureGen_integer,
        .nbSuccessors     = 2,
        .nbFeatures       = featureLabels_.size(),
        .featureLabels    = featureLabels_.data(),
    };
}

GBTModel SampleBinaryGBTModel::getModel()
{
    return gbtModel_;
}

GBTModel SampleCyclicGBTModel::getModel()
{
    return gbtModel_;
}

std::vector<uint64_t>
generateDeltaData(size_t nbElts, uint64_t baseValue, uint64_t delta)
{
    std::vector<uint64_t> data(nbElts);
    uint64_t value = baseValue;
    for (size_t i = 0; i < nbElts; ++i) {
        data[i] = value;
        value += delta;
    }
    return data;
}

} // namespace tests
} // namespace openzl
