// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "tools/training/utils/genetic_algorithm.h"

namespace openzl {
namespace training {
namespace tests {
namespace {

class TestGeneticAlgorithm : public GeneticAlgorithm<float> {
   public:
    ~TestGeneticAlgorithm() override = default;

    std::vector<float> initialPopulation() override
    {
        std::uniform_real_distribution<float> dist(-100, 100);

        std::vector<float> pop;
        pop.reserve(populationSize());
        for (size_t i = 0; i < populationSize(); ++i) {
            pop.push_back(dist(rng()));
        }
        return pop;
    }

    float crossover(const float& lhs, const float& rhs) override
    {
        std::bernoulli_distribution negate(0.5);
        return ((negate(rng()) ? -lhs : lhs) + (negate(rng()) ? -rhs : rhs))
                / 2;
    }

    float mutate(const float& parent) override
    {
        std::uniform_real_distribution<float> dist(-100, 100);
        return parent + dist(rng());
    }

    virtual std::vector<float> computeFitness(const float& gene) override
    {
        return {
            gene * gene,
            (gene - 10) * (gene - 10),
        };
    }
};

} // namespace

void expectClose(float f1, float f2, float eps = 0.00001)
{
    EXPECT_LT(std::abs(f1 - f2), eps);
}

void expectNotClose(float f1, float f2, float eps = 0.00001)
{
    EXPECT_GE(std::abs(f1 - f2), eps);
}

TEST(GeneticAlgorithmTest, CrowdingDistance1D)
{
    std::vector<std::vector<float>> fitness = {
        { 1.0 }, { 6.0 }, { 3.0 }, { 0.0 }
    };
    auto distance = crowdingDistance(fitness, { { 0, 1, 2, 3 } });
    ASSERT_EQ(distance.size(), 4u);
    ASSERT_TRUE(std::isinf(distance[3]) && distance[3] > 0);
    ASSERT_TRUE(std::isinf(distance[1]) && distance[1] > 0);
    expectClose(distance[0], (3.0 - 0.0) / 6.0);
    expectClose(distance[2], (6.0 - 1.0) / 6.0);

    distance = crowdingDistance(fitness, { { 3, 1, 2 } });
    ASSERT_EQ(distance.size(), 3u);
    ASSERT_TRUE(std::isinf(distance[0]));
    ASSERT_TRUE(std::isinf(distance[1]));
    expectClose(distance[2], (6.0 - 0.0) / 6.0);
}

TEST(GeneticAlgorithmTest, CrowdingDistance2D)
{
    std::vector<std::vector<float>> fitness = {
        { 0.0, 10.0 }, { 10.0, 5.0 }, { 5.0, 0.0 }, { 2.5, 7.5 }, { 4.0, 4.0 }
    };
    auto distance = crowdingDistance(fitness, { { 0, 1, 2, 3, 4 } });
    ASSERT_EQ(distance.size(), 5u);

    ASSERT_TRUE(std::isinf(distance[0]) && distance[0] > 0);
    ASSERT_TRUE(std::isinf(distance[1]) && distance[1] > 0);
    ASSERT_TRUE(std::isinf(distance[2]) && distance[2] > 0);
    expectClose(distance[3], (4.0 - 0.0) / 10.0 + (10.0 - 5.0) / 10.0);
    expectClose(distance[4], (5.0 - 2.5) / 10.0 + (5.0 - 0.0) / 10.0);
}

TEST(GeneticAlgorithmTest, Dominates)
{
    ASSERT_TRUE(dominates({ { 0.0 } }, { { 1.0 } }));
    ASSERT_FALSE(dominates({ { 0.5 } }, { { 0.5 } }));
    ASSERT_FALSE(dominates({ { 1.0 } }, { { 0.0 } }));

    ASSERT_TRUE(dominates({ { 0.0, 0.0 } }, { { 1.0, 1.0 } }));
    ASSERT_TRUE(dominates({ { 0.0, 1.0 } }, { { 1.0, 1.0 } }));
    ASSERT_FALSE(dominates({ { 1.0, 1.0 } }, { { 1.0, 1.0 } }));
    ASSERT_FALSE(dominates({ { 1.0, 1.0 } }, { { 0.0, 1.0 } }));
    ASSERT_FALSE(dominates({ { 1.0, 1.0 } }, { { 0.0, 0.0 } }));

    ASSERT_FALSE(dominates({ { 0.0, 1.0 } }, { { 1.0, 0.0 } }));
    ASSERT_FALSE(dominates({ { 1.0, 0.0 } }, { { 0.0, 1.0 } }));
}

TEST(GeneticAlgorithmTest, FastNonDominatedSort)
{
    std::vector<std::vector<float>> fitness = {
        { 1.0, 1.0 }, // front 0
        { 2.0, 2.0 }, // front 1
        { 3.0, 3.0 }, // front 2
        { 9.0, 0.0 }, // front 0
        { 8.0, 1.0 }, // front 1
        { 8.0, 2.0 }, // front 2
        { 0.0, 9.0 }, // front 0
        { 1.0, 8.0 }, // front 1
        { 2.0, 8.0 }, // front 2
        { 1.0, 1.0 }, // front 0
    };

    auto [fronts, rank] = fastNonDominatedSort(fitness);
    ASSERT_EQ(fronts.size(), 3u);
    ASSERT_EQ(rank.size(), fitness.size());

    for (size_t i = 0; i < fitness.size(); ++i) {
        ASSERT_EQ(rank[i], i % 3);
    }

    for (size_t r = 0; r < fronts.size(); ++r) {
        for (size_t idx : fronts[r]) {
            ASSERT_EQ(rank[idx], r);
        }
    }
}

TEST(GeneticAlgorithmTest, TournamentSelector)
{
    // Always picks the best candidate
    TournamentSelector selector(
            TournamentSelector::Parameters{ .torunamentSelectionProbability =
                                                    1.0 });
    ASSERT_EQ(1u, selector.select({ { 1, 0 } }, { { 10, 0 } }));
    ASSERT_EQ(0u, selector.select({ { 0, 0 } }, { { 10, 0 } }));

    // Always picks the worst candidate
    selector = TournamentSelector(
            TournamentSelector::Parameters{ .torunamentSelectionProbability =
                                                    0.0 });
    ASSERT_EQ(0u, selector.select({ { 1, 0 } }, { { 10, 0 } }));
    ASSERT_EQ(1u, selector.select({ { 0, 0 } }, { { 10, 0 } }));
}

TEST(GeneticAlgorithmTest, ExtendPopulation)
{
    TestGeneticAlgorithm ga;
    ASSERT_EQ(ga.population().size(), 0u);
    ASSERT_EQ(ga.fitness().size(), 0u);
    ASSERT_EQ(ga.rank().size(), 0u);
    ASSERT_EQ(ga.crowdingDistance().size(), 0u);

    ga.extendPopulation({ 0.0, 0.0, 1.0, 5.0 });

    ASSERT_EQ(ga.population().size(), 3u);
    ASSERT_EQ(ga.fitness().size(), 3u);
    ASSERT_EQ(ga.rank().size(), 3u);
    ASSERT_EQ(ga.crowdingDistance().size(), 3u);

    expectClose(ga.population()[0], 0.0);
    expectClose(ga.population()[1], 1.0);
    expectClose(ga.population()[2], 5.0);
}

TEST(GeneticAlgorithmTest, SubsetPopulation)
{
    TestGeneticAlgorithm ga;
    ga.extendPopulation({ 0.0, 0.0, 1.0, 5.0 });

    auto fitness          = ga.fitness()[1];
    auto rank             = ga.rank()[1];
    auto crowdingDistance = ga.crowdingDistance()[1];
    ga.subsetPopulation({ { 1 } });

    ASSERT_EQ(ga.population().size(), 1u);
    ASSERT_EQ(ga.fitness().size(), 1u);
    ASSERT_EQ(ga.rank().size(), 1u);
    ASSERT_EQ(ga.crowdingDistance().size(), 1u);

    expectClose(ga.population()[0], 1.0);
    ASSERT_EQ(fitness, ga.fitness()[0]);
    ASSERT_EQ(rank, ga.rank()[0]);
    expectNotClose(crowdingDistance, ga.crowdingDistance()[0]);
}

TEST(GeneticAlgorithmTest, SmokeTest)
{
    TestGeneticAlgorithm ga;
    ga.run();
    auto solution = ga.solution();
    std::vector<float> genes;
    std::vector<std::vector<float>> fitness;
    for (const auto& [g, f] : solution) {
        genes.push_back(g);
        fitness.push_back(f);
        ASSERT_EQ(ga.computeFitness(g), f);
    }
    auto [fronts, rank] = fastNonDominatedSort(fitness);
    ASSERT_EQ(fronts.size(), 1u);

    auto hasSolutionThatDominates =
            [&solution](const std::vector<float>& candidate) {
                for (const auto& [_, s] : solution) {
                    if (dominates(s, candidate)) {
                        return true;
                    }
                }
                return false;
            };
    ASSERT_TRUE(hasSolutionThatDominates({ 27, 27 }));
    ASSERT_TRUE(hasSolutionThatDominates({ 100, 2 }));
    ASSERT_TRUE(hasSolutionThatDominates({ 2, 100 }));
}

} // namespace tests
} // namespace training
} // namespace openzl
