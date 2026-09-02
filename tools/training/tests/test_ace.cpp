// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <array>
#include <chrono>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/codecs/ACE.hpp"
#include "openzl/zl_compressor.h"
#include "tools/training/ace/ace.h"
#include "tools/training/ace/ace_compressor.h"
#include "tools/training/ace/ace_mutate.h"
#include "tools/training/ace/ace_sampling.h"
#include "tools/training/ace/automated_compressor_explorer.h"
#include "tools/training/utils/serialized_compressor_internal.h"

namespace openzl {
namespace training {
namespace tests {
namespace {
void undelta(std::vector<uint64_t>& data)
{
    for (size_t i = 1; i < data.size(); ++i) {
        data[i] += data[i - 1];
    }
}

std::vector<uint64_t> tripleDeltaData()
{
    std::vector<uint64_t> data(1000, 1);
    undelta(data);
    undelta(data);
    undelta(data);
    return data;
}

std::pair<std::string, std::vector<uint32_t>> tripleDeltaStringData()
{
    std::string content;
    std::vector<uint32_t> lengths;
    for (const auto x : tripleDeltaData()) {
        auto s = std::to_string(x);
        content += s;
        lengths.push_back(uint32_t(s.size()));
    }
    return { std::move(content), std::move(lengths) };
}

/// Trains @p numGraphs ACE graphs, one per field of a struct input, each graph
/// seeing @p recordsPerGraph records, under a total budget of @p maxTimeSecs.
/// @returns How long the whole training call took.
std::chrono::milliseconds
timeAceTraining(size_t numGraphs, size_t recordsPerGraph, size_t maxTimeSecs)
{
    std::vector<uint64_t> data(numGraphs * recordsPerGraph);
    std::iota(data.begin(), data.end(), 0);
    undelta(data);

    std::vector<Input> inputsVec;
    inputsVec.push_back(
            Input::refSerial(data.data(), data.size() * sizeof(data[0])));
    std::vector<MultiInput> multiInputs;
    multiInputs.emplace_back(std::move(inputsVec));

    auto compressorGenFunc = [](poly::string_view serialized,
                                poly::string_view bundle = "") {
        auto compressor = std::make_unique<Compressor>();
        compressor->deserialize(serialized, bundle);
        return compressor;
    };

    Compressor compressor;
    std::vector<size_t> fieldSizes(numGraphs, sizeof(uint64_t));
    std::vector<ZL_GraphID> fieldGraphs;
    fieldGraphs.reserve(numGraphs);
    for (size_t i = 0; i < numGraphs; ++i) {
        fieldGraphs.push_back(graphs::ACE()(compressor));
    }
    compressor.selectStartingGraph(ZL_Compressor_registerSplitByStructGraph(
            compressor.get(),
            fieldSizes.data(),
            fieldGraphs.data(),
            fieldSizes.size()));
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

    TrainParams trainParams = {
        .compressorGenFunc = compressorGenFunc,
        .threads           = 1,
        .maxTimeSecs       = maxTimeSecs,
    };

    ACETrainer trainer;
    const auto start = std::chrono::steady_clock::now();
    auto results =
            trainer.train(multiInputs, compressor.serialize(), trainParams);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
    EXPECT_FALSE(results.empty());
    return elapsed;
}
} // namespace

class ACETest : public testing::Test {
   public:
    void SetUp() override
    {
        params                = AutomatedCompressorExplorer::Parameters{};
        params.numThreads     = 4;
        params.populationSize = 50;
        params.maxGenerations = 100;
        params.formatVersion  = ZL_MAX_FORMAT_VERSION;
    }

    ACECompressor runOnInput(poly::span<const Input> input)
    {
        ace = std::make_unique<AutomatedCompressorExplorer>(input, params);
        ace->run();
        auto solutions = ace->solution();
        for (size_t i = 1; i < solutions.size(); ++i) {
            EXPECT_LT(solutions[i - 1].second, solutions[i].second);
        }
        return solutions[0].first;
    }

