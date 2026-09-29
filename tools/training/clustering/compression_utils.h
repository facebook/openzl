// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "openzl/codecs/zl_clustering.h"
#include "openzl/common/logging.h"
#include "openzl/cpp/Input.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_reflection.h"
#include "tools/training/clustering/clustering_config.h"
#include "tools/training/clustering/utils.h"
#include "tools/training/utils/thread_pool.h"
#include "tools/training/utils/utils.h"

namespace openzl::training {
class SizeTimePair {
   public:
    size_t compressedSize;
    size_t time;

    bool operator<(const SizeTimePair& cost) const
    {
        return compressedSize < cost.compressedSize;
    }

    bool operator==(const SizeTimePair& cost) const
    {
        return compressedSize == cost.compressedSize;
    }

    SizeTimePair operator+(const SizeTimePair& cost) const
    {
        return SizeTimePair{ compressedSize + cost.compressedSize,
                             time + cost.time };
    }
};

struct ClusterInfo {
    size_t successorIdx{ 0 };
    size_t clusteringCodecIdx{ 0 };
    SizeTimePair cost{ std::numeric_limits<size_t>::max(), 0 };
};

class CompressionUtils {
   public:
    CompressionUtils(
            const ZL_Compressor* compressor,
            const std::vector<MultiInput>& samples,
            const std::vector<ZL_GraphID>& successors,
            const std::vector<ZL_NodeID>& clusteringCodecs,
            ThreadPool& threadPool)
            : compressor_(compressor),
              samples_(std::move(samples)),
              successors_(successors),
              clusteringCodecs_(clusteringCodecs),
              threadPool_(threadPool)
    {
        // Process clustering codecs into a map suitable to be used
        size_t clusteringCodecIdx = 0;
        for (auto& codec : clusteringCodecs_) {
            auto numInputs =
                    ZL_Compressor_Node_getNumInputs(compressor_, codec);
            auto isVariableInput =
                    ZL_Compressor_Node_isVariableInput(compressor_, codec);
            if (numInputs != 1 || !isVariableInput) {
                ZL_LOG(V,
                       "Invalid clustering code: clustering codecs must have exactly one input which is variable");
                continue;
            }
            ZL_Type type = ZL_Compressor_Node_getInput0Type(compressor_, codec);
            typeToClusteringCodecIdxsMap_[type].emplace_back(
                    clusteringCodecIdx++);
        }
        for (auto type : kInputTypes_) {
            if (typeToClusteringCodecIdxsMap_[type].size() == 0) {
                throw Exception(
                        "A clustering codec must be provided for each possible input type.");
            }
        }
    }

    ColumnMetadata aggregateInputMetadata() const;

    ClusterInfo getBestClusterInfo(
            const std::unordered_set<int>& tags,
            ZL_Type type,
            size_t eltWidth,
            const ColumnMetadata& metadata) const;

    // TODO: Improve this function and its caller to no longer require extra
    // synchronizing thread
    std::future<SizeTimePair> tryCompress(
            const ClusteringConfig& config,
            const std::function<bool(ColumnInfo)>& filter) const;

    std::future<SizeTimePair> tryCompress(const ClusteringConfig& config) const
    {
        std::function<bool(ColumnInfo)> filter = [](ColumnInfo) {
            return true;
        };
        return tryCompress(config, filter);
    }

    std::map<ZL_Type, std::vector<size_t>> getTypeToClusteringCodecIdxsMap()
            const
    {
        return typeToClusteringCodecIdxsMap_;
    }

   private:
    SizeTimePair compressSample(
            const ClusteringConfig& config,
            const std::function<bool(ColumnInfo)>& filter,
            const MultiInput& sample) const;

    const ZL_Compressor* compressor_;
    const std::vector<MultiInput> samples_;
    const std::vector<ZL_GraphID> successors_;
    const std::vector<ZL_NodeID> clusteringCodecs_;
    static constexpr int compressBoundFactor_ = 2;
    ThreadPool& threadPool_;

    std::map<ZL_Type, std::vector<size_t>> typeToClusteringCodecIdxsMap_;
    const std::vector<ZL_Type> kInputTypes_ = {
        ZL_Type_serial,
        ZL_Type_struct,
        ZL_Type_numeric,
        ZL_Type_string,
    };
};
} // namespace openzl::training
