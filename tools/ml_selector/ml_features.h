// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "openzl/compress/selectors/ml/gbt.h" // For LabeledFeature
#include "openzl/cpp/Compressor.hpp"
#include "tools/training/utils/utils.h"

namespace openzl::training {

using TargetsMap =
        std::unordered_map<size_t, std::unordered_map<std::string, float>>;
using FeatureMap = VECTOR(LabeledFeature);

/**
 * Defines type ChoiceFunction, which parses target map containing compression
 * data for each successor and selects the "best" possible successor.
 * @returns  vector containing index of "best" possible  successors. Note that
 * index is cast as float due to XGBoost requirement.
 */
using ChoiceFunction =
        std::function<std::vector<float>(std::vector<TargetsMap>&)>;

/**
 * Container for processed ML training data with features and labels.
 *
 * numericLabels is the index of the "best" successor based on choice function
 * for each sample. This is cast as a float due to XGBoost requirement.
 * features is a vector containing features for each sample.
 * featureNames is the name of each feature
 * featurePtrNames is the name of each feature as a const char*
 */
struct ProcessedMLTrainingSamples {
    std::vector<float> numericLabels;

    std::vector<std::vector<float>> features;
    std::vector<std::string> featureNames;
    std::vector<const char*> featurePtrNames;
};

/**
 * Make weighted choice function that chooses index successor with best 'score'
 * which is calculated by weight * cSize + (1 - weight) * cTime.
 */
ChoiceFunction makeWeightedChoiceFunc(float weight);

/**
 * Default choice function that chooses index successor with minimum compression
 * size as "best".
 */
std::vector<float> minSizeChoiceFunc(std::vector<TargetsMap>& targets);

/**
 * Given data, return processed samples needed to train ml model by:
 *
 * - Extracting features for each sample
 * - Brute force test each successor and storing compression time/size
 * - For each sample, select the "best" successor using @param choiceFunction
 * - Return best successor and features for each successor
 * NOTE: This assumes inputs are integers.
 *
 * @param inputs The inputs to train on
 * @param cctx The context to use
 * @param compressor The compressor to use
 * @param successorGraphs The graph ids of successors the ml selector can choose
 * @param featureGen featureGenerator to use
 * @param choiceFunction choiceFunction to use
 *
 * @returns ProcessedMLTrainingSamples containing best successor and features
 * for each sample
 */
ProcessedMLTrainingSamples extractMLFeatures(
        const std::vector<MultiInput>& inputs,
        Compressor& compressor,
        CCtx& cctx,
        const std::vector<ZL_GraphID>& successorGraphs,
        FeatureGenerator featureGen   = FeatureGen_integer,
        ChoiceFunction choiceFunction = minSizeChoiceFunc);

} // namespace openzl::training
