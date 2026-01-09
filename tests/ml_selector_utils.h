// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include <vector>
#include "openzl/compress/selectors/ml/gbt.h"

namespace openzl {
namespace tests {

class SampleGBTModel {
   protected:
    GBTModel gbtModel_{};
    std::vector<GBTPredictor_Node> nodes_;
    GBTPredictor_Tree tree_{};
    GBTPredictor_Forest forest_{};
    GBTPredictor predictor_{};
    std::vector<Label> featureLabels_;

   public:
    virtual GBTModel getModel() = 0;
    virtual ~SampleGBTModel()   = default;
};

class SampleBinaryGBTModel : public SampleGBTModel {
   public:
    SampleBinaryGBTModel();
    GBTModel getModel() override;
};

class SampleCyclicGBTModel : public SampleGBTModel {
   public:
    SampleCyclicGBTModel();
    GBTModel getModel() override;
};

// Generates data that have constant deltas between consecutive values
std::vector<uint64_t> generateDeltaData(
        size_t nbElts      = 10000,
        uint64_t baseValue = 0,
        uint64_t delta     = 0x12345);

} // namespace tests
} // namespace openzl
