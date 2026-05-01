// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "tools/ml_selector/ml_selector_trainer.h"
#include <cstdio>
#include <numeric>
#include <random>
#include <string>
#include <vector>
#include "ml_features.h"
#include "openzl/common/a1cbor_helpers.h"
#include "openzl/compress/selectors/ml/ml_selector_graph.h"
#include "openzl/zl_reflection.h"
#include "src/openzl/compress/cgraph.h"
#include "src/openzl/compress/selectors/ml/features.h"
#include "tools/logger/Logger.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/sample_collection/training_sample_collector.h"

// Suppress warnings for XGBoost headers
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wcast-align"
#include <xgboost/c_api.h>
#include <xgboost/json.h>
#pragma GCC diagnostic pop
using namespace openzl::tools::logger;

namespace openzl::training {

const std::string ML_SELECTOR_GRAPH_NAME = "zl.ml_selector";

namespace {
/**
 * Macro provided by xgboost cpp to guard all calls
 */
#define safe_xgboost(call)                                                 \
    {                                                                      \
        int err = (call);                                                  \
        if (err != 0) {                                                    \
            throw std::runtime_error(                                      \
                    std::string(__FILE__) + ":" + std::to_string(__LINE__) \
                    + ": error in " + #call + ":" + XGBGetLastError());    \
        }                                                                  \
    }

/**
 * Contains both the 2D feature matrices and their flattened versions. The 2D
 * format is for train test split, while the flattened format is required by
 * XGBoost's DMatrix API.
 *
 * Naming follows scikit-learn conventions:
 * - X: Feature matrix
 * - y: Label vector
 */
struct FeatureData {
    std::vector<std::vector<float>> X;
    std::vector<float> y;
    std::vector<float> XFlat;

    FeatureData(
            const std::vector<std::vector<float>>& X_,
            const std::vector<float>& y_)
            : X(X_), y(y_)
    {
        for (const auto& x : X) {
            XFlat.insert(XFlat.end(), x.begin(), x.end());
        }
    }
};

/**
 * Holds the train/test split data for XGBoost model training.
 *
 */
struct TestTrainData {
    FeatureData train;
    FeatureData test;
};

/**
 * Owns the memory for a GBTPredictor.
 */
struct GBTPredictorWrapper {
    /// Node arrays for each tree (indexed by tree)
    std::vector<std::unique_ptr<std::vector<GBTPredictor_Node>>> core_nodes_;
    /// Tree arrays for each forest (indexed by forest)
    std::vector<std::unique_ptr<std::vector<GBTPredictor_Tree>>> core_trees_;
    /// All forests in the model
    std::unique_ptr<std::vector<GBTPredictor_Forest>> core_forests_;
    std::unique_ptr<GBTPredictor> core_predictor_;
};

} // namespace

/**
 * Extracts a numeric value from an XGBoost JSON node.
 *
 * Handles both JsonNumber (float) and JsonInteger, converting
 * the value to a float. Throws exception if unable to extract value.
 *
 * @param json The XGBoost JSON node to extract the value from
 * @return The numeric value as a float, or -1.0f if not a numeric type
 */
static float extractNumericVal(const xgboost::Json& json)
{
    if (xgboost::IsA<xgboost::JsonNumber>(json)) {
        return xgboost::get<xgboost::JsonNumber const>(json);
    } else if (xgboost::IsA<xgboost::JsonInteger>(json)) {
        return static_cast<float>(
                xgboost::get<xgboost::JsonInteger const>(json));
    }
    throw Exception(
            "Expected JsonNumber. Unable to extract numeric value from XGBoost JSON node");
}

/**
 * Recursively computes the maximum node ID in an XGBoost decision tree.
 *
 * Traverses the JSON format for XGBoost model. Used to
 * pre-allocate a contiguous vector for storing parsed tree nodes.
 *
 * @param json The root JSON node of the XGBoost tree
 * @return The maximum node ID found in the tree
 */
static int findMaxNodeId(const xgboost::Json& json)
{
    auto& objMap = xgboost::get<xgboost::JsonObject const>(json);
    int maxId    = extractNumericVal(json["nodeid"]);

    if (objMap.find("children") != objMap.end()) {
        auto& children =
                xgboost::get<xgboost::JsonArray const>(json["children"]);
        for (const auto& child : children) {
            maxId = std::max(maxId, findMaxNodeId(child));
        }
    }
    return maxId;
}

/**
 * Deleter functor to manage XGBoost BoosterHandle resource. Automatically calls
 * XGBoosterFree when unique_pointer goes out of scope
 */
struct BoosterHandleDeleter {
    using pointer = BoosterHandle;
    void operator()(BoosterHandle handle) const
    {
        if (handle != nullptr) {
            XGBoosterFree(handle);
        }
    }
};

using BoosterUniquePtr = std::unique_ptr<void, BoosterHandleDeleter>;

/**
 * Deleter functor to manage XGBoost DMatrixHandle resource. Automatically calls
 * XGDMatrixFree when unique_pointer goes out of scope
 */
struct DMatrixHandleDeleter {
    using pointer = DMatrixHandle;
    void operator()(DMatrixHandle handle) const
    {
        if (handle != nullptr) {
            XGDMatrixFree(handle);
        }
    }
};

using DMatrixUniquePtr = std::unique_ptr<void, DMatrixHandleDeleter>;

/**
 * Recursively parses an XGBoost decision tree node from JSON into a
 * GBTPredictor_Node structure.
 *
 * The parsed node is stored directly into the pre-allocated nodes vector
 * at the position corresponding to its nodeid.
 *
 * @param nodeJson    The JSON representation of the tree node
 * @param nodes       Output vector where parsed nodes are stored by their ID
 *
 * @throws Exception if nodeid exceeds the bounds of the nodes vector
 */
static void parseXGBoostNode(
        const xgboost::Json& nodeJson,
        std::vector<GBTPredictor_Node>& nodes)
{
    GBTPredictor_Node node{};

    auto& objMap = xgboost::get<xgboost::JsonObject const>(nodeJson);

    size_t nodeid = extractNumericVal(nodeJson["nodeid"]);
    if (objMap.find("leaf") != objMap.end()) {
        // Leaf node
        float leafValue = extractNumericVal(nodeJson["leaf"]);
        node.value      = leafValue;
        node.featureIdx = -1;

        Logger::log_c(VERBOSE1, "Leaf node: %f\n", leafValue);
    } else {
        // Internal node
        // Parse feature index from "fN" format
        std::string splitFeature =
                xgboost::get<xgboost::JsonString const>(nodeJson["split"]);
        float threshold  = extractNumericVal(nodeJson["split_condition"]);
        int yesChild     = extractNumericVal(nodeJson["yes"]);
        int noChild      = extractNumericVal(nodeJson["no"]);
        int missingChild = extractNumericVal(nodeJson["missing"]);

        node.featureIdx =
                std::stoi(splitFeature.substr(1)); // Skip the 'f' prefix
        node.leftChildIdx    = yesChild;
        node.rightChildIdx   = noChild;
        node.missingChildIdx = missingChild;
        node.value           = threshold;

        Logger::log_c(VERBOSE1, "Internal Node %d: ,", nodeid);

        Logger::log_c(
                VERBOSE1, "split[%s < %f] \n", splitFeature.c_str(), threshold);

        Logger::log_c(
                VERBOSE1,
                ", Yes node %d, no node %d, missing node %d\n",
                yesChild,
                noChild,
                missingChild);

        // Recursively process children
        if (objMap.find("children") != objMap.end()) {
            auto& children = xgboost::get<xgboost::JsonArray const>(
                    nodeJson["children"]);
            for (const auto& child : children) {
                parseXGBoostNode(child, nodes);
            }
        }
    }

    if (nodeid >= nodes.size()) {
        throw Exception("Node id is out of range");
    }
    nodes[nodeid] = node;
}

/**
 * Splits feature and label data into training and test sets. Used for
 * evaluating model performance during XGBoost boosting rounds.
 *
 * @param features     Feature vectors to split
 * @param labels       Corresponding labels to split
 * @param testSize     Fraction of data to reserve for testing
 * @param shuffle      Whether to shuffle (in place) data before splitting
 * @param randomState  Seed for shuffling
 *
 * @return TrainTestData containing XTrain, XTest, yTrain, and yTest splits
 *
 * @throws Exception if features and labels have different sizes
 */
static TestTrainData trainTestSplit(
        std::vector<std::vector<float>>& features,
        std::vector<float>& labels,
        float testSize           = 0.2,
        bool shuffle             = true,
        unsigned int randomState = 40)
{
    if (features.size() != labels.size()) {
        throw Exception("Features and labels must have same size");
    }

    std::vector<size_t> indices(features.size());
    std::iota(indices.begin(), indices.end(), 0);

    if (shuffle) {
        std::mt19937 rng(randomState);
        std::shuffle(indices.begin(), indices.end(), rng);

        std::vector<std::vector<float>> shuffledFeatures;
        std::vector<float> shuffledLabels;
        shuffledFeatures.reserve(features.size());
        shuffledLabels.reserve(labels.size());

        for (size_t idx : indices) {
            shuffledFeatures.push_back(features[idx]);
            shuffledLabels.push_back(labels[idx]);
        }
        features = shuffledFeatures;
        labels   = shuffledLabels;
    }

    size_t trainSize = static_cast<size_t>(features.size() * (1.0f - testSize));

    std::vector<std::vector<float>> XTrain(
            features.begin(), features.begin() + trainSize);
    std::vector<std::vector<float>> XTest(
            features.begin() + trainSize, features.end());
    std::vector<float> yTrain(labels.begin(), labels.begin() + trainSize);
    std::vector<float> yTest(labels.begin() + trainSize, labels.end());

    return { .train = FeatureData(XTrain, yTrain),
             .test  = FeatureData(XTest, yTest) };
}

/**
 * Transforms a trained XGBoost model into GBTPredictor.
 *
 * @param xgBoostDump      JSON strings for each tree
 * @param numClasses       Number of classification classes (2 for binary)
 *
 * @return GBTPredictorWrapper containing the converted model with ownership
 *         of all internal structures
 * @throws Exception if the XGBoost dump is unexpected size
 */
static GBTPredictorWrapper createGBTModelFromXGBoost(
        std::vector<std::string>& xgBoostDump,
        size_t numClasses)
{
    GBTPredictorWrapper pred;
    // For binary classification, XGBoost creates 1 forest
    // For multiclass, it creates one forest per class
    if (numClasses == 1) {
        throw Exception("Only 1 class found in XGBoost dump, expected 2");
    }
    size_t numForests = numClasses == 2 ? 1 : numClasses;
    size_t numRounds  = xgBoostDump.size() / numForests;

    /**
     * For each boosting round, XGBoost creates a tree per class (unless its
     * binary classification then it's just one tree). So we have something like
     * this:
     *
     * r0: tree_label_0, tree_label_1, .... tree_label_n
     * r1: tree_label_0, tree_label_1, .... tree_label_n
     * ...
     *
     * So for forest i, we use (jth_round * numForests) + i to get all trees in
     * that forest
     */
    for (size_t forestIdx = 0; forestIdx < numForests; forestIdx++) {
        std::vector<GBTPredictor_Tree> trees;
        for (size_t round = 0; round < numRounds; round++) {
            size_t treeIdx = round * numForests + forestIdx;
            if (treeIdx >= xgBoostDump.size()) {
                throw Exception("XGBoost Dump unexpected size");
            }
            const std::string& treeStr = xgBoostDump[treeIdx];
            xgboost::Json treeJson     = xgboost::Json::Load(
                    xgboost::StringView(treeStr.data(), treeStr.size()));

            std::vector<GBTPredictor_Node> nodes;
            int maxNodeId = findMaxNodeId(treeJson);
            nodes.resize(maxNodeId + 1);

            parseXGBoostNode(treeJson, nodes);

            pred.core_nodes_.push_back(
                    std::make_unique<std::vector<GBTPredictor_Node>>(
                            std::move(nodes)));

            trees.push_back(
                    { .numNodes = pred.core_nodes_.back()->size(),
                      .nodes    = pred.core_nodes_.back()->data() });
        }

        pred.core_trees_.push_back(
                std::make_unique<std::vector<GBTPredictor_Tree>>(
                        std::move(trees)));

        if (!pred.core_forests_) {
            pred.core_forests_ =
                    std::make_unique<std::vector<GBTPredictor_Forest>>();
        }

        pred.core_forests_->push_back(
                { .numTrees = pred.core_trees_.back()->size(),
                  .trees    = pred.core_trees_.back()->data() });
    }

    pred.core_predictor_ = std::make_unique<GBTPredictor>(
            GBTPredictor{ .numForests = pred.core_forests_->size(),
                          .forests    = pred.core_forests_->data() });

    return pred;
}

/**
 * Trains an XGBoost gradient boosted tree model and converts it to the
 * GBTPredictor format for inference.
 *
 * @param data          Train/test split data
 * @param num_classes   Number of classification classes
 *
 * @return GBTPredictorWrapper containing the trained model
 *
 */
static GBTPredictorWrapper trainXGBoostModel(
        TestTrainData& data,
        size_t num_classes)
{
    if (data.train.X.empty() || data.train.y.empty()) {
        throw Exception("Training data cannot be empty");
    }

    if (data.test.X.empty() || data.test.y.empty()) {
        throw Exception("Test data cannot be empty");
    }

    std::vector<std::string> xgBoostDump;

    // equivalent to n_estimators in python XGBoost
    constexpr size_t DEFAULT_XGBOOST_ROUNDS = 30;

    // Create raw handles first, then wrap in unique_ptr
    DMatrixHandle trainHandleRaw   = nullptr;
    DMatrixHandle testHandleRaw    = nullptr;
    BoosterHandle boosterHandleRaw = nullptr;

    // Convert training and test data to DMatrixHandle needed by XGBoost
    safe_xgboost(XGDMatrixCreateFromMat(
            data.train.XFlat.data(),
            data.train.X.size(),
            data.train.X.front().size(),
            -1,
            &trainHandleRaw));
    DMatrixUniquePtr train(trainHandleRaw);

    safe_xgboost(XGDMatrixSetFloatInfo(
            train.get(), "label", data.train.y.data(), data.train.y.size()));

    safe_xgboost(XGDMatrixCreateFromMat(
            data.test.XFlat.data(),
            data.test.X.size(),
            data.test.X.front().size(),
            -1,
            &testHandleRaw));
    DMatrixUniquePtr test(testHandleRaw);

    // Set model parameters
    safe_xgboost(XGDMatrixSetFloatInfo(
            test.get(), "label", data.test.y.data(), data.test.y.size()));

    DMatrixHandle trainHandle = train.get();
    safe_xgboost(XGBoosterCreate(&trainHandle, 1, &boosterHandleRaw));
    BoosterUniquePtr booster(boosterHandleRaw);

    BoosterHandle boosterHandle = booster.get();
    safe_xgboost(XGBoosterSetParam(boosterHandle, "booster", "gbtree"));
    safe_xgboost(XGBoosterSetParam(boosterHandle, "learning_rate", "0.1"));
    // equivalent to n_jobs in python XGBoost
    safe_xgboost(XGBoosterSetParam(boosterHandle, "nthread", "1"));
    safe_xgboost(XGBoosterSetParam(boosterHandle, "min_child_weight", "0.0"));
    safe_xgboost(XGBoosterSetParam(boosterHandle, "subsample", "0.7"));
    safe_xgboost(XGBoosterSetParam(boosterHandle, "colsample_bynode", "0.8"));

    // Set the objective function based on whether multiclass or binary
    if (num_classes > 2) {
        safe_xgboost(XGBoosterSetParam(
                boosterHandle, "objective", "multi:softprob"));
        safe_xgboost(XGBoosterSetParam(
                boosterHandle,
                "num_class",
                std::to_string(num_classes).c_str()));
    } else {
        safe_xgboost(XGBoosterSetParam(
                boosterHandle, "objective", "binary:logistic"));
        // Explicitly set base_score to 0.5 (this just means that data has 50/50
        // chance to be in class 1 or class 2 as a start point). This is to
        // avoid auto-computation error, which happens if training data is
        // heavily imbalanced and only labeled to one class.
        safe_xgboost(XGBoosterSetParam(boosterHandle, "base_score", "0.5"));
    }

    const int eval_dmats_size                 = 2;
    DMatrixHandle eval_dmats[eval_dmats_size] = { trainHandle, test.get() };
    Logger::log_c(
            VERBOSE1,
            "Training XGBoost model with %d boosting rounds\n",
            DEFAULT_XGBOOST_ROUNDS);
    // Start training the model
    for (size_t i = 0; i < DEFAULT_XGBOOST_ROUNDS; ++i) {
        // Update the model performance for each iteration
        safe_xgboost(
                XGBoosterUpdateOneIter(boosterHandle, (int)i, trainHandle));

        const char* eval_names[eval_dmats_size] = { "train", "test" };
        const char* eval_result                 = NULL;
        safe_xgboost(XGBoosterEvalOneIter(
                boosterHandle,
                (int)i,
                eval_dmats,
                eval_names,
                eval_dmats_size,
                &eval_result));
        Logger::log_c(VERBOSE1, "%s\n", eval_result);
    }

    bst_ulong len = 0;

    const char** dump = nullptr;

    safe_xgboost(XGBoosterDumpModelEx(
            boosterHandle,
            "", // fmap (empty string for no feature map)
            0,  // No stats
            "json",
            &len,
            &dump));

    xgBoostDump = std::vector<std::string>(dump, dump + len);

    // unique_ptr will automatically free the handles when they go out of scope
    return createGBTModelFromXGBoost(xgBoostDump, num_classes);
}

static void updateCompressor(
        Compressor& compressor,
        ZL_MLSelectorConfig& config,
        std::string& mlSelectorGraphName)
{
    Arena* arena       = ALLOC_HeapArena_create();
    A1C_Arena a1cArena = A1C_Arena_wrap(arena);

    ZL_SerializedMLConfig serializedConfig = unwrap(
            MLSelector_serializeMLSelectorConfig(nullptr, &config, &a1cArena));

    ZL_CopyParam configParam = {
        .paramId   = ZL_GENERIC_ML_SELECTOR_CONFIG_ID,
        .paramPtr  = serializedConfig.data,
        .paramSize = serializedConfig.size,
    };

    ZL_LocalParams localParams = { .copyParams = { .genParams    = &configParam,
                                                   .nbCopyParams = 1 } };

    ZL_GraphParameters newParams = {
        .localParams = &localParams,
    };

    ZL_GraphID existingMlSelectorGraphId =
            compressor.getGraph(mlSelectorGraphName).value();

    // Replace old config with new trained config
    auto result = ZL_Compressor_overrideGraphParams(
            compressor.get(), existingMlSelectorGraphId, &newParams);

    ALLOC_Arena_freeArena(arena);

    if (ZL_isError(result)) {
        throw std::runtime_error("Error overriding graph params");
    }
}

std::shared_ptr<const std::string_view> trainMLSelectorGraph(
        const std::vector<MultiInput>& inputs,
        Compressor& compressor,
        const TrainParams& trainParams)
{
    (void)trainParams;

    // Find the ML selector graph by prefix
    auto mlSelectorGraphs = graph_mutation::findAllGraphsWithPrefix(
            compressor, ML_SELECTOR_GRAPH_NAME);

    if (mlSelectorGraphs.empty()) {
        throw std::runtime_error(
                "Error finding ML selector graph with prefix '"
                + ML_SELECTOR_GRAPH_NAME + "'");
    }

    std::set<std::string> trainedMlSelectors;
    bool trainedAnyThisPass = true;
    /**
     * Process ML selectors iteratively until no more can be trained.
     *
     * ML selectors may be chained (output of one feeds into another), but
     * mlSelectorGraphNames has no guaranteed order. On each pass, we train
     * any selector that currently has available inputs. Once trained, its
     * outputs become inputs for downstream selectors, which can then be
     * trained in subsequent passes.
     */
    while (trainedMlSelectors.size() < mlSelectorGraphs.size()
           && trainedAnyThisPass) {
        trainedAnyThisPass = false;

        for (auto mlSelectorGraph : mlSelectorGraphs) {
            std::string mlSelectorGraphName = ZL_Compressor_Graph_getName(
                    compressor.get(), mlSelectorGraph);
            if (trainedMlSelectors.count(mlSelectorGraphName) > 0) {
                continue;
            }

            auto cctx = refCCtxForTraining(compressor);

            // Collect inputs for mlSelector graph
            auto mlSelectorInputs = collectInputStreamsForGraph(
                    inputs, mlSelectorGraphName, cctx);

            if (mlSelectorInputs.empty()) {
                continue;
            }

            const auto successors = ZL_Compressor_Graph_getCustomGraphs(
                    compressor.get(), mlSelectorGraph);

            std::vector<ZL_GraphID> successorGraphs;
            successorGraphs.reserve(successors.nbGraphIDs);
            for (size_t i = 0; i < successors.nbGraphIDs; ++i) {
                successorGraphs.push_back(successors.graphids[i]);
            }

            ProcessedMLTrainingSamples trainingSample = extractMLFeatures(
                    mlSelectorInputs, compressor, cctx, successorGraphs);

            TestTrainData splitData = trainTestSplit(
                    trainingSample.features, trainingSample.numericLabels);

            GBTPredictorWrapper gbtPred =
                    trainXGBoostModel(splitData, successors.nbGraphIDs);

            GBTModel coreModel = {
                .predictor        = gbtPred.core_predictor_.get(),
                .featureGenerator = FeatureGen_integer,
                .nbSuccessors     = successors.nbGraphIDs,
                .nbFeatures       = trainingSample.featurePtrNames.size(),
                .featureLabels    = trainingSample.featurePtrNames.data(),
            };

            ZL_MLSelectorConfig config = { .model         = ZL_GBT,
                                           .runtimeConfig = &coreModel };

            // Update compressor with new trained config
            updateCompressor(compressor, config, mlSelectorGraphName);
            trainedAnyThisPass = true;
            trainedMlSelectors.insert(mlSelectorGraphName);
        }
    }

    if (trainedMlSelectors.size() == 0) {
        throw std::runtime_error("No inputs captured for any mlSelector graph");
    }

    // Warn if some mlSelectors were left untrained
    if (trainedMlSelectors.size() < mlSelectorGraphs.size()) {
        Logger::log(
                WARNINGS,
                "Warning: ",
                mlSelectorGraphs.size() - trainedMlSelectors.size(),
                " mlSelector(s) could not be trained - no inputs captured.");
    }

    return graph_mutation::createSharedStringView(compressor.serialize());
}
} // namespace openzl::training
