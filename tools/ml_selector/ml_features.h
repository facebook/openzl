// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "openzl/compress/selectors/ml/gbt.h" // For LabeledFeature
#include "openzl/cpp/Compressor.hpp"
#include "tools/training/utils/utils.h"

namespace openzl::training {

using TargetsMap =
        std::unordered_map<std::string, std::unordered_map<std::string, float>>;
using FeatureMap = VECTOR(LabeledFeature);

/**
 * Defines type ChoiceFunction, which parses target map containing compression
 * data for each successor and selects the "best" possible successor.
 * @returns vector containing "best" possible successors
 */
using ChoiceFunction =
        std::vector<std::string> (*)(std::vector<TargetsMap>& targets);

/**
 * Container for processed ML training data with features and labels.
 *
 * labels is the "best" successor based on choice function for each sample.
 * numericLabels is numeric representation of labels.
 * labelMap is a mapping from string label to numeric label.
 * features is a vector containing features for each sample.
 * featureNames is the name of each feature
 */
struct ProcessedMLTrainingSamples {
    std::vector<std::string> labels;
    std::vector<float> numericLabels;
    std::unordered_map<std::string, int> labelMap;

    std::vector<std::vector<float>> features;
    std::vector<std::string> featureNames;
};

/**
 * Default choice function that chooses successor with minimum compression size
 * as "best".
 */
std::vector<std::string> minSizeChoiceFunc(std::vector<TargetsMap>& targets);

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
 * @param successorLabels The name of the successors
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
        const std::vector<std::string>& successorLabels,
        FeatureGenerator featureGen   = FeatureGen_integer,
        ChoiceFunction choiceFunction = minSizeChoiceFunc);

} // namespace openzl::training
