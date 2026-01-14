// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include <cstdint>
#include <functional>
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/poly/Optional.hpp"

namespace openzl::training {

enum ClusteringTrainer {
    Greedy,
    BottomUp,
    FullSplit,
};

struct TrainParams {
    std::function<std::unique_ptr<Compressor>(poly::string_view)>
            compressorGenFunc; /* The function the trainer uses to create the
                                  compressor. Must handle
                                  dependency registration. This function must be
                                  defined. */
    poly::optional<uint32_t> threads;
    poly::optional<ClusteringTrainer> clusteringTrainer;
    poly::optional<size_t> numSamples;
    bool noAceSuccessors{ false };
    bool noClustering{ false };
    poly::optional<size_t> maxTimeSecs;
    poly::optional<size_t> maxFileSizeMb;
    poly::optional<size_t> maxTotalSizeMb;
    bool paretoFrontier{ false };
    bool tuneHyperparams{ false };
};

} // namespace openzl::training
