// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "openzl/cpp/Compressor.hpp"
#include "tools/training/train_params.h"
#include "tools/training/utils/utils.h"

namespace openzl::training {
extern const std::string ML_SELECTOR_GRAPH_NAME;

/**
 * This function trains a ML selector graph by:
 * - Extracting features and compression speed/size from the input data
 * - Training a XGBoost model using extracted data
 * - Parsing XGBoost model dump into GBTModel
 * - Register GBTModel into compressor and returns serialized compressor
 * Note:This assumes inputs are integers.
 *
 * @param inputs The inputs to train on
 * @param compressor The compressor to use
 * @param successorGraphs The graph ids of successors the ml selector can choose
 * @param successorLabels The name of the successors
 *
 * @returns a trained serialized compressor
 **/
std::shared_ptr<const std::string_view> trainMLSelectorGraph(
        const std::vector<MultiInput>& inputs,
        Compressor& compressor,
        const TrainParams& trainParams);

} // namespace openzl::training
