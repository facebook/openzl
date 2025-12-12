// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <folly/dynamic.h>
#include <folly/json.h>
#include <gtest/gtest.h>
#include <limits.h>
#include <optional>

#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "tests/fuzz_utils.h"
#include "tools/gbt_predictor/zstrong_gbt_predictor.h"
#include "tools/zstrong_ml.h"

#include "test_zstrong_ml_models.h"

namespace zstrong::ml {
namespace tests {
namespace {

using namespace gbt_predictor;
using namespace zstrong::ml::features;
using namespace openzl::tests;

std::vector<std::string> getConfigExamples()
{
    return { "",
             "{}",
             "[]",
             "[[]]",
             folly::toJson(folly::parseJson(GBT_BINARY_MODEL)["predictor"]),
             folly::toJson(folly::parseJson(GBT_MULTICLASS_MODEL)["predictor"])

    };
}

FUZZ(MLTest, FuzzGBTPredictorConfiguration)
{
    std::string const config =
            d_str().with_examples(getConfigExamples()).gen("config", f);
    try {
        auto predictor = GBTPredictor(std::string_view(config));
        // Use fuzzer to generate a vector of floats
        // Use the predictor to make predictions on some random data
        std::vector<float> features =
                gen_vec<float>(f, "config", ShortInputLengthInElts());
        ;
        // We predict in the try block as we will throw if we don't have enough
        // features
        ASSERT_LT(
                predictor.predict(features),
                INT_MAX); // Just add an assert to make sure we don't optimize
                          // the code out
    } catch (...) {
        // We couldn't construct but didn't crash
        return;
    }
}

template <typename T>
folly::dynamic vecToArray(std::vector<T> const& v)
{
    folly::dynamic array = folly::dynamic::array;
    for (auto const& val : v) {
        array.push_back(val);
    }
    return array;
}

template <typename Mode>
folly::dynamic generateTreeIndicesVector(
        StructuredFDP<Mode>& f,
        std::string const& name,
        std::vector<bool> const& leafMask)
{
    std::vector<int> indices;
    int idx = 0;
    for (auto const& leaf : leafMask) {
        if (leaf) {
            indices.push_back(-1);
        } else {
            indices.push_back(
                    f.u32_range(name.c_str(), idx + 1, leafMask.size()));
        }
        idx++;
    }
    return vecToArray(indices);
}

template <typename Mode>
folly::dynamic generateFeaturesIndicesVector(
        StructuredFDP<Mode>& f,
        std::vector<bool> const& leafMask,
        size_t nbFeatures)
{
    std::vector<int> indices;
    for (auto const& leaf : leafMask) {
        if (leaf) {
            indices.push_back(-1);
        } else {
            indices.push_back(f.u32_range("featureIdx", 0, nbFeatures - 1));
        }
    }
    return vecToArray(indices);
}

template <typename Mode>
folly::dynamic generateGBTTreeConfiguration(
        StructuredFDP<Mode>& f,
        size_t nbFeatures)
{
    folly::dynamic tree = folly::dynamic::object;
    size_t const nodes  = f.usize_range("nodes", 1, 500);
    std::vector<bool> leafMask =
            gen_vec<bool>(f, "leaf_mask", Const<std::size_t>(nodes - 1));
    leafMask.push_back(true);
    tree["featureIdx"] = generateFeaturesIndicesVector(f, leafMask, nbFeatures);
    tree["leftChildIdx"] =
            generateTreeIndicesVector(f, "leftChildIdx", leafMask);
    tree["rightChildIdx"] =
            generateTreeIndicesVector(f, "rightChildIdx", leafMask);
    tree["defaultLeft"] = vecToArray(f.vec_args(
            "defaultLeft", d_range(0, 1), Const<std::size_t>(nodes)));
    tree["value"] =
            vecToArray(gen_vec<float>(f, "value", Const<std::size_t>(nodes)));
    return tree;
}

template <typename Mode>
folly::dynamic generateGBTForestConfiguration(
        StructuredFDP<Mode>& f,
        size_t nbFeatures)
{
    folly::dynamic forest = folly::dynamic::array;
    size_t const trees    = f.usize_range("forests", 0, 600);
    for (size_t i = 0; i < trees; ++i) {
        forest.push_back(generateGBTTreeConfiguration(f, nbFeatures));
    }
    return forest;
}
FUZZ(MLTest, FuzzGBTPredictorPredict)
{
    folly::dynamic config   = folly::dynamic::array;
    size_t const nbForests  = f.usize_range("nbForests", 1, 32);
    size_t const nbFeatures = f.usize_range("nbFeatures", 1, 500);
    for (size_t i = 0; i < nbForests; ++i) {
        config.push_back(generateGBTForestConfiguration(f, nbFeatures));
    }
    std::optional<GBTPredictor> maybe_predictor = std::nullopt;
    try {
        maybe_predictor = GBTPredictor(config);
    } catch (...) {
        // We couldn't construct but didn't crash
        return;
    }
    // Use fuzzer to generate a vector of floats
    ASSERT_TRUE(maybe_predictor.has_value());
    // // Use the predictor to make predictions on some random data
    std::vector<float> features =
            gen_vec<float>(f, "config", Const<std::size_t>(nbFeatures));
    ;
    ASSERT_LT(
            maybe_predictor.value().predict(features),
            INT_MAX); // Just add an assert to make sure we don't optimize the
                      // code out
}

template <typename Mode>
void fuzzFeatureGenerator(StructuredFDP<Mode>& f, FeatureGenerator& fgen)
{
    FeatureMap fmap       = {};
    size_t const eltWidth = f.choices("elt_width", { 1, 2, 4, 8 });
    ZL_Type const stType  = f.choices(
            "stram_type",
            {
                    ZL_Type_serial,
                    ZL_Type_struct,
                    ZL_Type_numeric,
            });
    auto data = gen_str(f, "data", InputLengthInBytes(eltWidth));
    fgen.getFeatures(
            fmap, data.data(), stType, eltWidth, data.size() / eltWidth);
    ASSERT_LT(fmap.size(), 100000);
}

FUZZ(MLTest, FuzzFeatureGeneatros_IntFeaturesGenerator)
{
    auto fgen = IntFeatureGenerator();
    fuzzFeatureGenerator(f, dynamic_cast<FeatureGenerator&>(fgen));
}

FUZZ(MLTest, FuzzFeatureGeneatros_DeltaIntFeaturesGenerator)
{
    auto fgen = DeltaIntFeatureGenerator();
    fuzzFeatureGenerator(f, dynamic_cast<FeatureGenerator&>(fgen));
}

FUZZ(MLTest, FuzzFeatureGeneatros_TokenizeIntFeaturesGenerator)
{
    auto fgen = TokenizeIntFeatureGenerator();
    fuzzFeatureGenerator(f, dynamic_cast<FeatureGenerator&>(fgen));
}

} // namespace
} // namespace tests
} // namespace zstrong::ml
