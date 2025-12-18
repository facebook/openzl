// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <folly/dynamic.h>
#include <span>
#include <string>
#include <vector>
#include "openzl/compress/selectors/ml/gbt.h"

namespace zstrong::ml::gbt_predictor {

// A Gradient Boosted Trees (GBT) predictor that can be used
// to evaluate models trained by XGBoost or LightGBM.
// The model represents a list of forests, where each forest is
// a collection of trees.
//
// Prediction is done by evaluating all trees and getting a sum of the
// values per forest. The forest with the highest value is chosen as the
// predicted class.
// Binary-classificaiton is a special case in which only one forest is needed
// and if its combined value is compared to 0 to decide on the class.
//
// The predictor is initialized from a JSON string / a folly:dynamic object. The
// schema for the JSON is an array of arrays of trees. Each tree is encoded
// according to the shema described for Tree.

using CoreGBTPredictor = ::GBTPredictor;
class GBTPredictor {
   public:
    GBTPredictor() = delete;
    explicit GBTPredictor(folly::dynamic const& model);
    explicit GBTPredictor(std::string_view model);

    size_t predict(const std::vector<float>& features) const;
    size_t getNumClasses() const;
    std::shared_ptr<::GBTPredictor> getCorePredictor() const;

   private:
    void initFromJson(folly::dynamic const& model);
    size_t initTreeFromJson(
            folly::dynamic const& json,
            std::unique_ptr<GBTPredictor_Node[]>& nodes);

    std::vector<std::unique_ptr<GBTPredictor_Tree[]>> core_trees_;
    std::vector<std::unique_ptr<GBTPredictor_Node[]>> core_nodes_;
    std::unique_ptr<GBTPredictor_Forest[]> core_forests_;
    std::shared_ptr<CoreGBTPredictor> core_predictor_;
};
} // namespace zstrong::ml::gbt_predictor
