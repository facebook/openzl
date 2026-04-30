// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <random>
#include "tools/training/ace/ace_combination.h"

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

} // namespace tests
} // namespace training
} // namespace openzl