    ACECompressor runOnInput(const Input& input)
    {
        return runOnInput({ &input, 1 });
    }

    AutomatedCompressorExplorer::Parameters params;
    std::unique_ptr<AutomatedCompressorExplorer> ace;
};

TEST_F(ACETest, FormatVersionMustBeSet)
{
    auto data = tripleDeltaData();
    std::vector<Input> inputs;
    inputs.push_back(
            Input::refSerial(data.data(), data.size() * sizeof(data[0])));

    const AutomatedCompressorExplorer::Parameters defaultParams;
    EXPECT_EQ(defaultParams.formatVersion, 0);

    EXPECT_THROW(
            AutomatedCompressorExplorer defaultAce(inputs, defaultParams),
            Exception);
}

TEST_F(ACETest, CompressesAtAllFormatVersions)
{
    // A compressor trained for a specific, non-default format version must
    // actually compress at that version.

    for (uint32_t version = ZL_MIN_FORMAT_VERSION;
         version < ZL_MAX_FORMAT_VERSION;
         version++) {
        params.formatVersion = version;

        auto data = tripleDeltaData();
        auto input =
                Input::refSerial(data.data(), data.size() * sizeof(data[0]));
        auto solution = runOnInput(input);

        EXPECT_EQ(ace->formatVersion(), version);

        auto result = solution.benchmark(ace->inputs(), version);
        ASSERT_TRUE(result.has_value());
        ASSERT_GT(result->compressedSize, 0u);
    }
}

TEST_F(ACETest, ACEReservoirSampler)
{
    std::mt19937_64 rng(0xdeadbeef);
    for (size_t numSamples = 1; numSamples < 10; ++numSamples) {
        std::vector<size_t> samples(numSamples);
        std::iota(samples.begin(), samples.end(), 0);
        std::vector<size_t> counts(numSamples, 0);
        for (size_t repetitions = 0; repetitions < 100000; ++repetitions) {
            ACEReservoirSampler<const size_t> sampler(rng);
            ASSERT_EQ(sampler.get(), nullptr);
            for (const auto& s : samples) {
                sampler.update(s);
            }
            counts[*sampler.get()]++;
        }
        const size_t expectedCount = 100000 / numSamples;
        for (const auto& c : counts) {
            ASSERT_GE(c, expectedCount - expectedCount / 20);
            ASSERT_LE(c, expectedCount + expectedCount / 20);
        }
    }
}

TEST_F(ACETest, SerializeDeserialize)
{
    auto testRoundTrip = [](const ACECompressor& c) {
        auto serialized = c.serialize();
        ACECompressor rt(serialized);
        EXPECT_EQ(c, rt);
    };
    std::mt19937_64 rng(0xdeadbeef);
    for (auto type :
         { Type::Serial, Type::Struct, Type::Numeric, Type::String }) {
        ACEMutate mutator(rng, type, ZL_MAX_FORMAT_VERSION);
        for (const auto& compressor : getPrebuiltCompressors(type)) {
            testRoundTrip(compressor);
            auto mutated = mutator(compressor);
            testRoundTrip(mutated);
        }
        testRoundTrip(buildRandomGraphCompressor(rng, type));
        testRoundTrip(buildRandomNodeCompressor(
                rng, type, ZL_MAX_FORMAT_VERSION, kDefaultMaxDepth));
        testRoundTrip(buildRandomCompressor(
                rng, type, ZL_MAX_FORMAT_VERSION, kDefaultMaxDepth));
    }
}

TEST_F(ACETest, TripleDeltaNumeric)
{
    auto data     = tripleDeltaData();
    auto input    = Input::refNumeric(poly::span<const uint64_t>(data));
    auto solution = runOnInput(input);
    auto result   = solution.benchmark(ace->inputs(), ZL_MAX_FORMAT_VERSION);
    ASSERT_TRUE(result.has_value());
    ASSERT_LE(result->compressedSize, 90);
}

TEST_F(ACETest, TripleDeltaSerial)
{
    auto data  = tripleDeltaData();
    auto input = Input::refSerial(data.data(), data.size() * sizeof(data[0]));
    auto solution = runOnInput(input);
    auto result   = solution.benchmark(ace->inputs(), ZL_MAX_FORMAT_VERSION);
    ASSERT_TRUE(result.has_value());
    ASSERT_LE(result->compressedSize, 90);
}

TEST_F(ACETest, TripleDeltaStruct)
{
    auto data     = tripleDeltaData();
    auto input    = Input::refStruct(poly::span<const uint64_t>(data));
    auto solution = runOnInput(input);
    auto result   = solution.benchmark(ace->inputs(), ZL_MAX_FORMAT_VERSION);
    ASSERT_TRUE(result.has_value());
    ASSERT_LE(result->compressedSize, 90);
}

TEST_F(ACETest, TripleDeltaString)
{
    auto [content, lengths] = tripleDeltaStringData();
    auto input              = Input::refString(content, lengths);
    auto solution           = runOnInput(input);
    auto result = solution.benchmark(ace->inputs(), ZL_MAX_FORMAT_VERSION);
    ASSERT_TRUE(result.has_value());
    ASSERT_LE(result->compressedSize, 110);
}

TEST_F(ACETest, savePopulation)
{
    auto data  = tripleDeltaData();
    auto input = Input::refSerial(data.data(), data.size() * sizeof(data[0]));
    auto solution = runOnInput(input);
    auto result   = solution.benchmark(ace->inputs(), ZL_MAX_FORMAT_VERSION);
    ASSERT_TRUE(result.has_value());
    ASSERT_LE(result->compressedSize, 90);
    auto snapshot = ace->savePopulation();

    // Build a new AutomatedCompressorExplorer
    AutomatedCompressorExplorer ace2({ &input, 1 }, params);
    ASSERT_TRUE(ace2.solution().empty());
    ace2.extendPopulation(ace2.initialPopulation());
    ASSERT_FALSE(ace2.solution().empty());
    // Initial population doesn't have a good solution
    {
        auto solution2 = ace2.solution()[0].first;
        auto result2 =
                solution2.benchmark(ace->inputs(), ZL_MAX_FORMAT_VERSION);
        ASSERT_TRUE(result2.has_value());
        ASSERT_GT(result2->compressedSize, 90);
        ASSERT_NE(solution, solution2);
    }

    // Loading the snapshot gives good solution
    ace2.loadPopulation(snapshot);
    {
        auto solution2 = ace2.solution()[0].first;
        auto result2 =
                solution2.benchmark(ace->inputs(), ZL_MAX_FORMAT_VERSION);
        ASSERT_TRUE(result2.has_value());
        ASSERT_LE(result2->compressedSize, 90);
    }
    // NOTE: The exact smallest solution may not be preserved due to benchmark
    // instability, since it might not be Pareto-optimal in the new benchmark.
}

TEST_F(ACETest, maxTimeWorks)
{
    auto data  = tripleDeltaData();
    auto input = Input::refSerial(data.data(), data.size() * sizeof(data[0]));
    params.maxGenerations = 1 << 30;
    params.maxTime        = std::chrono::seconds(1);
    auto start            = std::chrono::steady_clock::now();
    auto solution         = runOnInput(input);
    auto stop             = std::chrono::steady_clock::now();
    auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(stop - start);
    ASSERT_GE(elapsed, std::chrono::seconds(1));
    ASSERT_LT(
            elapsed,
            std::chrono::seconds(30)); // 30s is a very loose upper bound for
                                       // the time it should take
}

TEST(ACETrainerTest, NoSaveAceStateProducesSmallerCompressor)
{
    // Triple delta pattern compresses well with ACE
    auto data  = tripleDeltaData();
    auto input = Input::refSerial(data.data(), data.size() * sizeof(data[0]));
    std::vector<Input> inputsVec;
    inputsVec.push_back(std::move(input));
    std::vector<MultiInput> multiInputs;
    multiInputs.emplace_back(std::move(inputsVec));

    auto compressorGenFunc = [](poly::string_view serialized,
                                poly::string_view bundle = "") {
        auto compressor = std::make_unique<Compressor>();
        compressor->deserialize(serialized, bundle);
        return compressor;
    };

    // Train once with saveAceState = true
    ACETrainer trainer;
    Compressor compressor;
    compressor.selectStartingGraph(graphs::ACE()(compressor));
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    TrainParams trainParams = {
        .compressorGenFunc = compressorGenFunc,
        .threads           = 1,
        .maxTimeSecs       = 5,
        .saveAceState      = true,
    };
    auto resultsWithState =
            trainer.train(multiInputs, compressor.serialize(), trainParams);
    ASSERT_FALSE(resultsWithState.empty());

    // Re-select from the same checkpoint without saving the ACE state. Training
    // is skipped so that both runs select from the same population.
    trainParams.saveAceState = false;
    ACETrainer replayTrainer(/* skipTraining */ true);
    auto resultsWithoutState = replayTrainer.train(
            multiInputs, **trainer.aceCheckpoint(), trainParams);
    ASSERT_FALSE(resultsWithoutState.empty());

    auto sizeWithAceState    = (*resultsWithState[0]).size();
    auto sizeWithoutAceState = (*resultsWithoutState[0]).size();

    // Serialized compressor without ACE state should be significantly smaller
    EXPECT_GT(sizeWithAceState, 0);
    EXPECT_GT(sizeWithoutAceState, 0);
    EXPECT_LE(sizeWithoutAceState, sizeWithAceState / 2)
            << "Serialized compressor without ACE state ("
            << sizeWithoutAceState
            << " bytes) should be at most half the size of one with ACE state ("
            << sizeWithAceState << " bytes)";

    // Compress data with both and verify identical output (same training run)
    auto compressWithResult =
            [&](const std::string_view& serializedCompressor) {
                auto comp = compressorGenFunc(serializedCompressor);
                CCtx cctx;
                cctx.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
                cctx.refCompressor(*comp);
                auto inputForCompress = Input::refSerial(
                        data.data(), data.size() * sizeof(data[0]));
                return cctx.compressOne(inputForCompress);
            };
    auto compressedWith    = compressWithResult(*resultsWithState[0]);
    auto compressedWithout = compressWithResult(*resultsWithoutState[0]);

    // Same training run → compressed output must be identical
    EXPECT_EQ(compressedWith.size(), compressedWithout.size())
            << "Compressed data sizes should be identical since both come from "
               "the same training run: "
            << compressedWith.size() << " vs " << compressedWithout.size();
}

TEST(ACETrainerTest, MaxTimeSecsIsSharedAcrossAceGraphs)
{
    constexpr size_t kNumGraphs   = 8;
    constexpr size_t kMaxTimeSecs = 2;
    // Enough records that a single generation outlasts the budget, so an
    // unshared budget costs a full generation per graph.
    constexpr size_t kRecordsPerGraph = 16384;

    // The budget only stops training between generations, and one generation
    // can outrun it on a slow machine, so a wall-clock bound would flake.
    // Compare against a one-graph run over the same records per graph instead:
    // shared, the cost stays near the one-graph cost; unshared, it multiplies
    // by the graph count.
    const auto oneGraph = timeAceTraining(1, kRecordsPerGraph, kMaxTimeSecs);
    const auto allGraphs =
            timeAceTraining(kNumGraphs, kRecordsPerGraph, kMaxTimeSecs);

    // Half of what an unshared budget would cost.
    const auto budget = oneGraph * int64_t(kNumGraphs) / 2;
    EXPECT_LT(allGraphs, budget)
            << "ACE training over " << kNumGraphs << " graphs took "
            << allGraphs.count() << "ms, against " << oneGraph.count()
            << "ms for one graph, for a budget of " << kMaxTimeSecs << "s";
}

} // namespace tests
} // namespace training
} // namespace openzl
