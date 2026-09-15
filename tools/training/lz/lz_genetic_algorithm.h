// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "openzl/openzl.hpp"

#include "tools/training/lz/lz_compressor.h"
#include "tools/training/utils/genetic_algorithm.h"
#include "tools/training/utils/thread_pool.h"
#include "tools/training/utils/utils.h"

namespace openzl {
namespace training {

/**
 * Tunables for the LZ search. The search space is far smaller than ACE's, so
 * these are smaller than the genetic algorithm's own defaults.
 */
constexpr size_t kLzPopulationSize = 75;
constexpr size_t kLzMaxGenerations = 150;

/// One field of `LzCompressor`, which the search varies as a unit.
struct LzField {
    /// Replaces the field with a random value from its allowed range.
    void (*randomize)(LzCompressor& lz, std::mt19937_64& rng);
    /// Replaces the field with the parent's value of that field.
    void (*inherit)(LzCompressor& lz, const LzCompressor& parent);
};

/**
 * @returns every field the search varies.
 *
 * The graph fields of `LzCompressor::params` are deliberately absent: they are
 * owned by the backend graph genes and are filled in by
 * `LzCompressor::buildParams()`.
 */
poly::span<const LzField> lzFields();

/**
 * @brief A genetic algorithm which tunes a single LZ backend graph.
 *
 * The gene is an `LzCompressor`: the LZ node's parameters together with the
 * backend graph to send each of the LZ node's outputs to. Candidates are scored
 * by benchmarking the retuned backend graph in isolation on the streams that
 * reach it, so the search cost doesn't grow with the size of the surrounding
 * compressor.
 *
 * Construct it, drive it with `step()` until `finished()`, then read the
 * Pareto-optimal tunings out of `solution()`.
 */
class LzGeneticAlgorithm : public GeneticAlgorithm<LzCompressor> {
   public:
    using Base = GeneticAlgorithm<LzCompressor>;

    struct Parameters : public Base::Parameters {
        Parameters()
        {
            populationSize = kLzPopulationSize;
            maxGenerations = kLzMaxGenerations;
        }

        size_t numThreads{ std::thread::hardware_concurrency() / 2 };
        /// Existing dictionaries required to verify decompression.
        std::shared_ptr<const std::string> dictBundleData;
    };

    /**
     * @param makeCompressor Creates the compressor that @p backendGraph belongs
     * to. Candidates are benchmarked in parallel, so it must be safe to call
     * concurrently.
     * @param backendGraph The name of the LZ backend graph to tune.
     * @param inputs The streams which reach @p backendGraph. They must outlive
     * the algorithm.
     */
    LzGeneticAlgorithm(
            std::function<Compressor()> makeCompressor,
            std::string backendGraph,
            poly::span<const MultiInput> inputs,
            const Parameters& params = Parameters());

    const std::string& backendGraph() const
    {
        return backendGraph_;
    }

    std::vector<LzCompressor> initialPopulation() override;

    LzCompressor crossover(
            const LzCompressor& parent1,
            const LzCompressor& parent2) override;

    LzCompressor mutate(const LzCompressor& parent) override;

    std::vector<float> computeFitness(const LzCompressor& gene) override;

    std::vector<std::vector<float>> computeFitness(
            poly::span<const LzCompressor> genes) override;

   private:
    static std::vector<float> computeFitness(
            const LzCompressor& gene,
            const std::function<Compressor()>& makeCompressor,
            const std::string& backendGraph,
            poly::span<const MultiInput> inputs,
            poly::string_view dictBundleData);

    poly::string_view dictBundleData() const
    {
        return dictBundleData_ ? poly::string_view(*dictBundleData_)
                               : poly::string_view{};
    }

    std::function<Compressor()> makeCompressor_;
    std::string backendGraph_;
    poly::span<const MultiInput> inputs_;
    std::shared_ptr<const std::string> dictBundleData_;
    ThreadPool threadPool_;
    std::unordered_map<uint64_t, std::pair<size_t, std::vector<float>>>
            cachedFitness_;
};

} // namespace training
} // namespace openzl
