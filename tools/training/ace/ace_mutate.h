// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tools/training/ace/ace_compressor.h"
#include "tools/training/ace/ace_compressors.h"
#include "tools/training/ace/ace_sampling.h"

namespace openzl {
namespace training {

/// Mutates an ACECompressor to produce a new ACECompressor with a single
/// mutation.
class ACEMutate {
   public:
    ACEMutate(
            std::mt19937_64& rng,
            Type inputType,
            uint32_t formatVersion,
            size_t maxDepth = kDefaultMaxDepth)
            : rng_(rng),
              inputType_(inputType),
              formatVersion_(formatVersion),
              maxDepth_(maxDepth)
    {
    }

    ACECompressor operator()(const ACECompressor& parent)
    {
        ACEReservoirSampler<const ACECompressor> sampler(rng_);
        parent.forEachComponent(inputType_, [&](const auto& component, auto) {
            sampler.update(component);
        });

        return parent.replace(
                inputType_,
                [&](const auto& component,
                    auto inputType,
                    size_t depth) -> poly::optional<ACECompressor> {
                    if (&component == sampler.get()) {
                        return replace(component, inputType, depth);
                    } else {
                        return poly::nullopt;
                    }
                });
    }

   private:
    ACECompressor
    replace(const ACECompressor& component, Type inputType, size_t depth)
    {
        std::uniform_int_distribution<int> dist(0, 3);
        auto choice = dist(rng_);
        if (choice == 0) {
            return randomSimpleCompressor(inputType);
        } else if (choice == 1) {
            return randomCompressor(inputType, depth);
        } else if (choice == 2) {
            return deleteRandomPipelinePrefix(component, inputType, depth);
        } else {
            return addRandomPipeline(component, inputType, depth);
        }
    }

    ACECompressor randomSimpleCompressor(Type inputType)
    {
        return randomChoice(rng_, getPrebuiltCompressors(inputType));
    }

    ACECompressor randomCompressor(Type inputType, size_t depth)
    {
        if (depth > maxDepth_) {
            return buildRandomGraphCompressor(rng_, inputType);
        } else {
            return buildRandomCompressor(
                    rng_, inputType, formatVersion_, maxDepth_ - depth);
        }
    }

    ACECompressor deleteRandomPipelinePrefix(
            const ACECompressor& compressor,
            Type inputType,
            size_t depth)
    {
        const ACECompressor* pipeline = &compressor;
        // Choose the last node to delete
        ACEReservoirSampler<const ACECompressor> sampler(rng_);
        while (pipeline->isNode()) {
            const auto& node = pipeline->asNode();
            if (node.successors.size() != 1) {
                break;
            }
            if (node.successors[0]->acceptsInputType(inputType)) {
                sampler.update(*pipeline);
            }
            pipeline = node.successors[0].get();
        }
        if (sampler.get() == nullptr) {
            return randomCompressor(inputType, depth);
        }
        auto replacement = *sampler.get()->asNode().successors[0];
        assert(replacement.acceptsInputType(inputType));
        return replacement;
    }

    ACECompressor addRandomPipeline(
            const ACECompressor& compressor,
            Type inputType,
            size_t depth)
    {
        if (depth >= maxDepth_) {
            return randomSimpleCompressor(inputType);
        }
        ACEReservoirSampler<const ACENode> sampler(rng_);
        // Bind to a named local: getNodesComptabileWith returns a vector by
        // value, and the sampler stores pointers into it that are dereferenced
        // after the loop.
        const auto compatibleNodes =
                getNodesComptabileWith(inputType, formatVersion_);
        for (const auto& node : compatibleNodes) {
            if (node.outputTypes.size() == 1
                && compressor.acceptsInputType(node.outputTypes[0])) {
                sampler.update(node);
            }
        }
        if (sampler.get() == nullptr) {
            return randomSimpleCompressor(inputType);
        }
        auto node = *sampler.get();
        return ACECompressor(std::move(node), { compressor });
    }

    std::mt19937_64& rng_;
    Type inputType_;
    uint32_t formatVersion_;
    size_t maxDepth_;
};

} // namespace training
} // namespace openzl
