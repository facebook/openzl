// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <string_view>
#include "openzl/cpp/Compressor.hpp"
#include "tools/training/train_params.h"
#include "tools/training/utils/serialized_compressor_internal.h"
#include "tools/training/utils/utils.h"

namespace openzl::training {

extern const std::string ACE_GRAPH_NAME;

class ACETrainer {
   public:
    /**
     * This class trains a compressor that contains any number of ACE graphs.
     * It can be run on untrained ACE compressor or re-run on an already-trained
     * ACE compressor.
     */
    ACETrainer() {}

    /**
     * Runs ACE training and returns the results.
     *
     * @param inputs The inputs to train on
     * @param serializedCompressorInput The serialized compressor input
     * @param trainParams The training parameters to use
     *
     * @return A vector shared pointer to the trained serialized compressors.
     *         If `trainParams.paretoFront` is false, the vector will contain a
     *         single compressor. Otherwise, it will contain a Pareto frontier
     *         of compressors.
     */
    std::vector<SerializedCompressorInternal> train(
            const std::vector<MultiInput>& inputs,
            std::string_view serializedCompressorInput,
            const TrainParams& trainParams);

    /**
     * @returns the ACE checkpoint of the latest training run. The ace
     * checkpoint is a serialization of the ace states containing the pareto
     * frontier produced during training.
     */
    const std::optional<SerializedCompressorInternal>& aceCheckpoint()
    {
        return checkPoint_;
    }

   private:
    std::optional<SerializedCompressorInternal> checkPoint_;
};

} // namespace openzl::training
