// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <folly/base64.h>
#include <folly/dynamic.h>

#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_reflection.h"
#include "tools/zstrong_ml.h" // @manual

#include "test_zstrong_ml_models.h"

using namespace ::testing;

namespace zstrong::ml {
namespace {
std::shared_ptr<GBTModel> getBinaryGBTModel()
{
    return std::make_shared<GBTModel>(GBT_BINARY_MODEL);
}

std::shared_ptr<GBTModel> getMulticlassGBTModel()
{
    return std::make_shared<GBTModel>(GBT_MULTICLASS_MODEL);
}

} // namespace

TEST(ZstrongMLTest, BinaryGBTModelTest)
{
    auto model = getBinaryGBTModel();
    for (int a = 0; a <= 1; a++) {
        for (int b = 0; b <= 1; b++) {
            FeatureMap features;
            features["a"]    = (float)a;
            features["b"]    = (float)b;
            size_t predicted = model->predict(features);
            // The model predicts the function `a & (b ^ 1)`
            ASSERT_EQ(predicted, (a & (b ^ 1)));
        }
    }
}

TEST(ZstrongMLTest, MulticlassGBTModelTest)
{
    auto model = getMulticlassGBTModel();
    for (int a = 0; a <= 2; a++) {
        for (int b = 0; b <= 2; b++) {
            for (int c = 0; c <= 2; c++) {
                FeatureMap features;
                features["a"]          = (float)a;
                features["b"]          = (float)b;
                features["c"]          = (float)c;
                const size_t predicted = model->predict(features);
                // The model predicts the function `(a+b+c)%3`
                ASSERT_EQ((a + b + c) % 3, predicted);
            }
        }
    }
}

TEST(ZstrongMLTest, BinaryMLSelectorTest)
{
    auto test = [](std::vector<ZL_GraphID> successors) {
        class TestFeatureGenerator : public FeatureGenerator {
           public:
            TestFeatureGenerator() : FeatureGenerator(getFeatureNames()) {}
            void getFeatures(
                    FeatureMap& featuresMap,
                    const void* data,
                    ZL_Type type,
                    size_t eltWidth,
                    size_t nbElts) const override
            {
                EXPECT_EQ(eltWidth, 1);
                EXPECT_GE(nbElts, 1);
                EXPECT_EQ(type, ZL_Type_serial);

                uint8_t const* buffer = (uint8_t const*)data;
                featuresMap["a"]      = buffer[0] == '0' ? 0 : 1;
                featuresMap["b"]      = buffer[1] == '0' ? 0 : 1;
            }

            std::unordered_set<std::string> getFeatureNames() const override
            {
                return { "a", "b" };
            }
        };
        auto cgraph = CGraph();

        auto const graphID = [&]() -> ZL_GraphID {
            // We define a scope here to make sure we handle object ownership
            // and memory correctly
            auto model            = getBinaryGBTModel();
            auto featureGenerator = std::make_shared<TestFeatureGenerator>();
            auto selector         = std::make_unique<MLSelector>(
                    ZL_Type_serial, model, featureGenerator);

            return registerOwnedSelector(
                    *cgraph,
                    std::move(selector),
                    successors,
                    {},
                    "!MyMLSelector");
        }();

        ASSERT_EQ(
                std::string(ZL_Compressor_Graph_getName(cgraph.get(), graphID)),
                "MyMLSelector");

        ASSERT_FALSE(ZL_isError(
                ZL_Compressor_selectStartingGraphID(cgraph.get(), graphID)));

        size_t const fse_compressed_size =
                cgraph.compress(std::string("00") + std::string(10000, 'a'))
                        .size();
        ASSERT_LT(fse_compressed_size, 1000);

        size_t const store_compressed_size =
                cgraph.compress(std::string("10") + std::string(10000, 'a'))
                        .size();
        ASSERT_GT(store_compressed_size, 10000);
    };
    test({ ZL_GRAPH_FSE, ZL_GRAPH_STORE });
}

TEST(ZstrongMLTest, MemTrainingCollectorTest)
{
    auto test = [](std::vector<std::string> inputs) {
        std::vector<std::string> successorLabels = {
            "store", "fse", "huff", "zstd"
        };
        std::vector<ZL_GraphID> successorGraphs = {
            ZL_GRAPH_STORE, ZL_GRAPH_FSE, ZL_GRAPH_HUFFMAN, ZL_GRAPH_ZSTD
        };
        ASSERT_EQ(successorLabels.size(), successorGraphs.size()); // sanity
        auto selector = MemMLTrainingSelector(
                ZL_Type_serial,
                successorLabels,
                true,
                std::make_shared<features::IntFeatureGenerator>());
        auto cgraph = CGraph();
        auto const graphID =
                selector.registerSelector(*cgraph, successorGraphs);
        ASSERT_FALSE(ZL_isError(
                ZL_Compressor_selectStartingGraphID(cgraph.get(), graphID)));
        for (auto const& inp : inputs) {
            auto compressed = cgraph.compress(inp);
            ASSERT_EQ(inp, decompress(compressed));
        }

        // Make sure that we can serialize and unserialize correctly
        const auto collectedJson    = selector.getCollectedJson();
        const auto collectedSamples = MLTrainingSamplesFromJson(collectedJson);
        ASSERT_EQ(collectedSamples.size(), inputs.size());

        for (auto inpi = 0; inpi < inputs.size(); inpi++) {
            const auto& collected     = collectedSamples[inpi];
            const auto& collectedData = *(collected.data);
            ASSERT_EQ(collectedData.eltWidth, 1);
            ASSERT_EQ(collectedData.streamType, ZL_Type_serial);
            std::string_view dataSV(
                    reinterpret_cast<const char*>(collectedData.data.data()),
                    collectedData.data.size());
            ASSERT_EQ(dataSV, inputs[inpi]);

            const auto targets = collected.targets;
            ASSERT_EQ(targets.size(), successorGraphs.size());
            for (auto s = 0; s < successorGraphs.size(); s++) {
                const auto& target = targets.at(successorLabels[s]);
                // @note This test implies it knows the frame format,
                // which is a moving target. This test is brittle.
                const size_t checksumSize =
                        8 + (ZL_MAX_FORMAT_VERSION >= ZL_CHUNK_VERSION_MIN);
                const auto expectedSize =
                        compress(inputs[inpi], successorGraphs[s]).size()
                        - checksumSize; // we substract checksumSize because we
                                        // don't want to include checksums in
                                        // calculation
                ASSERT_EQ(target.at("size"), (float)expectedSize);
            }

            const auto& features = collected.features;
            ASSERT_EQ(features.at("nbElts"), inputs[inpi].size());
        }
    };
    test({ "1234567890",
           "123456789011111111111",
           "dawdawdawfergferfwirh23irfweifbhiauyfhgeiu" });
    test({ "" });
    test({ "1" });
}

void assertExpectedErrorRate(double val1, double val2)
{
    EXPECT_NEAR(val1, val2, abs(val1) * 1e-6);
}

template <typename Int>
void TestTypedMoments()
{
    std::vector<Int> data = { 1, 1, 1, 1, 0, 1, 2, 3 };
    features::IntFeatureGenerator generator;
    FeatureMap features;
    generator.getFeatures(
            features,
            data.data(),
            ZL_Type_numeric,
            sizeof(data[0]),
            data.size());

    // clang-format off
    /*
    Given an numpy array named `data`, we can generate the following in python using:
    ```
    print(f'assertExpectedErrorRate(features["mean"], {np.mean(data)});')
    print(f'assertExpectedErrorRate(features["variance"], {np.var(data, ddof=1)});')
    print(f'assertExpectedErrorRate(features["stddev"], {np.std(data, ddof=1)});')
    print(f'assertExpectedErrorRate(features["skewness"], {scipy.stats.skew(data)});')
    print(f'assertExpectedErrorRate(features["kurtosis"], {scipy.stats.kurtosis(data)});')
    ```
    */
    // clang-format on
    assertExpectedErrorRate(features["nbElts"], data.size());
    assertExpectedErrorRate(features["eltWidth"], sizeof(data[0]));
    assertExpectedErrorRate(features["cardinality"], 4);
    assertExpectedErrorRate(
            features["cardinality_upper"],
            (std::is_same<Int, uint8_t>::value) ? 4 : 5);
    assertExpectedErrorRate(features["cardinality_lower"], 4);
    assertExpectedErrorRate(features["range_size"], 3);
    assertExpectedErrorRate(features["mean"], 1.25);
    assertExpectedErrorRate(features["variance"], 0.7857142857142857);
    assertExpectedErrorRate(features["stddev"], 0.8864052604279183);
    assertExpectedErrorRate(features["skewness"], 0.8223036670302644);
    assertExpectedErrorRate(features["kurtosis"], 0.2148760330578514);
}

TEST(ZstrongMLTest, TestMoments8)
{
    TestTypedMoments<uint8_t>();
}
TEST(ZstrongMLTest, TestMoments16)
{
    TestTypedMoments<uint16_t>();
}
TEST(ZstrongMLTest, TestMoments32)
{
    TestTypedMoments<uint32_t>();
}
TEST(ZstrongMLTest, TestMoments64)
{
    TestTypedMoments<uint64_t>();
}

TEST(ZstrongMLTest, TestMomentsStableLarge)
{
    std::vector<uint64_t> data(1 << 24, (uint64_t)-1);
    data.push_back(0);
    data.push_back(1);
    data.push_back(2);
    data.push_back(3);

    features::IntFeatureGenerator generator;
    FeatureMap features;
    generator.getFeatures(
            features,
            data.data(),
            ZL_Type_numeric,
            sizeof(data[0]),
            data.size());

    assertExpectedErrorRate(features["nbElts"], data.size());
    assertExpectedErrorRate(features["eltWidth"], sizeof(data[0]));
    assertExpectedErrorRate(features["cardinality"], 5);
    assertExpectedErrorRate(features["cardinality_upper"], 6);
    assertExpectedErrorRate(features["cardinality_lower"], 5);
    assertExpectedErrorRate(features["range_size"], 1.84467440737095e+19);
    assertExpectedErrorRate(features["mean"], 1.844673967566409e+19);
    assertExpectedErrorRate(features["variance"], 8.11296045646944e+31);
    assertExpectedErrorRate(features["stddev"], 9007197375693196.0);
    assertExpectedErrorRate(features["skewness"], -2047.99951171875);
    assertExpectedErrorRate(features["kurtosis"], 4194300.0000002384);
}

TEST(ZstrongMLTest, TestMomentsStableSmall)
{
    std::vector<uint64_t> data(1 << 24, 1);
    data.push_back(0);
    data.push_back(1);
    data.push_back(2);
    data.push_back(3);

    features::IntFeatureGenerator generator;
    FeatureMap features;
    generator.getFeatures(
            features,
            data.data(),
            ZL_Type_numeric,
            sizeof(data[0]),
            data.size());

    assertExpectedErrorRate(features["nbElts"], data.size());
    assertExpectedErrorRate(features["eltWidth"], sizeof(data[0]));
    assertExpectedErrorRate(features["cardinality"], 4);
    assertExpectedErrorRate(features["cardinality_upper"], 5);
    assertExpectedErrorRate(features["cardinality_lower"], 4);
    assertExpectedErrorRate(features["range_size"], 3);
    assertExpectedErrorRate(features["mean"], 1.0000001192092611);
    assertExpectedErrorRate(features["variance"], 3.576277904926602e-07);
    assertExpectedErrorRate(features["stddev"], 0.0005980198913854456);
    assertExpectedErrorRate(features["skewness"], 2229.5797976466847);
    assertExpectedErrorRate(features["kurtosis"], 8388605.888889026);
}

TEST(ZstrongMLTest, TestMomentsUint8)
{
    std::vector<uint8_t> data;
    for (int i = 0; i < 256; ++i) {
        data.insert(data.end(), i, i);
    }

    features::IntFeatureGenerator generator;
    FeatureMap features;
    generator.getFeatures(
            features,
            data.data(),
            ZL_Type_numeric,
            sizeof(data[0]),
            data.size());

    assertExpectedErrorRate(features["nbElts"], data.size());
    assertExpectedErrorRate(features["eltWidth"], sizeof(data[0]));
    assertExpectedErrorRate(features["cardinality"], 255);
    assertExpectedErrorRate(features["cardinality_upper"], 255);
    assertExpectedErrorRate(features["cardinality_lower"], 255);
    assertExpectedErrorRate(features["range_size"], 254);
    assertExpectedErrorRate(features["mean"], 170.33333333333334);
    assertExpectedErrorRate(features["variance"], 3626.666666666667);
    assertExpectedErrorRate(features["stddev"], 60.221812216726484);
    assertExpectedErrorRate(features["skewness"], -0.5656951738787298);
    assertExpectedErrorRate(features["kurtosis"], -0.6000551487484294);
}
} // namespace zstrong::ml
