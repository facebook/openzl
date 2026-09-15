// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/lz/lz_genetic_algorithm.h"

#include <algorithm>
#include <array>
#include <future>
#include <limits>
#include <random>

#include "openzl/codecs/zl_lz.h"
#include "tools/training/ace/ace_sampling.h"

namespace openzl {
namespace training {
namespace {

/// Probability that a mutated field is reset to its default rather than being
/// given a random value.
constexpr double kUnsetProbability = 0.25;

/// Compression levels below this are just another spelling of a higher
/// `acceleration`, which is searched separately, so the bottom of the
/// parameter's range is not worth searching.
constexpr int kMinCompressionLevel = -8;
/// `acceleration` is allowed up to 10000, far past the point where skipping
/// more of the input can pay for the matches it misses.
constexpr int kMaxAcceleration = 32;
/// Neither entropy gain parameter has a documented range. These cover the
/// defaults (0.5% of the input + 50 bytes, and 1%) with room on either side.
constexpr int kMaxMinGainForEntropyBytesLog = 16;
constexpr int kMaxMinGainForEntropyPct      = 50;

bool randomBool(std::mt19937_64& rng)
{
    return std::uniform_int_distribution<int>(0, 1)(rng) == 1;
}

bool randomUnset(std::mt19937_64& rng)
{
    return std::bernoulli_distribution(kUnsetProbability)(rng);
}

poly::optional<int> randomInt(std::mt19937_64& rng, int min, int max)
{
    if (randomUnset(rng)) {
        return poly::nullopt;
    }
    return std::uniform_int_distribution<int>(min, max)(rng);
}

/// Samples in [0, 2^@p maxLog] on a log scale, so that the small values, where
/// the parameter still changes the outcome, aren't drowned out by the rest.
poly::optional<int> randomLogInt(std::mt19937_64& rng, int maxLog)
{
    if (randomUnset(rng)) {
        return poly::nullopt;
    }
    const int exponent = std::uniform_int_distribution<int>(0, maxLog)(rng);
    return std::uniform_int_distribution<int>(0, 1 << exponent)(rng);
}

poly::optional<ZL_LzStrategy> randomStrategy(std::mt19937_64& rng)
{
    if (randomUnset(rng)) {
        return poly::nullopt;
    }
    return ZL_LzStrategy(
            std::uniform_int_distribution<int>(
                    ZL_LZPARAM_STRATEGY_MIN, ZL_LZPARAM_STRATEGY_MAX)(rng));
}

poly::optional<ACECompressor> randomSuccessor(
        std::mt19937_64& rng,
        poly::span<const ACECompressor> successors)
{
    if (randomUnset(rng)) {
        return poly::nullopt;
    }
    return randomChoice(rng, successors);
}

/// Declares the `LzField` for `LzCompressor::FIELD`, whose random value is the
/// expression @p RANDOM over the in-scope `rng`.
#define LZ_FIELD(FIELD, RANDOM)                                              \
    LzField                                                                  \
    {                                                                        \
        [](LzCompressor& lz, std::mt19937_64& rng) { lz.FIELD = (RANDOM); }, \
                [](LzCompressor& lz, const LzCompressor& parent) {           \
                    lz.FIELD = parent.FIELD;                                 \
                }                                                            \
    }

LzCompressor randomLzCompressor(std::mt19937_64& rng)
{
    LzCompressor lz;
    for (const auto& field : lzFields()) {
        field.randomize(lz, rng);
    }
    return lz;
}

} // namespace

poly::span<const LzField> lzFields()
{
    static const std::array<LzField, 14> fields = {
        LZ_FIELD(
                params.nodeParams.compressionLevel,
                randomInt(
                        rng,
                        kMinCompressionLevel,
                        ZL_LZPARAM_COMPRESSIONLEVEL_MAX)),
        LZ_FIELD(
                params.nodeParams.acceleration,
                randomInt(rng, ZL_LZPARAM_ACCELERATION_MIN, kMaxAcceleration)),
        LZ_FIELD(
                params.nodeParams.windowLog,
                randomInt(
                        rng,
                        ZL_LZPARAM_WINDOWLOG_MIN,
                        ZL_LZPARAM_WINDOWLOG_MAX)),
        LZ_FIELD(params.nodeParams.strategy, randomStrategy(rng)),
        LZ_FIELD(
                params.nodeParams.hashLog1,
                randomInt(
                        rng, ZL_LZPARAM_HASHLOG1_MIN, ZL_LZPARAM_HASHLOG1_MAX)),
        LZ_FIELD(
                params.nodeParams.hashLog2,
                randomInt(
                        rng, ZL_LZPARAM_HASHLOG2_MIN, ZL_LZPARAM_HASHLOG2_MAX)),
        LZ_FIELD(
                params.nodeParams.hashLength,
                randomInt(
                        rng,
                        ZL_LZPARAM_HASHLENGTH_MIN,
                        ZL_LZPARAM_HASHLENGTH_MAX)),
        LZ_FIELD(
                params.minGainForEntropyBytes,
                randomLogInt(rng, kMaxMinGainForEntropyBytesLog)),
        LZ_FIELD(
                params.minGainForEntropyPct,
                randomInt(rng, 0, kMaxMinGainForEntropyPct)),
        LZ_FIELD(literalsGraph, randomSuccessor(rng, literalsCompressors())),
        LZ_FIELD(offsetsGraph, randomSuccessor(rng, offsetsCompressors())),
        LZ_FIELD(
                muxedBytesGraph, randomSuccessor(rng, muxedBytesCompressors())),
        LZ_FIELD(
                overflowLengthsGraph,
                randomSuccessor(rng, overflowLengthsCompressors())),
        LZ_FIELD(
                muxLengthsGraph, randomSuccessor(rng, muxLengthsCompressors())),
    };
    return fields;
}

#undef LZ_FIELD

LzGeneticAlgorithm::LzGeneticAlgorithm(
        std::function<Compressor()> makeCompressor,
        std::string backendGraph,
        poly::span<const MultiInput> inputs,
        const Parameters& params)
        : Base(params),
          makeCompressor_(std::move(makeCompressor)),
          backendGraph_(std::move(backendGraph)),
          inputs_(inputs),
          dictBundleData_(params.dictBundleData),
          threadPool_(std::max<size_t>(1, params.numThreads))
{
    if (!makeCompressor_) {
        throw Exception("No compressor generator provided");
    }
}

std::vector<LzCompressor> LzGeneticAlgorithm::initialPopulation()
{
    std::vector<LzCompressor> population;

    // The untuned graph, which is a strong solution in its own right
    population.emplace_back();

    // Each backend graph on its own, as building blocks for crossover
    auto addSuccessors =
            [&population](
                    poly::optional<ACECompressor> LzCompressor::* successor,
                    poly::span<const ACECompressor> compressors) {
                for (const auto& compressor : compressors) {
                    LzCompressor lz;
                    lz.*successor = compressor;
                    population.push_back(std::move(lz));
                }
            };
    addSuccessors(&LzCompressor::literalsGraph, literalsCompressors());
    addSuccessors(&LzCompressor::offsetsGraph, offsetsCompressors());
    addSuccessors(&LzCompressor::muxedBytesGraph, muxedBytesCompressors());
    addSuccessors(
            &LzCompressor::overflowLengthsGraph, overflowLengthsCompressors());
    addSuccessors(&LzCompressor::muxLengthsGraph, muxLengthsCompressors());

    for (size_t i = 0; i < populationSize(); ++i) {
        population.push_back(randomLzCompressor(rng()));
    }
    return population;
}

LzCompressor LzGeneticAlgorithm::crossover(
        const LzCompressor& parent1,
        const LzCompressor& parent2)
{
    auto child = parent1;
    for (const auto& field : lzFields()) {
        if (randomBool(rng())) {
            field.inherit(child, parent2);
        }
    }
    return child;
}

LzCompressor LzGeneticAlgorithm::mutate(const LzCompressor& parent)
{
    auto child      = parent;
    const auto& all = lzFields();
    const auto field =
            std::uniform_int_distribution<size_t>(0, all.size() - 1)(rng());
    all[field].randomize(child, rng());
    return child;
}

/* static */ std::vector<float> LzGeneticAlgorithm::computeFitness(
        const LzCompressor& gene,
        const std::function<Compressor()>& makeCompressor,
        const std::string& backendGraph,
        poly::span<const MultiInput> inputs,
        poly::string_view dictBundleData)
{
    const LzGraphMutation mutation(backendGraph, gene);
    const auto result = mutation.benchmarkBackendGraph(
            makeCompressor, inputs, dictBundleData);
    if (!result.has_value()) {
        return std::vector<float>(3, std::numeric_limits<float>::infinity());
    }
    return result->asFloatVector();
}

std::vector<float> LzGeneticAlgorithm::computeFitness(const LzCompressor& gene)
{
    return computeFitness(
            gene, makeCompressor_, backendGraph_, inputs_, dictBundleData());
}

std::vector<std::vector<float>> LzGeneticAlgorithm::computeFitness(
        poly::span<const LzCompressor> genes)
{
    std::vector<std::future<std::vector<float>>> futures;
    futures.reserve(genes.size());
    for (const auto& gene : genes) {
        // Benchmark timings are noisy, so occasionally re-measure a gene that
        // has been seen before and keep the best measurement of each objective.
        auto cache = cachedFitness_.find(gene.hash());
        if (cache != cachedFitness_.end()) {
            std::uniform_int_distribution<size_t> dist(0, cache->second.first);
            if (dist(rng()) != 0) {
                std::promise<std::vector<float>> promise;
                promise.set_value(cache->second.second);
                futures.emplace_back(promise.get_future());
                continue;
            }
        }
        futures.emplace_back(threadPool_.run([&gene,
                                              &makeCompressor = makeCompressor_,
                                              &backendGraph   = backendGraph_,
                                              inputs          = inputs_,
                                              dictBundle = dictBundleData()] {
            return computeFitness(
                    gene, makeCompressor, backendGraph, inputs, dictBundle);
        }));
    }

    std::vector<std::vector<float>> results;
    results.reserve(genes.size());
    for (size_t i = 0; i < genes.size(); ++i) {
        auto result            = futures[i].get();
        auto [cache, inserted] = cachedFitness_.emplace(
                genes[i].hash(), std::make_pair(1, result));
        if (!inserted) {
            for (size_t j = 0; j < result.size(); ++j) {
                cache->second.second[j] =
                        std::min(cache->second.second[j], result[j]);
            }
            ++cache->second.first;
        }
        results.push_back(std::move(result));
    }
    return results;
}

} // namespace training
} // namespace openzl
