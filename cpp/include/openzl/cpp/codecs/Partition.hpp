// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_partition.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/codecs/Node.hpp"

#include <vector>

namespace openzl {
namespace nodes {

class Partition : public Node {
   public:
    static constexpr NodeID node = ZL_NODE_PARTITION;

    static constexpr NodeMetadata<1, 2> metadata = {
        .inputs           = { InputMetadata{ .type = Type::Numeric } },
        .singletonOutputs = { OutputMetadata{ .type = Type::Numeric,
                                              .name = "partitions" },
                              OutputMetadata{ .type = Type::Serial,
                                              .name = "offsets" } },
        .description =
                "Partition unsigned integer values into buckets and extra bits using configurable partition boundaries",
    };

    /// Construct with a preset ID (1-4).
    explicit Partition(ZL_PartitionParamsPreset preset) : preset_(preset) {}

    explicit Partition(
            uint64_t startValue,
            std::vector<uint64_t> partitionSizes)
            : preset_(ZL_PartitionParamsPreset_custom)
    {
        copyParam_.push_back(startValue);
        copyParam_.insert(
                copyParam_.end(), partitionSizes.begin(), partitionSizes.end());
    }

    NodeID baseNode() const override
    {
        return node;
    }

    poly::optional<NodeParameters> parameters() const override
    {
        LocalParams params;
        if (preset_ == ZL_PartitionParamsPreset_custom) {
            params.addCopyParam(
                    ZL_PARTITION_CUSTOM_PID,
                    copyParam_.data(),
                    copyParam_.size() * sizeof(uint64_t));
        } else {
            params.addIntParam(ZL_PARTITION_PRESET_PID, preset_);
        }
        return NodeParameters{ .localParams = std::move(params) };
    }

    GraphID operator()(
            Compressor& compressor,
            GraphID partitions,
            GraphID offsets = ZL_GRAPH_STORE) const
    {
        return buildGraph(
                compressor,
                std::initializer_list<GraphID>{ partitions, offsets });
    }

    ~Partition() override = default;

   private:
    ZL_PartitionParamsPreset preset_;
    std::vector<uint64_t> copyParam_;
};

} // namespace nodes
} // namespace openzl
