// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/openzl.hpp"
#include "tools/training/utils/pareto_combination.h"

namespace openzl {
namespace training {
namespace tests {
namespace {

/// Every candidate benchmarks against the same original size, since the
/// candidates for a backend graph all see the same inputs.
constexpr size_t kOriginalSize = 1 << 20;

CompressionResult resultFromFitness(const std::vector<size_t>& fitness)
{
    if (fitness.size() != 3) {
        throw std::runtime_error("Invalid fitness vector size");
    }
    return CompressionResult{
        .originalSize      = kOriginalSize,
        .compressedSize    = fitness[0],
        .compressionTime   = std::chrono::nanoseconds{ fitness[1] },
        .decompressionTime = std::chrono::nanoseconds{ fitness[2] },
    };
}

std::vector<CandidateSelection> getCandidates(
        const std::string& name,
        const std::vector<std::vector<size_t>>& fitness)
{
    std::vector<CandidateSelection> candidates;
    candidates.reserve(fitness.size());
    for (size_t i = 0; i < fitness.size(); ++i) {
        candidates.emplace_back(name, resultFromFitness(fitness[i]), i);
    }
    return candidates;
}

/// @returns The candidate index each selection in @p frontier chose. Requires
/// that every selection chose exactly one candidate of subcompressor @p name.
std::vector<size_t> chosenIndices(
        const std::vector<CandidateSelection>& frontier,
        const std::string& name)
{
    std::vector<size_t> indices;
    indices.reserve(frontier.size());
    for (const auto& selection : frontier) {
        indices.push_back(selection.choices().at(name));
    }
    return indices;
}

bool isPareto(const std::vector<CandidateSelection>& frontier)
{
    for (size_t i = 0; i < frontier.size(); ++i) {
        for (size_t j = 0; j < frontier.size(); ++j) {
            if (frontier[i].dominates(frontier[j])) {
                return false;
            }
        }
    }
    return true;
}

/// A mutation which swaps a backend graph out for a fixed standard graph.
class ReplaceWithGraph : public BackendGraphMutation {
   public:
    ReplaceWithGraph(std::string name, GraphID replacement)
            : BackendGraphMutation(std::move(name)), replacement_(replacement)
    {
    }

    poly::optional<GraphID> newGraph(Compressor&) const override
    {
        return replacement_;
    }

   private:
    GraphID replacement_;
};

/// A mutation which swaps a backend graph out for zstd at a fixed level.
class UseZstdLevel : public BackendGraphMutation {
   public:
    UseZstdLevel(std::string name, int level)
            : BackendGraphMutation(std::move(name)), level_(level)
    {
    }

    poly::optional<GraphID> newGraph(Compressor& compressor) const override
    {
        return ZL_Compressor_registerZstdGraph_withLevel(
                compressor.get(), level_);
    }

   private:
    int level_;
};

} // namespace

class ParetoCombinationTest : public testing::Test {
   public:
    void SetUp() override
    {
        candidates_.clear();
        gen_ = std::mt19937(0);
    }

    void setUpRandomCandidates(
            size_t numSubcompressors,
            size_t numCandidatesPerSubcompressor)
    {
        candidates_.reserve(numSubcompressors);
        for (size_t i = 0; i < numSubcompressors; ++i) {
            std::vector<std::vector<size_t>> fitness;
            fitness.reserve(numCandidatesPerSubcompressor);
            for (size_t j = 0; j < numCandidatesPerSubcompressor; ++j) {
                fitness.push_back(getRandomFitness());
            }
            candidates_.push_back(getCandidates(std::to_string(i), fitness));
        }
    }

    std::vector<size_t> getRandomFitness()
    {
        std::vector<size_t> fitness(3);
        for (size_t i = 0; i < 3; ++i) {
            fitness[i] = distribution_(gen_);
        }
        return fitness;
    }

