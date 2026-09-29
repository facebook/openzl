// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tools/training/clustering/clustering_config_builder.h"
#include "tools/training/utils/thread_pool.h"
#include "tools/training/utils/utils.h"

namespace openzl::training {
class Trainer {
   public:
    Trainer(int maxThreads = std::thread::hardware_concurrency(),
            poly::optional<size_t> maxTimeSecs = std::nullopt)
            : threadPool_(maxThreads), maxTimeSecs_(maxTimeSecs)
    {
    }
    virtual ~Trainer() = default;

    virtual ClusteringConfigBuilder getTrainedClusteringConfig(
            const ZL_Compressor* compressor,
            const std::vector<MultiInput>& samples,
            const std::vector<ZL_GraphID>& successors,
            const std::vector<ZL_NodeID>& clusteringCodecs,
            const std::map<std::pair<ZL_Type, size_t>, size_t>&
                    typeToDefaultSuccessorIdxMap) = 0;

   protected:
    ThreadPool threadPool_;
    poly::optional<size_t> maxTimeSecs_;
};
} // namespace openzl::training
