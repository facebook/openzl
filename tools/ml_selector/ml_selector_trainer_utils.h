// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "src/openzl/compress/selectors/ml/gbt.h"

namespace openzl::training {
using Hyperparams = std::map<std::string, std::string>;

/**
 * Default XGBoost hyperparameters.
 */
inline const Hyperparams defaultXGBoostHyperParams = {
    { "learning_rate", "0.1" },
    { "min_child_weight", "0.0" },
    { "subsample", "0.7" },
    { "colsample_bynode", "0.8" },
};

/**
 * Owns the memory for a GBTPredictor.
 */
struct GBTPredictorWrapper {
    /// Node arrays for each tree (indexed by tree)
    std::vector<std::unique_ptr<std::vector<GBTPredictor_Node>>> core_nodes_;
    /// Tree arrays for each forest (indexed by forest)
    std::vector<std::unique_ptr<std::vector<GBTPredictor_Tree>>> core_trees_;
    /// All forests in the model
    std::unique_ptr<std::vector<GBTPredictor_Forest>> core_forests_;
    std::unique_ptr<GBTPredictor> core_predictor_;
};

/**
 * Contains both the 2D feature matrices and their flattened versions. The 2D
 * format is for train test split, while the flattened format is required by
 * XGBoost's DMatrix API.
 *
 * Naming follows scikit-learn conventions:
 * - X: Feature matrix
 * - y: Label vector
 */
struct FeatureData {
    std::vector<std::vector<float>> X;
    std::vector<float> y;
    std::vector<float> XFlat;

    FeatureData(
            const std::vector<std::vector<float>>& X_,
            const std::vector<float>& y_)
            : X{ X_ }, y{ y_ }
    {
        for (const auto& x : X) {
            XFlat.insert(XFlat.end(), x.begin(), x.end());
        }
    }
};

/**
 * Holds the train/test split data for XGBoost model training.
 *
 */
struct TestTrainData {
    FeatureData train;
    FeatureData test;
};

/**
 * Splits feature and label data into training and test sets. Used for
 * evaluating model performance during XGBoost boosting rounds.
 *
 * @param features     Feature vectors to split
 * @param labels       Corresponding labels to split
 * @param testSize     Fraction of data to reserve for testing
 * @param shuffle      Whether to shuffle (in place) data before splitting
 * @param randomState  Seed for shuffling
 *
 * @return TrainTestData containing XTrain, XTest, yTrain, and yTest splits
 *
 * @throws Exception if features and labels have different sizes
 */
TestTrainData trainTestSplit(
        std::vector<std::vector<float>>& features,
        std::vector<float>& labels,
        float testSize           = 0.2,
        bool shuffle             = true,
        unsigned int randomState = 40);

/**
 * Trains an XGBoost gradient boosted tree model and converts it to the
 * GBTPredictor format for inference.
 *
 * @param data          Train/test split data
 * @param num_classes   Number of classification classes
 *
 * @return GBTPredictorWrapper containing the trained model
 */
GBTPredictorWrapper trainXGBoostModel(
        TestTrainData& data,
        size_t num_classes,
        const Hyperparams& hyperparams = defaultXGBoostHyperParams);

} // namespace openzl::training