   protected:
    std::vector<std::vector<CandidateSelection>> candidates_;
    std::mt19937 gen_;
    std::uniform_int_distribution<unsigned> distribution_{ 1, 10000 };
};

TEST_F(ParetoCombinationTest, ParetoFrontierFiltersCorrectly)
{
    ThreadPool threadPool(1);
    std::vector<std::vector<size_t>> fitness;
    fitness.reserve(1000);
    for (size_t j = 0; j < 1000; ++j) {
        fitness.push_back(getRandomFitness());
    }
    auto candidates = getCandidates("0", fitness);
    auto frontier   = filterParetoFrontier(std::move(candidates), threadPool);
    EXPECT_FALSE(frontier.empty());
    EXPECT_TRUE(isPareto(frontier));
}

TEST_F(ParetoCombinationTest, ParetoFrontierFiltersIdenticallyOnManyThreads)
{
    // Filtering fans out over the thread pool, so it must produce the same
    // frontier however many threads it runs on.
    std::vector<std::vector<size_t>> fitness;
    fitness.reserve(1000);
    for (size_t j = 0; j < 1000; ++j) {
        fitness.push_back(getRandomFitness());
    }

    ThreadPool oneThread(1);
    ThreadPool manyThreads(8);
    auto serial = filterParetoFrontier(getCandidates("0", fitness), oneThread);
    auto parallel =
            filterParetoFrontier(getCandidates("0", fitness), manyThreads);

    EXPECT_FALSE(serial.empty());
    EXPECT_EQ(chosenIndices(serial, "0"), chosenIndices(parallel, "0"));
}

TEST_F(ParetoCombinationTest, CandidatePruningRespectsLimit)
{
    ThreadPool threadPool(1);
    std::vector<std::vector<size_t>> fitness;
    fitness.reserve(1000);
    for (size_t j = 0; j < 1000; ++j) {
        fitness.push_back(getRandomFitness());
    }
    auto candidates = getCandidates("0", fitness);
    auto frontier   = filterParetoFrontier(std::move(candidates), threadPool);
    EXPECT_TRUE(isPareto(frontier));
    // The filtered pareto frontier can be small so the only guarantee is
    // returning <= numCandidates
    auto pruned = pruneCandidates(std::move(frontier), 10);
    EXPECT_LE(pruned.size(), 10);
}

TEST_F(ParetoCombinationTest, CandidatePruningWithDuplicateFitness)
{
    // Candidates repeat values within each dimension, so pruning must
    // distinguish candidates that tie on crowding distance.
    const std::vector<std::vector<size_t>> fitness = {
        { 20, 15, 15 }, { 20, 13, 17 }, { 25, 15, 10 }, { 25, 13, 12 },
        { 35, 10, 10 }, { 35, 25, 5 },  { 45, 12, 6 },  { 45, 10, 8 },
        { 50, 8, 5 },   { 55, 12, 4 },
    };
    auto candidates = getCandidates("0", fitness);
    ASSERT_TRUE(isPareto(candidates));

    auto pruned = pruneCandidates(std::move(candidates), 6);
    EXPECT_EQ(pruned.size(), 6);
    EXPECT_TRUE(isPareto(pruned));
}

TEST_F(ParetoCombinationTest, PruningKeepsEverythingUnderTheLimit)
{
    // Too few candidates for a meaningful crowding distance
    auto candidates = getCandidates("0", { { 20, 15, 15 }, { 25, 13, 17 } });
    EXPECT_EQ(pruneCandidates(std::move(candidates), 6).size(), 2);
}

TEST_F(ParetoCombinationTest, PruningNoCandidatesIsEmpty)
{
    EXPECT_TRUE(pruneCandidates({}, 6).empty());
}

TEST_F(ParetoCombinationTest, PruningWithAConstantDimension)
{
    // Every candidate decompresses in the same time, so that dimension has no
    // range to normalize the crowding distance by
    std::vector<std::vector<size_t>> fitness;
    for (size_t i = 0; i < 20; ++i) {
        fitness.push_back({ 100 + i, 200 - i, 50 });
    }
    auto candidates = getCandidates("0", fitness);
    EXPECT_EQ(pruneCandidates(std::move(candidates), 6).size(), 6);
}

TEST_F(ParetoCombinationTest, PruningWithEveryDimensionConstant)
{
    // Identical candidates, so no dimension has a range and no crowding
    // distance can be computed at all
    const std::vector<std::vector<size_t>> fitness(20, { 100, 200, 50 });
    auto candidates = getCandidates("0", fitness);
    EXPECT_EQ(pruneCandidates(std::move(candidates), 6).size(), 6);
}

TEST_F(ParetoCombinationTest, CombinationSizeIsLimited)
{
    ThreadPool threadPool(1);
    setUpRandomCandidates(10, 40);
    auto frontier = combineCandidates(candidates_, threadPool);
    EXPECT_LE(frontier.size(), 1000);
}

TEST_F(ParetoCombinationTest, ProducesParetoOptimalCombination)
{
    ThreadPool threadPool(1);
    setUpRandomCandidates(10, 40);
    auto frontier = combineCandidates(candidates_, threadPool);
    EXPECT_FALSE(frontier.empty());
    EXPECT_TRUE(isPareto(frontier));
    // Every combination must choose exactly one candidate per subcompressor
    for (const auto& candidate : frontier) {
        EXPECT_EQ(candidate.choices().size(), candidates_.size());
    }
}

TEST_F(ParetoCombinationTest, SubcompressorWithNoCandidatesKeepsOtherChoices)
{
    ThreadPool threadPool(1);
    candidates_.push_back(
            getCandidates("0", { { 20, 15, 15 }, { 25, 13, 17 } }));
    candidates_.push_back({});
    candidates_.push_back(
            getCandidates("2", { { 30, 10, 10 }, { 35, 8, 12 } }));

    // The subcompressor with no candidates is skipped, rather than emptying the
    // frontier and discarding the choices already made for subcompressor "0"
    auto frontier = combineCandidates(candidates_, threadPool);
    EXPECT_FALSE(frontier.empty());
    for (const auto& candidate : frontier) {
        EXPECT_EQ(candidate.choices().size(), 2);
        EXPECT_EQ(candidate.choices().count("0"), 1);
        EXPECT_EQ(candidate.choices().count("2"), 1);
    }
}

class MergedParetoFrontierTest : public testing::Test {
   public:
    static constexpr const char* kBackendGraph = "test.backend";

