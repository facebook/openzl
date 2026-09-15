// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <chrono>
#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/openzl.hpp"

#include "openzl/compress/cgraph.h"
#include "openzl/zl_reflection.h"

#include "openzl/codecs/zl_lz.h"
#include "openzl/codecs/zl_split.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/lz/lz_compressor.h"
#include "tools/training/lz/lz_genetic_algorithm.h"
#include "tools/training/lz/lz_trainer.h"

namespace openzl {
namespace training {
namespace tests {
namespace {

/// Data with enough redundancy for LZ to find matches, but not so much that
/// every compression level produces the same output.
std::string lzData()
{
    const std::vector<std::string> words = { "alpha", "beta",  "gamma",
                                             "delta", "epsel", "zeta" };
    std::string data;
    std::mt19937 gen(0);
    std::uniform_int_distribution<size_t> distribution(0, words.size() - 1);
    while (data.size() < 100000) {
        data += words[distribution(gen)];
        data += ' ';
    }
    return data;
}

std::vector<MultiInput> makeInputs(const std::string& data)
{
    MultiInput multiInput;
    multiInput.add(Input::refSerial(data));
    std::vector<MultiInput> inputs;
    inputs.push_back(std::move(multiInput));
    return inputs;
}

CompressorGenFn compressorGenFunc()
{
    return [](poly::string_view serialized, poly::string_view bundle) {
        auto compressor = std::make_unique<Compressor>();
        compressor->deserialize(serialized, bundle);
        return compressor;
    };
}

TrainParams lzTrainParams()
{
    return TrainParams{
        .compressorGenFunc = compressorGenFunc(),
        .threads           = 1,
    };
}

/// A compressor whose starting graph is a tunable instance of the LZ graph.
/// Parameterizing without a name inherits the "zl.lz" prefix, which is how the
/// trainer finds the instance.
std::string lzCompressor()
{
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    compressor.selectStartingGraph(
            compressor.parameterizeGraph(ZL_GRAPH_LZ, GraphParameters{}));
    return compressor.serialize();
}

/// A compressor which splits an @p inputSize byte input evenly between two
/// tunable instances of the LZ graph.
std::string twoLzGraphCompressor(size_t inputSize)
{
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    // A trailing zero sends everything after the first segment to the second
    const std::vector<size_t> segmentSizes = { inputSize / 2, 0 };
    const std::vector<GraphID> lzGraphs    = {
        compressor.parameterizeGraph(ZL_GRAPH_LZ, GraphParameters{}),
        compressor.parameterizeGraph(ZL_GRAPH_LZ, GraphParameters{}),
    };
    compressor.selectStartingGraph(ZL_Compressor_registerSplitGraph(
            compressor.get(),
            ZL_Type_serial,
            segmentSizes.data(),
            lzGraphs.data(),
            lzGraphs.size()));
    return compressor.serialize();
}

/// A compressor with no LZ graph for the trainer to tune.
std::string genericCompressor()
{
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    compressor.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);
    return compressor.serialize();
}

/// Round trips @p data through @p serialized and @returns its compressed size.
size_t roundTrip(
        const SerializedCompressorInternal& serialized,
        const std::string& data)
{
    auto compressor = compressorGenFunc()(*serialized, "");
    CCtx cctx;
    cctx.refCompressor(*compressor);
    auto input      = Input::refSerial(data);
    auto compressed = cctx.compressOne(input);

    DCtx dctx;
    EXPECT_EQ(dctx.decompressSerial(compressed), data);
    return compressed.size();
}

/// @returns true iff @p lhs and @p rhs hold the same value for @p field.
bool fieldsEqual(
        const LzField& field,
        const LzCompressor& lhs,
        const LzCompressor& rhs)
{
    auto inherited = lhs;
    field.inherit(inherited, rhs);
    return inherited == lhs;
}

/// @returns the number of fields @p lhs and @p rhs disagree on.
size_t numDifferingFields(const LzCompressor& lhs, const LzCompressor& rhs)
{
    size_t differences = 0;
    for (const auto& field : lzFields()) {
        differences += !fieldsEqual(field, lhs, rhs);
    }
    return differences;
}

LzCompressor randomTuning(std::mt19937_64& rng)
{
    LzCompressor lz;
    for (const auto& field : lzFields()) {
        field.randomize(lz, rng);
    }
    return lz;
}

/// @returns the name of the tunable LZ graph in @p compressor.
std::string lzGraphName(const Compressor& compressor)
{
    for (const auto& graph :
         graph_mutation::findAllGraphsWithPrefix(compressor, LZ_GRAPH_NAME)) {
        if (ZL_Compressor_getGraphType(compressor.get(), graph)
            == ZL_GraphType_parameterized) {
            return ZL_Compressor_Graph_getName(compressor.get(), graph);
        }
    }
    throw Exception("No tunable LZ graph in the compressor");
}

/// A budget small enough to keep the tests quick.
LzGeneticAlgorithm::Parameters lzGaParams()
{
    LzGeneticAlgorithm::Parameters params;
    params.numThreads     = 1;
    params.populationSize = 8;
    params.maxGenerations = 3;
    return params;
}

/// A genetic algorithm tuning the LZ graph of `lzCompressor()` on @p inputs.
LzGeneticAlgorithm lzGeneticAlgorithm(
        const std::vector<MultiInput>& inputs,
        const LzGeneticAlgorithm::Parameters& params = lzGaParams())
{
    auto serialized     = std::make_shared<std::string>(lzCompressor());
    auto makeCompressor = [serialized] {
        return std::move(*compressorGenFunc()(*serialized, ""));
    };
    return LzGeneticAlgorithm(
            makeCompressor, lzGraphName(makeCompressor()), inputs, params);
}

} // namespace

TEST(LzTrainerTest, ParetoFrontierRequiresTraining)
{
    LzTrainer trainer;
    EXPECT_THROW(trainer.paretoFrontier(), Exception);
}

TEST(LzTrainerTest, TrainsAnLzGraph)
{
    const auto data = lzData();
    LzTrainer trainer;
    trainer.train(makeInputs(data), lzCompressor(), lzTrainParams());

    auto frontier = trainer.paretoFrontier();
    ASSERT_FALSE(frontier.empty());

    // Every compressor on the frontier must round trip and actually compress,
    // and the frontier is sorted smallest-first
    const auto best = roundTrip(frontier[0], data);
    for (const auto& compressor : frontier) {
        const auto compressedSize = roundTrip(compressor, data);
        EXPECT_LT(compressedSize, data.size());
        EXPECT_LE(best, compressedSize);
    }
}

TEST(LzTrainerTest, TrainsWithinATimeBudget)
{
    const auto data         = lzData();
    auto trainParams        = lzTrainParams();
    trainParams.maxTimeSecs = 1;

    LzTrainer trainer;
    trainer.train(makeInputs(data), lzCompressor(), trainParams);

    // The budget cuts the search short, but it still produces a compressor
    auto frontier = trainer.paretoFrontier();
    ASSERT_FALSE(frontier.empty());
    EXPECT_LT(roundTrip(frontier[0], data), data.size());
}

TEST(LzTrainerTest, SplitsTheTimeBudgetBetweenTheGraphsLeftToTrain)
{
    const auto now      = std::chrono::steady_clock::now();
    const auto deadline = now + std::chrono::seconds(600);

    // timeShare() reads the clock itself, so it sees slightly less time left
    // than was put on the deadline here, and truncates to whole seconds
    auto shareIs = [](const poly::optional<std::chrono::seconds>& share,
                      int seconds) {
        return share.has_value() && share->count() <= seconds
                && share->count() >= seconds - 1;
    };

    // Each graph gets an even share of the time left
    EXPECT_TRUE(shareIs(timeShare(deadline, 4), 150));
    // A graph that finished early leaves its unused share to the rest
    EXPECT_TRUE(shareIs(timeShare(deadline, 2), 300));
    EXPECT_TRUE(shareIs(timeShare(deadline, 1), 600));

    // No deadline is no limit
    EXPECT_FALSE(timeShare(poly::nullopt, 4).has_value());

    // Past the deadline every graph still gets a second, so the last graphs
    // are tuned rather than left at their defaults
    EXPECT_EQ(
            timeShare(now - std::chrono::seconds(10), 4),
            std::chrono::seconds(1));
}

TEST(LzTrainerTest, TrainsEveryLzGraphUnderOneTimeBudget)
{
    const auto data         = lzData();
    auto trainParams        = lzTrainParams();
    trainParams.maxTimeSecs = 2;

    LzTrainer trainer;
    trainer.train(
            makeInputs(data), twoLzGraphCompressor(data.size()), trainParams);

    // Both graphs are tuned out of the one budget they share
    auto frontier = trainer.paretoFrontier();
    ASSERT_FALSE(frontier.empty());
    EXPECT_LT(roundTrip(frontier[0], data), data.size());
}

TEST(LzTrainerTest, LeavesCompressorsWithoutAnLzGraphAlone)
{
    const auto data = lzData();
    LzTrainer trainer;
    trainer.train(makeInputs(data), genericCompressor(), lzTrainParams());

    // Nothing to tune, so the untouched compressor is the only result
    auto frontier = trainer.paretoFrontier();
    ASSERT_EQ(frontier.size(), 1);
    EXPECT_LT(roundTrip(frontier[0], data), data.size());
}

TEST(LzTrainerTest, RetuningAnLzGraphInPlaceTakesEffect)
{
    const auto data = lzData();

    // Retunes an LZ graph in place, exactly as LzGraphMutation does, and
    // @returns the size `data` compresses to
    auto compressedSize = [&data](const graphs::Lz::Parameters& params) {
        Compressor compressor;
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        const auto lz =
                compressor.parameterizeGraph(ZL_GRAPH_LZ, GraphParameters{});
        compressor.selectStartingGraph(lz);

        const auto graphParams = graphs::Lz(params).parameters();
        const auto cParams     = graphParams->toC();
        compressor.unwrap(ZL_Compressor_overrideGraphParams(
                compressor.get(), lz, &cParams));

        CCtx cctx;
        cctx.refCompressor(compressor);
        auto input = Input::refSerial(data);
        return cctx.compressOne(input).size();
    };

    // Searching one in every 16 bytes for a match compresses worse than
    // searching every byte
    EXPECT_LT(
            compressedSize({ .nodeParams = { .acceleration = 1 } }),
            compressedSize({ .nodeParams = { .acceleration = 16 } }));
}

TEST(LzCompressorTest, BuildsItsBackendGraphsIntoTheCompressor)
{
    LzCompressor lz;
    lz.params.nodeParams.acceleration = 4;
    lz.literalsGraph                  = literalsCompressors()[0];
    lz.offsetsGraph                   = offsetsCompressors()[0];

    Compressor compressor;
    const auto params = lz.buildParams(compressor);

    // One custom graph per backend graph the tuning overrides, which the LZ
    // graph selects by index
    ASSERT_TRUE(params.customGraphs.has_value());
    EXPECT_EQ(params.customGraphs->size(), 2);
    ASSERT_TRUE(params.localParams.has_value());
    EXPECT_FALSE(params.localParams->getIntParams().empty());
}

TEST(LzCompressorTest, TheMuxLengthsGraphReplacesTheMuxedOutputs)
{
    LzCompressor lz;
    lz.muxedBytesGraph      = muxedBytesCompressors()[0];
    lz.overflowLengthsGraph = overflowLengthsCompressors()[0];
    lz.muxLengthsGraph      = muxLengthsCompressors()[0];

    // The mux lengths node doesn't run, so its outputs have no graphs to build
    Compressor compressor;
    const auto params = lz.buildParams(compressor);
    ASSERT_TRUE(params.customGraphs.has_value());
    EXPECT_EQ(params.customGraphs->size(), 1);
}

TEST(LzGeneticAlgorithmTest, MutationChangesAtMostOneField)
{
    std::mt19937_64 rng(0);
    const auto data   = lzData();
    const auto inputs = makeInputs(data);
    auto ga           = lzGeneticAlgorithm(inputs);

    for (size_t i = 0; i < 100; ++i) {
        const auto parent = randomTuning(rng);
        const auto child  = ga.mutate(parent);
        EXPECT_LE(numDifferingFields(parent, child), 1);
        // The graph fields of `params` are owned by the backend graph genes
        EXPECT_FALSE(child.params.literalsGraph.has_value());
        EXPECT_FALSE(child.params.offsetsGraph.has_value());
        EXPECT_FALSE(child.params.muxedBytesGraph.has_value());
        EXPECT_FALSE(child.params.overflowLengthsGraph.has_value());
        EXPECT_FALSE(child.params.muxLengthsGraph.has_value());
    }
}

TEST(LzGeneticAlgorithmTest, CrossoverTakesEveryFieldFromAParent)
{
    std::mt19937_64 rng(0);
    const auto data   = lzData();
    const auto inputs = makeInputs(data);
    auto ga           = lzGeneticAlgorithm(inputs);

    for (size_t i = 0; i < 100; ++i) {
        const auto parent1 = randomTuning(rng);
        const auto parent2 = randomTuning(rng);
        const auto child   = ga.crossover(parent1, parent2);
        for (const auto& field : lzFields()) {
            EXPECT_TRUE(
                    fieldsEqual(field, child, parent1)
                    || fieldsEqual(field, child, parent2));
        }
    }
}

TEST(LzGeneticAlgorithmTest, FindsATuningNoWorseThanTheDefault)
{
    const auto data   = lzData();
    const auto inputs = makeInputs(data);
    auto ga           = lzGeneticAlgorithm(inputs);

    // The untuned graph is in the initial population, so the frontier can only
    // improve on it
    const auto untuned = ga.computeFitness(LzCompressor{});
    ASSERT_FALSE(std::isinf(untuned[0]));

    ga.run();
    const auto solution = ga.solution();
    ASSERT_FALSE(solution.empty());
    EXPECT_LE(solution.front().second[0], untuned[0]);
}

} // namespace tests
} // namespace training
} // namespace openzl
