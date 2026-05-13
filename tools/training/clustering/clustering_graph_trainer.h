// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/cpp/Compressor.hpp"
#include "tools/training/train_params.h"
#include "tools/training/utils/serialized_compressor_internal.h"
#include "tools/training/utils/utils.h"

namespace openzl::training {

extern const std::string CLUSTERING_GRAPH_NAME;

/*
 * This function trains a clustering graph. It takes in a
 * directory of samples, a graph name, and a training algorithm, and returns a
 * trained serialized compressor.
 *
 * @param inputs the file paths to the samples.
 * @param untrainedGraphName The name of the untrained graph.
 * @param compressor The compressor to train.
 * @param trainParams The training parameters controlling sample sizes, the
 * number of threads and the training algorithm.
 *
 * @return The trained serialized compressor
 */

SerializedCompressorInternal trainClusteringGraph(
        const std::vector<MultiInput>& inputs,
        Compressor& compressor,
        const TrainParams& trainParams);

} // namespace openzl::training