    Compressor makeCompressor() const
    {
        Compressor compressor;
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        GraphParameters params;
        // Anchor names ('!' prefix) are not suffixed with a unique ID, so the
        // graph can be looked up by kBackendGraph
        params.name = std::string("!") + kBackendGraph;
        compressor.selectStartingGraph(compressor.parameterizeGraph(
                ZL_GRAPH_COMPRESS_GENERIC, params));
        return compressor;
    }

    MergedParetoFrontier::BackendGraphMutationsMap makeCandidates(
            const std::vector<int>& zstdLevels) const
    {
        std::vector<std::unique_ptr<BackendGraphMutation>> mutations;
        mutations.reserve(zstdLevels.size());
        for (const auto level : zstdLevels) {
            mutations.push_back(
                    std::make_unique<UseZstdLevel>(kBackendGraph, level));
        }
        MergedParetoFrontier::BackendGraphMutationsMap candidates;
        candidates.emplace(kBackendGraph, std::move(mutations));
        return candidates;
    }

    /// Data that compresses well, but not so well that every zstd level
    /// produces the same output.
    static std::string compressibleData()
    {
        std::string data;
        std::mt19937 gen(0);
        std::uniform_int_distribution<unsigned> distribution(0, 15);
        for (size_t i = 0; i < 20000; ++i) {
            data += char('a' + distribution(gen));
        }
        return data;
    }

    std::vector<MultiInput> makeInputs(const std::string& data) const
    {
        MultiInput multiInput;
        multiInput.add(Input::refSerial(data));
        std::vector<MultiInput> inputs;
        inputs.push_back(std::move(multiInput));
        return inputs;
    }

