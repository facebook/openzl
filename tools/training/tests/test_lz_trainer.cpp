// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <memory>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/openzl.hpp"

#include "openzl/compress/cgraph.h"

#include "openzl/codecs/zl_lz.h"
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

} // namespace tests
} // namespace training
} // namespace openzl
