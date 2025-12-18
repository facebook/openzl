// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <folly/base64.h>
#include <folly/dynamic.h>
#include <folly/json.h>

#include "tools/zstrong_cpp.h"
#include "tools/zstrong_ml.h"

#include "openzl/common/assertion.h"
#include "openzl/compress/selectors/ml/features.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"
#include "openzl/zl_data.h"

const size_t kMaxVectorSize = 1024;
namespace zstrong {
namespace ml {

namespace {
std::vector<std::string> getStringsArrayFromJsonObject(
        folly::dynamic const& object,
        std::string_view fieldName)
{
    if (!object.isObject()) {
        throw std::runtime_error("Invalid JSON format");
    }
    const auto field = object[fieldName];
    if (!field.isArray()) {
        throw std::runtime_error("Invalid JSON format");
    }
    std::vector<std::string> values;
    values.reserve(field.size());
    for (auto const& v : field) {
        values.push_back(v.asString());
    }
    return values;
}

std::vector<std::string> getLabelsFromJson(folly::dynamic const& model)
{
    return getStringsArrayFromJsonObject(model, "labels");
}

std::vector<std::string> getFeaturesFromJson(folly::dynamic const& model)
{
    return getStringsArrayFromJsonObject(model, "features");
}

std::vector<Label> getLabelsFromStrings(
        const std::vector<std::string>& label_strs)
{
    std::vector<Label> labels;
    for (const auto& label_str : label_strs) {
        labels.push_back(label_str.data());
    }
    return labels;
}

gbt_predictor::GBTPredictor getPredictorFromJson(folly::dynamic const& model)
{
    if (!model.isObject()) {
        throw std::runtime_error("Invalid JSON format");
    }
    return gbt_predictor::GBTPredictor(model["predictor"]);
}

} // namespace

GBTModel::GBTModel(folly::dynamic const& model)
        : labels_str_(getLabelsFromJson(model)),
          labels_(getLabelsFromStrings(labels_str_)),
          features_str_(getFeaturesFromJson(model)),
          features_(getLabelsFromStrings(features_str_)),
          predictor_(getPredictorFromJson(model))
{
    if (predictor_.getNumClasses() != labels_str_.size()) {
        throw std::runtime_error(
                "Invalid JSON format - labels and classes mismatch");
    }
}

GBTModel::GBTModel(std::string_view model) : GBTModel(folly::parseJson(model))
{
}

Label GBTModel::predict(const ZL_Input* input, const FeatureGenerator* fgen)
        const
{
    FeatureMap featuresMap;
    fgen->getFeatures(featuresMap, input);
    size_t classIdx = predict(featuresMap);
    if (classIdx >= labels_.size()) {
        return "";
    }
    return labels_[classIdx];
}

size_t GBTModel::predict(const FeatureMap& featuresMap) const
{
    // Prep features in order
    std::vector<float> featuresData;
    for (const std::string& f : features_) {
        if (featuresMap.contains(f))
            featuresData.push_back(featuresMap.at(f));
        else
            featuresData.push_back(std::numeric_limits<float>::quiet_NaN());
    }
    return predictor_.predict(featuresData);
}
namespace features {

namespace {

void calcIntegerFeatures(
        FeatureMap& featuresMap,
        const void* data,
        size_t eltWidth,
        size_t nbElts)
{
    ZL_Input* stream = ZL_TypedRef_createNumeric(data, eltWidth, nbElts);
    VECTOR(LabeledFeature) features = VECTOR_EMPTY(kMaxVectorSize);
    const ZL_Report report          = FeatureGen_integer(stream, &features);
    for (size_t i = 0; i < VECTOR_SIZE(features); i++) {
        auto const& feature        = VECTOR_AT(features, i);
        featuresMap[feature.label] = feature.value;
    }

    VECTOR_DESTROY(features);
    ZL_TypedRef_free(stream);
    ZL_REQUIRE_SUCCESS(report);
}

static inline void*
createDeltas(const void* data, size_t eltWidth, size_t nbElts)
{
    void* deltas = malloc(eltWidth * (nbElts - 1));
    ZL_REQUIRE_NN(deltas);
    uint64_t previous;
    memcpy(&previous, data, eltWidth);
    for (size_t i = 1; i < nbElts; i++) {
        uint64_t current;
        memcpy(&current, (const uint8_t*)data + eltWidth * i, eltWidth);
        const uint64_t delta = current - previous;
        memcpy((uint8_t*)deltas + eltWidth * (i - 1), &delta, eltWidth);
        previous = current;
    }
    return deltas;
}

void calcIntegerDeltaFeatures(
        FeatureMap& features,
        const void* data,
        size_t eltWidth,
        size_t nbElts)
{
    if (nbElts <= 1)
        return;
    void* deltas;
    switch (eltWidth) {
        case 1:
            deltas = createDeltas(data, 1, nbElts);
            break;
        case 2:
            deltas = createDeltas(data, 2, nbElts);
            break;
        case 4:
            deltas = createDeltas(data, 4, nbElts);
            break;
        case 8:
            deltas = createDeltas(data, 8, nbElts);
            break;
        default:
            ZL_REQUIRE_FAIL("Unexpected eltWidth");
    }
    FeatureMap deltaFeatures;

    calcIntegerFeatures(deltaFeatures, deltas, eltWidth, nbElts - 1);

    for (auto const& [featureName, featureValue] : deltaFeatures) {
        features["delta_" + featureName] = featureValue;
    }

    free(deltas);
}
} // namespace

void IntFeatureGenerator::getFeatures(
        FeatureMap& featuresMap,
        const void* data,
        ZL_Type type,
        size_t eltWidth,
        size_t nbElts) const
{
    (void)type;
    calcIntegerFeatures(featuresMap, data, eltWidth, nbElts);
}

std::unordered_set<std::string> IntFeatureGenerator::getFeatureNames() const
{
    return { "nbElts",
             "eltWidth",
             "cardinality",
             "cardinality_upper",
             "cardinality_lower",
             "range_size",
             "mean",
             "variance",
             "stddev",
             "skewness",
             "kurtosis" };
}

void DeltaIntFeatureGenerator::getFeatures(
        FeatureMap& featuresMap,
        const void* data,
        ZL_Type type,
        size_t eltWidth,
        size_t nbElts) const
{
    (void)type;
    calcIntegerFeatures(featuresMap, data, eltWidth, nbElts);
    calcIntegerDeltaFeatures(featuresMap, data, eltWidth, nbElts);
}

std::unordered_set<std::string> DeltaIntFeatureGenerator::getFeatureNames()
        const
{
    return {
        "nbElts",
        "eltWidth",
        "cardinality",
        "cardinality_upper",
        "cardinality_lower",
        "range_size",
        "mean",
        "variance",
        "stddev",
        "skewness",
        "kurtosis",
        "delta_nbElts",
        "delta_eltWidth",
        "delta_cardinality",
        "delta_cardinality_upper",
        "delta_cardinality_lower",
        "delta_range_size",
        "delta_mean",
        "delta_variance",
        "delta_stddev",
        "delta_skewness",
        "delta_kurtosis",
    };
}

void TokenizeIntFeatureGenerator::getFeatures(
        FeatureMap& featuresMap,
        const void* data,
        ZL_Type type,
        size_t eltWidth,
        size_t nbElts) const
{
    (void)type;
    calcIntegerFeatures(featuresMap, data, eltWidth, nbElts);
    calcIntegerDeltaFeatures(featuresMap, data, eltWidth, nbElts);

    // Add tokenize estimate
    {
        auto cardEstimateUpperBound = featuresMap["cardinality_upper"];
        auto const tokenizeEstimatedAlphabetSize =
                cardEstimateUpperBound * eltWidth;
        auto const tokenizeEstimatedIndicesSize =
                nbElts * (size_t)ZL_nextPow2(cardEstimateUpperBound) / 8;
        auto const tokenizeEstimatedUpperBounds =
                tokenizeEstimatedAlphabetSize + tokenizeEstimatedIndicesSize;

        featuresMap["tokenize_estimated_size"] =
                (double)tokenizeEstimatedUpperBounds;
        featuresMap["tokenize_estimated_size_ratio"] =
                (double)tokenizeEstimatedUpperBounds
                / (double)(nbElts * eltWidth);
    }
}

std::unordered_set<std::string> TokenizeIntFeatureGenerator::getFeatureNames()
        const
{
    return { "nbElts",
             "eltWidth",
             "cardinality",
             "cardinality_upper",
             "cardinality_lower",
             "range_size",
             "mean",
             "variance",
             "stddev",
             "skewness",
             "kurtosis" };
}

} // namespace features

ZL_GraphID MLSelector::select(
        ZL_Selector const*,
        ZL_Input const* input,
        std::span<ZL_GraphID const> successors) const
{
    if (labelsIdx_.size()) {
        std::string predictedLabel =
                model_.get()->predict(input, featureGenerator_.get());
        size_t predictedIdx = labelsIdx_.at(predictedLabel);
        return successors[predictedIdx];
    } else {
        FeatureMap features;
        featureGenerator_.get()->getFeatures(features, input);
        auto prediction = model_.get()->predict(features);
        return successors[prediction];
    }
}

ZL_GraphID MLTrainingSelector::select(
        ZL_Selector const* selCtx,
        ZL_Input const* input,
        std::span<ZL_GraphID const> successors) const
{
    if (successors.size() != labels_.size()) {
        throw std::runtime_error(
                "Number of successors doesn't match number of labels");
    }
    TargetsMap targets;
    ZL_GraphID best = ZL_GRAPH_STORE;
    auto const storeResult =
            ZL_Selector_tryGraph(selCtx, input, ZL_GRAPH_STORE);
    size_t const storeSize = ZL_isError(storeResult.finalCompressedSize)
            ? ZL_Input_numElts(input) * ZL_Input_eltWidth(input)
            : ZL_validResult(storeResult.finalCompressedSize);
    size_t bestSize        = storeSize;
    for (size_t i = 0; i < successors.size(); i++) {
        auto successor        = successors[i];
        auto label            = labels_[i];
        auto const timerStart = std::chrono::steady_clock::now();
        auto const result     = ZL_Selector_tryGraph(selCtx, input, successor);
        std::chrono::duration<double, std::milli> const timeElapsedMS =
                (std::chrono::steady_clock::now() - timerStart);
        size_t const size = ZL_isError(result.finalCompressedSize)
                ? storeSize * 1.1 // We don't want a failure to perform
                                  // worse than a successful store
                : ZL_validResult(result.finalCompressedSize);
        if (size < bestSize) {
            bestSize = size;
            best     = successor;
        }
        targets[label] = { { "size", size },
                           { "ctime", timeElapsedMS.count() } };
    }
    collectSample(input, std::move(targets));
    return best;
}

void MLTrainingSelector::collectSample(ZL_Input const* data, TargetsMap targets)
        const
{
    std::optional<MLTrainingSampleData> inputData = std::nullopt;
    if (collectInputs_) {
        size_t eltWidth       = ZL_Input_eltWidth(data);
        size_t nbElts         = ZL_Input_numElts(data);
        size_t bufferSize     = eltWidth * nbElts;
        const uint8_t* buffer = (const uint8_t*)ZL_Input_ptr(data);
        inputData             = MLTrainingSampleData{ .data = std::vector(
                                                  buffer, buffer + bufferSize),
                                                      .eltWidth   = eltWidth,
                                                      .streamType = ZL_Input_type(data) };
    }
    FeatureMap fmap;
    if (featureGenerator_) {
        featureGenerator_->getFeatures(
                fmap,
                ZL_Input_ptr(data),
                ZL_Input_type(data),
                ZL_Input_eltWidth(data),
                ZL_Input_numElts(data));
    }
    MLTrainingSample sample{ std::move(inputData),
                             std::move(fmap),
                             std::move(targets) };
    collectSample(std::move(sample));
}

void MemMLTrainingSelector::collectSample(MLTrainingSample&& sample) const
{
    results_.wlock()->emplace_back(std::move(sample));
}

std::string MemMLTrainingSelector::getCollectedJson() const
{
    return results_.withRLock(MLTrainingSamplesToJson);
}

std::vector<MLTrainingSample> MemMLTrainingSelector::getCollected() const
{
    return results_.copy();
}

size_t MemMLTrainingSelector::getCollectedSize() const
{
    return results_.rlock()->size();
}

std::vector<MLTrainingSample> MemMLTrainingSelector::flushCollected() const
{
    folly::Synchronized<std::vector<MLTrainingSample>> results2;
    results_.swap(results2);
    return std::move(*results2.wlock());
}

void MemMLTrainingSelector::clearCollected()
{
    results_ = std::vector<MLTrainingSample>{};
}

void FileMLTrainingSelector::collectSample(MLTrainingSample&& sample) const
{
    output_.withWLock([this, &sample](std::ofstream& out) {
        bool expected = true;
        if (!firstSample_.compare_exchange_strong(expected, false)) {
            out << ",\n";
        }
        out << sample.toJson();
    });
}

folly::dynamic MLTrainingSampleData::toDynamic() const
{
    folly::dynamic dyndata = folly::dynamic::object;
    dyndata["eltWidth"]    = eltWidth;
    dyndata["streamType"]  = (int)streamType;
    {
        std::string_view dataSV(
                reinterpret_cast<const char*>(data.data()), data.size());
        dyndata["b64data"] = folly::base64Encode(dataSV);
    }
    return dyndata;
}

MLTrainingSample::MLTrainingSample(folly::dynamic const& dynamic)
{
    {
        // Data
        const folly::dynamic& dyndata = dynamic.getDefault("data", nullptr);
        if (dyndata.isObject()) {
            const auto data_ =
                    folly::base64Decode(dyndata["b64data"].asString());
            data = MLTrainingSampleData{
                .data       = { data_.begin(), data_.end() },
                .eltWidth   = (size_t)dyndata["eltWidth"].asInt(),
                .streamType = (ZL_Type)dyndata["streamType"].asInt(),
            };
        }
    }

    {
        // Targets
        folly::dynamic dyntargets = dynamic["targets"];
        for (auto const& dyntarget : dyntargets.items()) {
            auto const& label = dyntarget.first.asString();
            targets[label]    = {};
            for (auto const& dynmetric : dyntarget.second.items()) {
                targets[label][dynmetric.first.asString()] =
                        dynmetric.second.asDouble();
            }
        }
    }

    {
        // Features
        folly::dynamic dynfeatures = dynamic["features"];
        for (auto const& dynfeature : dynfeatures.items()) {
            features[dynfeature.first.asString()] =
                    dynfeature.second.asDouble();
        }
    }
}

folly::dynamic MLTrainingSample::toDynamic() const
{
    folly::dynamic res = folly::dynamic::object;
    {
        // Input data
        if (data)
            res["data"] = (*data).toDynamic();
    }

    {
        // Targets
        folly::dynamic dyntargets = folly::dynamic::object;
        for (auto const& label : targets) {
            folly::dynamic labeldyn = folly::dynamic::object;
            for (auto const& metric : label.second) {
                labeldyn[metric.first] = metric.second;
            }
            dyntargets[label.first] = labeldyn;
        }
        res["targets"] = dyntargets;
    }

    {
        // Features
        folly::dynamic dynfeatures = folly::dynamic::object;
        for (auto const& feature : features) {
            if (std::isfinite(feature.second))
                // save value as dynamic double
                dynfeatures[feature.first] = folly::dynamic(feature.second);
        }
        res["features"] = dynfeatures;
    }
    return res;
}

std::string MLTrainingSample::toJson() const
{
    folly::json::serialization_opts opts;
    opts.dtoa_flags = folly::DtoaFlags::EMIT_TRAILING_DECIMAL_POINT
            | folly::DtoaFlags::EMIT_TRAILING_ZERO_AFTER_POINT;

    return folly::json::serialize(toDynamic(), opts);
}

std::vector<MLTrainingSample> MLTrainingSamplesFromJson(std::string_view json)
{
    std::vector<MLTrainingSample> res;
    auto const dynamic = folly::parseJson(json);
    if (!dynamic.isArray()) {
        throw std::runtime_error("Cannot parse, expected array");
    }
    for (const auto& dynsample : dynamic) {
        res.emplace_back(dynsample);
    }
    return res;
}

std::string MLTrainingSamplesToJson(
        const std::vector<MLTrainingSample>& samples)
{
    folly::dynamic array = folly::dynamic::array;
    for (auto const& sample : samples) {
        array.push_back(sample.toDynamic());
    }

    folly::json::serialization_opts opts;
    opts.dtoa_flags = folly::DtoaFlags::EMIT_TRAILING_DECIMAL_POINT
            | folly::DtoaFlags::EMIT_TRAILING_ZERO_AFTER_POINT;
    return folly::json::serialize(array, opts);
}

} // namespace ml

} // namespace zstrong
