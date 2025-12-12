// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "tools/ml_selector/ml_features.h"
#include <chrono>
#include <cstdio>
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

std::vector<std::string> minSizeChoiceFunc(std::vector<TargetsMap>& targets)
{
    std::vector<std::string> result;
    for (size_t i = 0; i < targets.size(); i++) {
        std::string min_label = "";
        for (const auto& it : targets[i]) {
            if (min_label == ""
                || it.second.at("size") < targets[i].at(min_label).at("size")) {
                min_label = it.first;
            }
        }
        result.push_back(min_label);
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
        const std::vector<std::string>& successorLabels,
        ChoiceFunction choiceFunction)
{
    std::vector<std::string> labels = choiceFunction(samples.targetData);
    std::vector<std::vector<float>> features;
    std::vector<std::string> featureNames;

    // convert features to vector for easier shuffling for train test split
    for (size_t i = 0; i < samples.featureData.size(); i++) {
        const FeatureMap& feature = samples.featureData[i];
        features.emplace_back();
        for (size_t j = 0; j < VECTOR_SIZE(feature); j++) {
            // save features strings
            if (i == 0) {
                featureNames.emplace_back(VECTOR_AT(feature, j).label);
            }
            features.back().push_back(VECTOR_AT(feature, j).value);
        }
    }

    std::unordered_map<std::string, int> labelMap;
    for (size_t i = 0; i < successorLabels.size(); i++) {
        labelMap[successorLabels[i]] = (int)i;
    }

    std::vector<float> numericLabels;
    numericLabels.reserve(labels.size());
    for (const auto& label : labels) {
        numericLabels.push_back(labelMap[label]);
    }

    return {
        .labels        = std::move(labels),
        .numericLabels = std::move(numericLabels),
        .labelMap      = std::move(labelMap),
        .features      = std::move(features),
        .featureNames  = std::move(featureNames),
    };
}

ProcessedMLTrainingSamples extractMLFeatures(
        const std::vector<MultiInput>& inputs,
        Compressor& compressor,
        CCtx& cctx,
        const std::vector<ZL_GraphID>& successorGraphs,
        const std::vector<std::string>& successorLabels,
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
                targets[successorLabels[i]] = {
                    { "size", totalSize }, { "ctime", timeElapsedMS.count() }
                };
            }

            compressor.selectStartingGraph(startingGraphId);
            targetData.push_back(targets);

            FeatureMap features = VECTOR_EMPTY(kMaxVectorSize);
            const ZL_Report report =
                    featureGen(input.get(), &features, nullptr);
            if (ZL_isError(report)) {
                throw std::runtime_error(
                        std::string("Error generating integer features: ")
                        + ZL_ErrorCode_toString(ZL_errorCode(report)));
            }
            featureData.push_back(features);
        }

        MLTrainingSample samples = { .featureData = featureData,
                                     .targetData  = targetData };

        ProcessedMLTrainingSamples results = processTrainingSamples(
                samples, successorLabels, choiceFunction);

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
