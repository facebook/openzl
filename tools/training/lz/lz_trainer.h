// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <chrono>

#include "openzl/openzl.hpp"

#include "openzl/cpp/codecs/Lz.hpp"
#include "tools/training/train_params.h"
#include "tools/training/utils/pareto_combination.h"
#include "tools/training/utils/serialized_compressor_internal.h"
#include "tools/training/utils/utils.h"

namespace openzl {
namespace training {

extern const std::string LZ_GRAPH_NAME;

/**
 * Tunes every LZ backend graph in a compressor and produces the Pareto-optimal
 * set of compressors that results.
 *
 * Only parameterized instances of the LZ graph can be tuned. The standard LZ
 * graph is shared by every compressor, so it is left alone.
 *
 * @note: This isn't just a method that returns the serialized compressors
 * because in the future we may want to expose the `BackendGraphMutation`s from
 * the `MergedParetoFrontier`.
 */
class LzTrainer {
   public:
    /**
     * Call this method first. It trains @p serializedCompressorInput on @p
     * inputs with @p trainingParams.
     *
     * @throws a FormatVersionUnsupportedError if th format version is too old.
     */
    void train(
            poly::span<const MultiInput> inputs,
            poly::string_view serializedCompressorInput,
            const TrainParams& trainParams);

    /**
     * Called after training, it produces a
     */
    std::vector<SerializedCompressorInternal> paretoFrontier() const;

   private:
    poly::optional<MergedParetoFrontier> paretoFrontier_;
};

/*******************************
 * Helpers exposed for testing *
 *******************************/

/**
 * @returns An even share of the time left before @p deadline for each of the
 * @p graphsRemaining LZ graphs still to be trained, or null if there is no
 * deadline.
 *
 * Recomputing the share before each graph hands the time that the graphs before
 * it didn't need to the graphs after it. Never less than a second, so that the
 * last graphs are still tuned once the deadline has passed.
 */
poly::optional<std::chrono::seconds> timeShare(
        const poly::optional<std::chrono::steady_clock::time_point>& deadline,
        size_t graphsRemaining);

} // namespace training
} // namespace openzl
