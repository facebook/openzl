// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tools/training/ace/ace_compressor.h"
#include "tools/training/ace/ace_mutate.h"
#include "tools/training/ace/ace_sampling.h"

namespace openzl {
namespace training {

/// Crosses over two ACECompressors to produce a new ACECompressor that inherits
/// a comination of their traits.
class ACECrossover {
   public:
    ACECrossover(std::mt19937_64& rng, Type inputType, uint32_t formatVersion)
            : rng_(rng), inputType_(inputType), formatVersion_(formatVersion)
    {
    }

    ACECompressor operator()(
            const ACECompressor& parent1,
            const ACECompressor& parent2)
    {
        // Shuffle the parents
        std::uniform_int_distribution<int> dist(0, 1);
        if (dist(rng_)) {
            return crossover(parent1, parent2);
        } else {
            return crossover(parent2, parent1);
        }
    }

   private:
    ACECompressor crossover(
            const ACECompressor& donor,
            const ACECompressor& recipient)
    {
        for (size_t attempt = 0; attempt < 5; ++attempt) {
            auto donorComponent = getRandomComponent(donor);
            auto child          = replaceRandomComponent(
                    recipient, std::move(donorComponent));
            if (child.has_value()) {
                return std::move(*child);
            }
        }
        return ACEMutate(rng_, inputType_, formatVersion_)(recipient);
    }

    ACECompressor getRandomComponent(const ACECompressor& donor)
    {
        ACEReservoirSampler<const ACECompressor> sampler(rng_);
        donor.forEachComponent(inputType_, [&](const auto& component, auto) {
            sampler.update(component);
        });
        return *sampler.get();
    }

    poly::optional<ACECompressor> replaceRandomComponent(
            const ACECompressor& recipient,
            ACECompressor&& donorComponent)
    {
        ACEReservoirSampler<const ACECompressor> sampler(rng_);
        recipient.forEachComponent(
                inputType_, [&](const auto& component, auto inputType) {
                    if (donorComponent.acceptsInputType(inputType)) {
                        sampler.update(component);
                    }
                });
        if (sampler.get()) {
            return recipient.replace(
                    inputType_,
                    [&donorComponent, target = sampler.get()](
                            const auto& component,
                            auto inputType,
                            size_t) mutable -> poly::optional<ACECompressor> {
                        if (&component == target) {
                            assert(donorComponent.acceptsInputType(inputType));
                            return std::move(donorComponent);
                        } else {
                            return poly::nullopt;
                        }
                    });
        } else {
            return poly::nullopt;
        }
    }

    std::mt19937_64& rng_;
    Type inputType_;
    uint32_t formatVersion_;
};

} // namespace training
} // namespace openzl
