// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <optional>
#include <random>
#include "custom_parsers/dependency_registration.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/codecs/ACE.hpp"
#include "tools/training/ace/ace.h"
#include "tools/training/ace/ace_combination.h"
#include "tools/training/utils/serialized_compressor_internal.h"

namespace openzl {
namespace training {
namespace tests {

class ACECombinationTest : public testing::Test {
   public:
    void SetUp() override
    {
        candidates_.clear();
        params_.threads                = 1;
        std::mt19937::result_type seed = 0;
        gen_                           = std::mt19937(seed);
    }

    void setUpRandomFitnessCandidates(
            size_t numCandidates,
            size_t numSubcompressors)
    {
        candidates_.reserve(numCandidates);
        for (size_t i = 0; i < numCandidates; ++i) {
            std::vector<std::vector<size_t>> fitness;
            fitness.reserve(numSubcompressors);
            for (size_t j = 0; j < numSubcompressors; ++j) {
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

    std::vector<CandidateSelection> getCandidates(
            const std::string& name,
            std::vector<std::vector<size_t>> fitness)
    {
        std::vector<CandidateSelection> candidates;
        for (size_t i = 0; i < fitness.size(); ++i) {
            if (fitness[i].size() != 3) {
                throw std::runtime_error("Invalid fitness vector size");
            }
            ACECompressionResult result;
            result.compressedSize  = fitness[i][0];
            result.compressionTime = std::chrono::nanoseconds{ fitness[i][1] };
            result.decompressionTime =
                    std::chrono::nanoseconds{ fitness[i][2] };
            CandidateSelection cs(name, result, i);
            candidates.push_back(std::move(cs));
        }
        return candidates;
    }

    bool isPareto(std::vector<CandidateSelection> frontier)
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

   protected:
    TrainParams params_;
    std::vector<std::vector<CandidateSelection>> candidates_;
    std::mt19937 gen_;
    std::uniform_int_distribution<unsigned> distribution_{ 1, 10000 };
};

TEST_F(ACECombinationTest, ParetoFrontierFiltersCorrectly)
{
    ThreadPool threadPool(1);
    size_t numCandidates = 1000;
    std::vector<std::vector<size_t>> fitness;
    fitness.reserve(numCandidates);
    for (size_t j = 0; j < numCandidates; ++j) {
        fitness.push_back(getRandomFitness());
    }
    auto candidates = getCandidates("0", fitness);
    auto frontier   = filterParetoFrontier(std::move(candidates), threadPool);
    EXPECT_TRUE(isPareto(frontier));
}

TEST_F(ACECombinationTest, TestCandidatePruning)
{
    ThreadPool threadPool(1);
    size_t numCandidates = 1000;
    std::vector<std::vector<size_t>> fitness;
    fitness.reserve(numCandidates);
    for (size_t j = 0; j < numCandidates; ++j) {
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

TEST_F(ACECombinationTest, TestCandidatePruningWithDuplicateFitness)
{
    std::vector<std::vector<size_t>> fitness;
    fitness.push_back({ 20, 15, 15 });
    fitness.push_back({ 20, 13, 17 });
    fitness.push_back({ 25, 15, 10 });
    fitness.push_back({ 35, 10, 10 });
    fitness.push_back({ 35, 25, 5 });
    fitness.push_back({ 45, 12, 6 });
    auto candidates = getCandidates("0", fitness);
    EXPECT_TRUE(isPareto(candidates));
    auto pruned = pruneCandidates(std::move(candidates), 6);
    EXPECT_EQ(pruned.size(), 6);
}

TEST_F(ACECombinationTest, CombinationSizeIsLimited)
{
    setUpRandomFitnessCandidates(10, 40);
    auto frontier = combineCandidates(candidates_, params_);
    EXPECT_LE(frontier.size(), 1000);
}

TEST_F(ACECombinationTest, ProducesParetoOptimalCombination)
{
    setUpRandomFitnessCandidates(10, 40);
    auto frontier = combineCandidates(candidates_, params_);
    EXPECT_TRUE(isPareto(frontier));
}

TEST_F(ACECombinationTest, NoSaveAceStateProducesSmallerCompressor)
{
    // Create sample data: triple delta pattern compresses well with ACE
    std::vector<uint64_t> data(1000, 1);
    for (size_t i = 1; i < data.size(); ++i) {
        data[i] += data[i - 1];
    }
    for (size_t i = 1; i < data.size(); ++i) {
        data[i] += data[i - 1];
    }
    for (size_t i = 1; i < data.size(); ++i) {
        data[i] += data[i - 1];
    }
    auto input = Input::refSerial(data.data(), data.size() * sizeof(data[0]));
    std::vector<Input> inputsVec;
    inputsVec.push_back(std::move(input));
    std::vector<training::MultiInput> multiInputs;
    multiInputs.emplace_back(std::move(inputsVec));

    auto compressorGenFunc = [](poly::string_view serialized) {
        auto compressor = std::make_unique<Compressor>();
        compressor->deserialize(serialized);
        return compressor;
    };

    // Train once with saveAceState = true
    ACETrainer trainer;
    Compressor compressor;
    compressor.selectStartingGraph(graphs::ACE()(compressor));
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    training::TrainParams trainParams = {
        .compressorGenFunc = compressorGenFunc,
        .threads           = 1,
        .maxTimeSecs       = 5,
        .saveAceState      = true,
    };
    auto resultsWithState =
            trainer.train(multiInputs, compressor.serialize(), trainParams);
    ASSERT_FALSE(resultsWithState.empty());

    // Re-serialize the same checkpoint without ACE state
    trainParams.saveAceState = false;
    auto resultsWithoutState = getCombinedCompressors(
            multiInputs, *trainer.aceCheckpoint(), trainParams);
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

} // namespace tests
} // namespace training
} // namespace openzl
