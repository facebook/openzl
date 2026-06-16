// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <chrono>
#include <vector>

#include "tools/logger/Logger.h"
#include "tools/ml_selector/ml_selector_trainer.h"
#include "tools/training/ace/ace.h"
#include "tools/training/clustering/clustering_graph_trainer.h"
#include "tools/training/dict/base_dict_trainer.h"
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
        auto newCompressor = trainParams.compressorGenFunc(
                *serializedTrainedCompressors[0], "");
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

    // Dict training: for each serialized candidate, deserialize, train
    // dicts, re-serialize with bundleID + dictIDs in CBOR.
    std::vector<TrainedCandidate> dictTrainedCandidates;
    if (!serializedTrainedCompressors.empty()) {
        for (auto& sc : serializedTrainedCompressors) {
            if (trainParams.dictTraining) {
                auto candidateCompressor =
                        trainParams.compressorGenFunc(*sc, "");
                dictTrainedCandidates.push_back(trainDictsForCandidate(
                        inputs, *candidateCompressor, trainParams));
            } else {
                TrainedCandidate candidate;
                candidate.serializedCompressor = std::string(*sc);
                dictTrainedCandidates.push_back(std::move(candidate));
            }
        }
    } else if (trainParams.dictTraining) {
        // No clustering/ACE/ML graph — run dict training directly on
        // the input compressor if it has dict-requiring nodes.
        auto candidate =
                trainDictsForCandidate(inputs, compressor, trainParams);
        if (!candidate.dicts.empty()) {
            dictTrainedCandidates.push_back(std::move(candidate));
        }
    }

    if (dictTrainedCandidates.empty()) {
        throw Exception("No trainable graph found in compressor.");
    }

    auto endTime = std::chrono::steady_clock::now();
    Logger::log_c(
            INFO,
            "Trained %zu compressors in %lf minutes (wall time).",
            dictTrainedCandidates.size(),
            std::chrono::duration<double, std::ratio<60>>(endTime - startTime)
                    .count());
    // TODO pretty print just the graphs (exclude params etc)
    Logger::log(
            INFO,
            "Smallest trained graph:",
            std::string(
                    Compressor::convertSerializedToJson(
                            dictTrainedCandidates[0].serializedCompressor))
                    .c_str());

    return dictTrainedCandidates;
}

} // namespace openzl::training
