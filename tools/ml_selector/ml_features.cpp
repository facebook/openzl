// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "tools/ml_selector/ml_features.h"
#include <chrono>
#include <cstdio>
#include <queue>
#include <string>
#include <vector>
#include "openzl/zl_reflection.h"
#include "tools/logger/Logger.h"

namespace openzl::training {

using tools::logger::ERRORS;
using tools::logger::Logger;
const size_t kMaxVectorSize = 1024;

namespace {
struct MLTrainingSample {
    std::vector<FeatureMap> featureData;
    std::vector<TargetsMap> targetData;
};
} // namespace

static size_t compress(CCtx& cctx, Compressor& compressor, const Input& input)
{
    cctx.refCompressor(compressor);
    return cctx.compressOne(input).size();
}

ChoiceFunction makeWeightedChoiceFunc(float weight)
{
    return [weight](std::vector<TargetsMap>& targets) {
        std::vector<float> result;
        for (size_t i = 0; i < targets.size(); i++) {
            std::priority_queue<
                    std::pair<float, size_t>,
                    std::vector<std::pair<float, size_t>>,
                    std::greater<std::pair<float, size_t>>>
                    pq;
            for (const auto& it : targets[i]) {
                float score = weight * it.second.at("size")
                        + (1 - weight) * it.second.at("ctime");
                pq.emplace(score, it.first);
            }
            result.push_back((float)pq.top().second);
        }
        return result;
    };
}

std::vector<float> minSizeChoiceFunc(std::vector<TargetsMap>& targets)
{
    std::vector<float> result;
    for (size_t i = 0; i < targets.size(); i++) {
        size_t min_ind = targets[i].begin()->first;
        for (const auto& it : targets[i]) {
            if (it.second.at("size") < targets[i].at(min_ind).at("size")) {
                min_ind = it.first;
            }
        }
        result.push_back((float)min_ind);
    }

    return result;
}

/**
 * Parsing samples to get label by extracting "best" successor through choice
 * function. Convert features to vector for easier shuffling.
 * @returns ProcessedMLTrainingSamples containing labels and features data
 */
static ProcessedMLTrainingSamples processTrainingSamples(
        MLTrainingSample& samples,
        ChoiceFunction choiceFunction)
{
    std::vector<float> numericLabels = choiceFunction(samples.targetData);
    std::vector<std::vector<float>> features;
    std::vector<std::string> featureNames;
    std::vector<const char*> featurePtrNames;

    if (!samples.featureData.empty()) {
        size_t numFeatures = VECTOR_SIZE(samples.featureData[0]);
        featureNames.reserve(numFeatures);
        featurePtrNames.reserve(numFeatures);
    }

    // convert features to vector for easier shuffling for train test split
    for (size_t i = 0; i < samples.featureData.size(); i++) {
        const FeatureMap& feature = samples.featureData[i];
        features.emplace_back();
        for (size_t j = 0; j < VECTOR_SIZE(feature); j++) {
            // save features strings
            if (i == 0) {
                featureNames.emplace_back(VECTOR_AT(feature, j).label);
                featurePtrNames.emplace_back(featureNames.back().c_str());
            }
            features.back().push_back(VECTOR_AT(feature, j).value);
        }
    }

    return { .numericLabels   = std::move(numericLabels),
             .features        = std::move(features),
             .featureNames    = std::move(featureNames),
             .featurePtrNames = std::move(featurePtrNames) };
}

ProcessedMLTrainingSamples extractMLFeatures(
        const std::vector<MultiInput>& inputs,
        Compressor& compressor,
        CCtx& cctx,
        const std::vector<ZL_GraphID>& successorGraphs,
        FeatureGenerator featureGen,
        ChoiceFunction choiceFunction)
{
    std::vector<FeatureMap> featureData;
    std::vector<TargetsMap> targetData;

    try {
        for (auto& multiInput : inputs) {
            TargetsMap targets;
            if (multiInput->size() != 1) {
                throw std::runtime_error(
                        "ML Selector only supports single input");
            }
            auto& input = multiInput->front();
            if (input.type() != Type::Numeric) {
                throw std::runtime_error(
                        "ML Selector only supports integer inputs");
            }
            ZL_GraphID startingGraphId;
            if (!ZL_Compressor_getStartingGraphID(
                        compressor.get(), &startingGraphId)) {
                throw std::runtime_error("Error finding starting graph");
            }
            for (size_t i = 0; i < successorGraphs.size(); ++i) {
                compressor.selectStartingGraph(successorGraphs[i]);

                auto const timerStart = std::chrono::steady_clock::now();
                size_t totalSize      = compress(cctx, compressor, input);

                std::chrono::duration<double, std::milli> const timeElapsedMS =
                        (std::chrono::steady_clock::now() - timerStart);
                targets[i] = { { "size", totalSize },
                               { "ctime", timeElapsedMS.count() } };
            }

            compressor.selectStartingGraph(startingGraphId);
            targetData.push_back(targets);

            FeatureMap features    = VECTOR_EMPTY(kMaxVectorSize);
            const ZL_Report report = featureGen(input.get(), &features);
            if (ZL_isError(report)) {
                throw std::runtime_error(
                        std::string("Error generating integer features: ")
                        + ZL_ErrorCode_toString(ZL_errorCode(report)));
            }
            featureData.push_back(features);
        }

        MLTrainingSample samples = { .featureData = featureData,
                                     .targetData  = targetData };

        ProcessedMLTrainingSamples results =
                processTrainingSamples(samples, choiceFunction);

        // free memory since we already copied into vector
        for (auto& feature : featureData) {
            VECTOR_DESTROY(feature);
        }

        return results;
    } catch (const std::exception& e) {
        // free memory if there is a exception
        for (auto& feature : featureData) {
            VECTOR_DESTROY(feature);
        }
        (void)e;
        throw;
    }
}

} // namespace openzl::training
