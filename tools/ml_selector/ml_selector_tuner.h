// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include <map>
#include <random>
#include <string>
#include <vector>
#include "openzl/cpp/Compressor.hpp"
#include "tools/ml_selector/ml_selector_trainer_utils.h"
#include "tools/training/utils/utils.h"

namespace openzl::training {

/**
 * Defines the search range for a single hyperparameter.
 */
struct ParamRange {
    float min;
    float max;
};

inline const std::map<std::string, ParamRange> defaultParamRanges = {
    { "learning_rate", { 0.001f, 1.0f } },
    { "min_child_weight", { 0.0f, 30.0f } },
    { "subsample", { 0.1f, 1.0f } },
    { "colsample_bynode", { 0.1f, 1.0f } },
};

/**
 * Defines the training context for ML selector training.
 *
 * @param featureNames names of each feature for the ML model
 * @param splitData train/test split data
 * @param successorGraphs list of successor graphs to train on
 * @param compressor base compressor to train
 */
struct TrainingContext {
    std::vector<std::string> featureNames;
    std::vector<const char*> featurePtrNames;
    TestTrainData splitData;
    std::vector<ZL_GraphID> successorGraphs;
    Compressor* compressor;
};

/**
 * Tuning config for ML selector hyperparameter tuning.
 *
 * @param populationSize number of hyperparameter sets per iteration
 * @param survivalRate fraction of population to survive to next iteration
 * @param maxIterations max number of iterations to run
 * @param convergenceThreshold num of iterations without improvement before
 * stopping
 * @param mutationRate probability of random modification to hyperparameter
 * @param compressionWeight weight of compression size versus compression time
 * @param rng random number generator for stochastic operations
 */
struct TuningConfig {
    size_t populationSize       = 20;
    float survivalRate          = 0.25f;
    size_t maxIterations        = 10;
    size_t convergenceThreshold = 2;
    float mutationRate          = 0.25f;
    float compressionWeight     = 1.0f;
    mutable std::mt19937 rng{ std::random_device{}() };
};

/**
 * Compression size and time for given set of hyperparameters.
 */
struct EvaluationResult {
    float size;
    float ctime;
    Hyperparams params;
};

/**
 * Generates an initial population of hyperparameters using stratified sampling.
 *
 * Each parameter range is divided into `populationSize` equal segments, and
 * each population member samples one random value from each segment. This
 * ensures the initial population is evenly distributed across the entire
 * parameter space.
 */
std::vector<Hyperparams> generateInitialTuningPop(
        const std::map<std::string, ParamRange>& paramRanges,
        const TuningConfig& config);

/**
 * Calculates a weighted fitness score for a set of hyperparameters.
 * @param size   Compressed size
 * @param ctime  Compression time
 * @param weight Weight for compression size vs time (0.0-1.0).
 *               Higher values prioritize size over speed.
 * @returns Weighted fitness score (lower is better)
 */
float calculateHyperparamScore(float size, float ctime, float weight);

/**
 * Genetic algo hyperparameter tuning for ML selector.
 *
 * Algorithm:
 * 1. Start with random population of hyperparams
 * 2. For each iteration, evaluate each hyperparam through compression sizes
 * 3. Select the top N hyperparams that survive to next iteration
 * 4. Crossover and create new hyperparams by combining hyperparams from top N
 * 5. Mutate hyperparams by randomly changing a hyperparam value
 * 6. Repeat until convergence or max iterations reached
 *
 * @param ctx               Training context
 * @param inputs            Inputs for training and evaluating ML selector
 * @param paramRanges       Map of param name to {min, max} range to search
 * @param initialPop        Initial population of hyperparams to start with
 * @param config            Tuning configuration
 * @returns Best hyperparameters found based on compression size
 */
std::pair<EvaluationResult, EvaluationResult> tuneHyperparams(
        TrainingContext& ctx,
        const std::vector<MultiInput>& inputs,
        const std::map<std::string, ParamRange>& paramRanges,
        const std::vector<Hyperparams>& initialPop,
        const TuningConfig& config);

} // namespace openzl::training
