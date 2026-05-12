// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <chrono>
#include <stdexcept>

#include "tools/logger/Logger.h"
#include "tools/ml_selector/ml_selector_trainer.h"
#include "tools/training/ace/ace.h"
#include "tools/training/clustering/clustering_graph_trainer.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/train.h"
#include "tools/training/utils/serialized_compressor_internal.h"

using namespace openzl::training::graph_mutation;
using namespace openzl::tools::logger;

namespace openzl::training {

std::vector<TrainedCandidate> train(
        const std::vector<MultiInput>& inputs,
        Compressor& compressor,
        const TrainParams& trainParams)
{
    auto startTime = std::chrono::steady_clock::now();
    std::vector<SerializedCompressorInternal> serializedTrainedCompressors;
    if (!trainParams.compressorGenFunc) {
        throw Exception("Compressor generator function is not set.");
    }

    if (graph_mutation::hasTargetGraph(compressor, CLUSTERING_GRAPH_NAME)) {
        serializedTrainedCompressors.clear();
        serializedTrainedCompressors.push_back(
                trainClusteringGraph(inputs, compressor, trainParams));
        auto newCompressor =
                trainParams.compressorGenFunc(*serializedTrainedCompressors[0]);
        compressor = std::move(*newCompressor);
    }

    if (graph_mutation::hasTargetGraph(compressor, ACE_GRAPH_NAME)) {
        // TODO: The ace trainer supports checkpointing now but we need to
        // add flags to utilize it
        ACETrainer aceTrainer;
        serializedTrainedCompressors =
                aceTrainer.train(inputs, compressor.serialize(), trainParams);
    }

    if (graph_mutation::hasTargetGraph(compressor, ML_SELECTOR_GRAPH_NAME)) {
        serializedTrainedCompressors.clear();
        serializedTrainedCompressors.push_back(
                trainMLSelectorGraph(inputs, compressor, trainParams));
    }

    if (serializedTrainedCompressors.empty()) {
        throw Exception("No trainable graph found in compressor.");
    }

    auto endTime = std::chrono::steady_clock::now();
    Logger::log_c(
            INFO,
            "Trained %d compressors in %lf minutes (wall time).",
            serializedTrainedCompressors.size(),
            std::chrono::duration<double, std::ratio<60>>(endTime - startTime)
                    .count());
    // TODO pretty print just the graphs (exclude params etc)
    Logger::log(
            VERBOSE3,
            "Smallest trained graph:",
            std::string(
                    Compressor::convertSerializedToJson(
                            *serializedTrainedCompressors[0]))
                    .c_str());

    std::vector<TrainedCandidate> retval;
    retval.reserve(serializedTrainedCompressors.size());
    for (auto& trainedCompressor : serializedTrainedCompressors) {
        retval.emplace_back(
                TrainedCandidate{ .serializedCompressor =
                                          std::string(*trainedCompressor) });
    }
    return retval;
}
} // namespace openzl::training
