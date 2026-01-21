// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "tools/ml_selector/ml_selector_tuner.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <queue>
#include <random>
#include <set>
#include "openzl/zl_compressor.h"
#include "tools/logger/Logger.h"
#include "tools/ml_selector/ml_selector_graph.h"
using namespace openzl::tools::logger;

namespace openzl::training {
namespace { // Anonymous namespace

struct EvaluationResultComparator {
    float cSizeWeight;

    explicit EvaluationResultComparator(float weight = 0.75f)
            : cSizeWeight{ weight }
    {
    }

    bool operator()(const EvaluationResult& a, const EvaluationResult& b) const
    {
        float scoreA = calculateHyperparamScore(a.size, a.ctime, cSizeWeight);
        float scoreB = calculateHyperparamScore(b.size, b.ctime, cSizeWeight);
        return scoreA > scoreB;
    }
};

using SortedPopulation = std::priority_queue<
        EvaluationResult,
        std::vector<EvaluationResult>,
        EvaluationResultComparator>;

const std::set<std::string> kIntegerParams = { "max_depth",
                                               "num_boost_round",
                                               "max_leaves" };

} // namespace

static std::pair<float, float> evaluateWithContext(
        TrainingContext& ctx,
        const std::vector<MultiInput>& inputs,
        const Hyperparams& params)
{
    GBTPredictorWrapper gbtPred = trainXGBoostModel(
            ctx.splitData, ctx.successorGraphs.size(), params);

    GBTModel coreModel = {
        .predictor        = gbtPred.core_predictor_.get(),
        .featureGenerator = FeatureGen_integer,
        .nbSuccessors     = ctx.successorGraphs.size(),
        .nbFeatures       = ctx.featurePtrNames.size(),
        .featureLabels    = ctx.featurePtrNames.data(),
    };
    ZL_MLSelectorConfig config = { .model         = ZL_GBT,
                                   .runtimeConfig = &coreModel };

    // Register new ML selector graph with trained model
    auto mlSelectorGraphId = unwrap(ZL_MLSelector_registerGraph(
            ctx.compressor->get(),
            &config,
            ctx.successorGraphs.data(),
            ctx.successorGraphs.size()));

    ZL_GraphID staticGraph = ZL_Compressor_registerStaticGraph_fromNode1o(
            ctx.compressor->get(),
            ZL_NODE_CONVERT_SERIAL_TO_NUM_LE64,
            mlSelectorGraphId);

    ctx.compressor->selectStartingGraph(staticGraph);

    CCtx cctx = refCCtxForTraining(*ctx.compressor);

    auto const timerStart = std::chrono::steady_clock::now();
    size_t totalSize      = 0;
    for (size_t i = 0; i < inputs.size(); i++) {
        size_t compressedSize = cctx.compressOne(inputs[i]->front()).size();
        totalSize += compressedSize;
    }
    std::chrono::duration<double, std::milli> const timeElapsedMS =
            (std::chrono::steady_clock::now() - timerStart);
    return std::make_pair(totalSize, timeElapsedMS.count());
}

static SortedPopulation evaluatePopulation(
        TrainingContext& ctx,
        const std::vector<MultiInput>& inputs,
        const std::vector<Hyperparams>& currPop,
        float cSizeWeight)
{
    EvaluationResultComparator comparator(cSizeWeight);
    SortedPopulation compressionSizes(comparator);

    float minScore = std::numeric_limits<float>::max();
    float maxScore = std::numeric_limits<float>::lowest();
    float minSize = 0, minCtime = 0;
    float maxSize = 0, maxCtime = 0;
    for (size_t i = 0; i < currPop.size(); i++) {
        auto [size, ctime] = evaluateWithContext(ctx, inputs, currPop[i]);
        compressionSizes.push({ size, ctime, currPop[i] });
        float score = calculateHyperparamScore(size, ctime, cSizeWeight);

        if (score < minScore) {
            minScore = score;
            minSize  = size;
            minCtime = ctime;
        }
        if (score > maxScore) {
            maxScore = score;
            maxSize  = size;
            maxCtime = ctime;
        }
    }

    Logger::log_c(
            VERBOSE2,
            "    Worst result in this generation with compression size %0.2f and ctime %0.2f",
            maxSize,
            maxCtime);

    Logger::log_c(
            VERBOSE2,
            "    Best result in this generation with compression size %0.2f and ctime %0.2f",
            minSize,
            minCtime);

    return compressionSizes;
}

static std::vector<Hyperparams> crossover(
        const std::vector<Hyperparams>& survivingPop,
        size_t childPopSize,
        const TuningConfig& config)
{
    std::uniform_int_distribution<size_t> parentDist(
            0, survivingPop.size() - 1);
    std::uniform_int_distribution<int> coinFlip(0, 1);

    std::vector<Hyperparams> childPop;
    for (size_t i = 0; i < childPopSize; i++) {
        size_t parent1 = parentDist(config.rng);
        size_t parent2;
        do {
            parent2 = parentDist(config.rng);
        } while (parent2 == parent1 && survivingPop.size() > 1);

        Hyperparams child;
        for (const auto& [gene, _] : survivingPop[parent1]) {
            child[gene] = coinFlip(config.rng) == 0
                    ? survivingPop[parent1].at(gene)
                    : survivingPop[parent2].at(gene);
        }
        childPop.push_back(child);
    }
    return childPop;
}

static void mutate(
        std::vector<Hyperparams>& pop,
        const TuningConfig& config,
        const std::map<std::string, ParamRange>& paramRanges)
{
    std::uniform_real_distribution<float> probDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> noiseDist(-0.2f, 0.2f);

    for (auto& individual : pop) {
        for (auto& [gene, value] : individual) {
            if (probDist(config.rng) < config.mutationRate) {
                const auto& range = paramRanges.at(gene);
                float currentVal  = std::stof(value);
                float mutatedVal  = std::clamp(
                        currentVal
                                + noiseDist(config.rng)
                                        * (range.max - range.min),
                        range.min,
                        range.max);

                if (kIntegerParams.count(gene)) {
                    value = std::to_string(
                            static_cast<int>(std::round(mutatedVal)));
                } else {
                    value = std::to_string(mutatedVal);
                }
            }
        }
    }
}

float calculateHyperparamScore(float size, float ctime, float weight)
{
    return weight * size + (1 - weight) * ctime;
}

std::vector<Hyperparams> generateInitialTuningPop(
        const std::map<std::string, ParamRange>& ranges,
        const TuningConfig& config)
{
    std::vector<Hyperparams> population;
    population.reserve(config.populationSize);

    if (config.populationSize == 0) {
        std::cerr << "Error: Population size must be greater than 0"
                  << std::endl;
        return population;
    }

    // For each parameter, create stratified intervals
    std::map<std::string, std::vector<float>> stratifiedValues;

    for (const auto& [name, range] : ranges) {
        std::vector<float> values;
        float stepSize = (range.max - range.min) / config.populationSize;

        float curr = range.min;
        for (size_t i = 0; i < config.populationSize; i++) {
            std::uniform_real_distribution<float> dist(curr, curr + stepSize);
            float value = dist(config.rng);
            if (kIntegerParams.count(name)) {
                values.push_back((int)value);
            } else {
                values.push_back(value);
            }
            curr += stepSize;
        }
        // Shuffle to randomize order
        std::shuffle(values.begin(), values.end(), config.rng);
        stratifiedValues[name] = std::move(values);
    }

    for (size_t i = 0; i < config.populationSize; i++) {
        Hyperparams params;
        for (const auto& [name, values] : stratifiedValues) {
            if (kIntegerParams.count(name)) {
                params[name] =
                        std::to_string(static_cast<int>(std::round(values[i])));
            } else {
                params[name] = std::to_string(values[i]);
            }
        }
        population.push_back(params);
    }
    return population;
}

static int getLabelDistribution(const TrainingContext& ctx)
{
    // Print label distribution across classes
    std::map<int, size_t> labelCounts;
    for (float label : ctx.splitData.train.y) {
        labelCounts[static_cast<int>(label)]++;
    }
    for (float label : ctx.splitData.test.y) {
        labelCounts[static_cast<int>(label)]++;
    }

    size_t totalSamples =
            ctx.splitData.train.y.size() + ctx.splitData.test.y.size();
    Logger::log_c(
            VERBOSE2, "Label distribution across %zu samples:", totalSamples);
    for (const auto& [classIdx, count] : labelCounts) {
        float percentage = 100.0f * count / totalSamples;
        Logger::log_c(
                VERBOSE2,
                "    Class %d: %zu samples (%.1f%%)",
                classIdx,
                count,
                percentage);
    }
    return labelCounts.size();
}

static void printTuningResults(
        EvaluationResult bestPop,
        const std::vector<MultiInput>& inputs,
        float defaultSize,
        float defaultCtime)
{
    Logger::log(VERBOSE1, "Best Hyperparameter");
    for (const auto& [name, value] : bestPop.params) {
        Logger::log_c(VERBOSE1, "    %s: %s", name.c_str(), value.c_str());
    }
    size_t uncompressedSize = 0;
    for (auto& input : inputs) {
        uncompressedSize +=
                (*input).front()
                        .contentSize(); // Gets first Input's contentSize
    }

    Logger::log_c(
            VERBOSE2,
            "Compressed size changed from %0.2f to %0.2f",
            defaultSize,
            bestPop.size);

    Logger::log_c(
            VERBOSE2,
            "Compressed ratio changed from %0.2f to %0.2f",
            uncompressedSize / defaultSize,
            uncompressedSize / bestPop.size);

    Logger::log_c(
            VERBOSE2,
            "Compression time changed from %0.2f to %0.2f",
            defaultCtime,
            bestPop.ctime);
}

std::pair<EvaluationResult, EvaluationResult> tuneHyperparams(
        TrainingContext& ctx,
        const std::vector<MultiInput>& inputs,
        const std::map<std::string, ParamRange>& paramRanges,
        const std::vector<Hyperparams>& initialPop,
        const TuningConfig& config)
{
    if (getLabelDistribution(ctx) < 2) {
        Logger::log(
                VERBOSE1,
                "Error: Not enough classes for tuning. Need at least 2 classes.");
        return std::make_pair(
                EvaluationResult{ 0, 0, defaultXGBoostHyperParams },
                EvaluationResult{ 0, 0, defaultXGBoostHyperParams });
    }

    float cSizeWeight     = config.compressionWeight;
    size_t iterationsLeft = config.maxIterations;
    size_t convergeVal    = config.convergenceThreshold;
    float prevBestScore   = -1;

    size_t survivingPopSize =
            static_cast<size_t>(config.populationSize * config.survivalRate);
    std::vector<Hyperparams> survivingPop;
    std::vector<Hyperparams> currentPop = initialPop;

    while (iterationsLeft > 0 && convergeVal > 0) {
        // Evaluate current population
        Logger::log_c(
                VERBOSE2,
                "Evaluating current population %d",
                config.maxIterations - iterationsLeft);

        auto compressionSizes =
                evaluatePopulation(ctx, inputs, currentPop, cSizeWeight);

        // Check for convergence
        const auto& best = compressionSizes.top();
        float score =
                calculateHyperparamScore(best.size, best.ctime, cSizeWeight);
        if (prevBestScore < 0) {
            prevBestScore = score;
        } else {
            if (std::abs(score - prevBestScore) < 0.0005f * prevBestScore) {
                // If there is no improvement then decrease the convergence
                convergeVal--;
                prevBestScore = score;
            } else {
                convergeVal = config.convergenceThreshold;
            }
        }

        // Select parents for next generation
        survivingPop.clear();
        for (size_t i = 0; i < survivingPopSize; i++) {
            survivingPop.push_back(compressionSizes.top().params);
            compressionSizes.pop();
        }

        // Crossover
        auto childPop = crossover(
                survivingPop, config.populationSize - survivingPopSize, config);

        survivingPop.insert(
                survivingPop.end(), childPop.begin(), childPop.end());

        // Mutation
        mutate(survivingPop, config, paramRanges);

        iterationsLeft--;
        currentPop = survivingPop;
    }
    Logger::log(VERBOSE2, "Stopping tuning... Evaluating final population");
    auto finalPop = evaluatePopulation(ctx, inputs, survivingPop, cSizeWeight);
    auto [defaultSize, defaultCtime] =
            evaluateWithContext(ctx, inputs, defaultXGBoostHyperParams);
    auto bestPop = finalPop.top();

    printTuningResults(bestPop, inputs, defaultSize, defaultCtime);

    return std::make_pair(
            bestPop,
            EvaluationResult{
                    defaultSize, defaultCtime, defaultXGBoostHyperParams });
}

} // namespace openzl::training