    /// @returns the size of @p data compressed by @p serialized
    size_t compressedSize(
            const SerializedCompressorInternal& serialized,
            const std::string& data) const
    {
        Compressor compressor;
        compressor.deserialize(*serialized);
        CCtx cctx;
        cctx.refCompressor(compressor);
        auto input = Input::refSerial(data);
        return cctx.compressOne(input).size();
    }
};

TEST_F(MergedParetoFrontierTest, KeepsOnlyParetoOptimalCandidates)
{
    const auto data = compressibleData();
    auto inputs     = makeInputs(data);

    MergedParetoFrontier frontier(
            [this] { return makeCompressor(); },
            makeCandidates({ 1, 19 }),
            inputs,
            uint32_t(1));

    // A higher zstd level is smaller but slower, so at most both survive
    ASSERT_FALSE(frontier.selections().empty());
    ASSERT_LE(frontier.selections().size(), 2u);

    auto compressors = frontier.paretoFrontier();
    ASSERT_EQ(compressors.size(), frontier.selections().size());

    // Every selection is a working zstd compressor, and they are sorted
    // smallest-first
    const auto best = compressedSize(compressors[0], data);
    EXPECT_LT(best, data.size() * 3 / 4);
    for (const auto& compressor : compressors) {
        EXPECT_LE(best, compressedSize(compressor, data));
    }

    auto mutations = frontier.mutationsAt(0);
    ASSERT_EQ(mutations.size(), 1u);
    EXPECT_EQ(mutations.at(kBackendGraph)->graphName(), kBackendGraph);
}

TEST_F(MergedParetoFrontierTest, BenchmarksCandidatesOnMultipleThreads)
{
    const auto data = compressibleData();
    auto inputs     = makeInputs(data);

    // Candidates are benchmarked in parallel, which builds a compressor per
    // candidate concurrently
    MergedParetoFrontier frontier(
            [this] { return makeCompressor(); },
            makeCandidates({ 1, 3, 6, 9, 12, 15, 19 }),
            inputs,
            uint32_t(4));

    ASSERT_FALSE(frontier.selections().empty());
    auto compressors = frontier.paretoFrontier();
    ASSERT_EQ(compressors.size(), frontier.selections().size());
    for (const auto& compressor : compressors) {
        EXPECT_LT(compressedSize(compressor, data), data.size() * 3 / 4);
    }
}

TEST_F(MergedParetoFrontierTest, BenchmarksTheReplacementGraph)
{
    const auto data = compressibleData();
    auto inputs     = makeInputs(data);

    std::vector<std::unique_ptr<BackendGraphMutation>> mutations;
    mutations.push_back(
            std::make_unique<ReplaceWithGraph>(kBackendGraph, ZL_GRAPH_ZSTD));
    mutations.push_back(
            std::make_unique<ReplaceWithGraph>(kBackendGraph, ZL_GRAPH_STORE));
    MergedParetoFrontier::BackendGraphMutationsMap candidates;
    candidates.emplace(kBackendGraph, std::move(mutations));

    MergedParetoFrontier frontier(
            [this] { return makeCompressor(); },
            std::move(candidates),
            inputs,
            uint32_t(1));

    // Each candidate must be measured as the graph it installs, not as the
    // graph it replaces: storing and compressing cannot look alike.
    ASSERT_EQ(frontier.selections().size(), 2);
    EXPECT_LT(
            frontier.selections()[0].result().compressedSize,
            data.size() * 3 / 4);
    EXPECT_GE(frontier.selections()[1].result().compressedSize, data.size());
}

TEST_F(MergedParetoFrontierTest, UnmutatedWhenThereAreNoCandidates)
{
    const auto data = compressibleData();
    auto inputs     = makeInputs(data);

    MergedParetoFrontier frontier(
            [this] { return makeCompressor(); }, {}, inputs, uint32_t(1));

    ASSERT_EQ(frontier.selections().size(), 1u);
    EXPECT_TRUE(frontier.selections()[0].choices().empty());

    auto compressors = frontier.paretoFrontier();
    ASSERT_EQ(compressors.size(), 1u);
    EXPECT_EQ(
            compressedSize(compressors[0], data),
            compressedSize(
                    SerializedCompressorInternal(makeCompressor().serialize()),
                    data));
}

} // namespace tests
} // namespace training
} // namespace openzl
