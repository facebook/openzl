// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "tools/ml_selector/ml_selector_trainer.h"
#include <cstdio>
#include <string>
#include <vector>
#include "ml_features.h"
#include "openzl/zl_reflection.h"
#include "src/openzl/compress/selectors/ml/features.h"
#include "tools/ml_selector/ml_selector_graph.h"
#include "tools/ml_selector/ml_selector_trainer_utils.h"
#include "tools/ml_selector/ml_selector_tuner.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/sample_collection/training_sample_collector.h"

namespace openzl::training {

const std::string ML_SELECTOR_GRAPH_NAME = "mlSelector";

std::shared_ptr<const std::string_view> trainMLSelectorGraph(
        const std::vector<MultiInput>& inputs,
        Compressor& compressor,
        const TrainParams& trainParams)
{
    (void)trainParams;
    std::vector<ZL_GraphID> successorGraphs;

    // Find the ML selector graph by prefix
    auto mlSelectorGraphNames = graph_mutation::findAllGraphsWithPrefix(
            compressor.serialize(), ML_SELECTOR_GRAPH_NAME);

    if (mlSelectorGraphNames.empty() || mlSelectorGraphNames.size() > 1) {
        throw std::runtime_error(
                "Error finding ML selector graph with prefix '"
                + ML_SELECTOR_GRAPH_NAME + "'");
    }

    // Use the first mlSelector graph found
    const ZL_GraphID mlSelectorGraph =
            compressor.getGraph(mlSelectorGraphNames[0]).value();
    const auto& mlSelectorGraphName = mlSelectorGraphNames[0];

    const auto successors = ZL_Compressor_Graph_getCustomGraphs(
            compressor.get(), mlSelectorGraph);

    successorGraphs.reserve(successors.nbGraphIDs);
    for (size_t i = 0; i < successors.nbGraphIDs; ++i) {
        successorGraphs.push_back(successors.graphids[i]);
    }

    auto cctx = refCCtxForTraining(compressor);
    // Collect inputs for first mlSelector graph
    auto mlSelectorInputs =
            collectInputStreamsForGraph(inputs, mlSelectorGraphName, cctx);

    if (mlSelectorInputs.empty()) {
        throw std::runtime_error("Unable to get inputs to mlSelector");
    }

    ProcessedMLTrainingSamples trainingSample = extractMLFeatures(
            mlSelectorInputs, compressor, cctx, successorGraphs);

    TestTrainData splitData = trainTestSplit(
            trainingSample.features, trainingSample.numericLabels);

    Hyperparams params = defaultXGBoostHyperParams;
    if (trainParams.tuneHyperparams) {
        training::TrainingContext trainingContext = {
            .featureNames    = trainingSample.featureNames,
            .featurePtrNames = trainingSample.featurePtrNames,
            .splitData       = splitData,
            .successorGraphs = successorGraphs,
            .compressor      = &compressor,
        };

        TuningConfig config;
        std::vector<Hyperparams> initialPop =
                generateInitialTuningPop(defaultParamRanges, config);

        auto [tunedResult, defaultResult] = training::tuneHyperparams(
                trainingContext,
                mlSelectorInputs,
                defaultParamRanges,
                initialPop,
                config);

        auto tunedScore = config.compressionWeight * tunedResult.size
                + (1 - config.compressionWeight) * tunedResult.ctime;

        auto defaultScore = config.compressionWeight * defaultResult.size
                + (1 - config.compressionWeight) * defaultResult.ctime;

        if (tunedScore < defaultScore) {
            params = tunedResult.params;
        }
    }

    GBTPredictorWrapper gbtPred =
            trainXGBoostModel(splitData, successors.nbGraphIDs, params);

    GBTModel coreModel = {
        .predictor        = gbtPred.core_predictor_.get(),
        .featureGenerator = FeatureGen_integer,
        .nbSuccessors     = successors.nbGraphIDs,
        .nbFeatures       = trainingSample.featurePtrNames.size(),
        .featureLabels    = trainingSample.featurePtrNames.data(),
    };

    ZL_MLSelectorConfig config = { .model         = ZL_GBT,
                                   .runtimeConfig = &coreModel };

    auto mlSelectorGraphId = unwrap(ZL_MLSelector_registerGraph(
            compressor.get(),
            &config,
            successorGraphs.data(),
            successorGraphs.size()));

    auto newMlSelectorGraphName =
            ZL_Compressor_Graph_getName(compressor.get(), mlSelectorGraphId);

    return graph_mutation::createSharedStringView(
            graph_mutation::renameGraphInCompressor(
                    compressor.serialize(),
                    mlSelectorGraphName,
                    newMlSelectorGraphName));
}
} // namespace openzl::training
