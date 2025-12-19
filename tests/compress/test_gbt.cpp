// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <cmath>
#include <random>
#include <vector>

#include "openzl/common/stream.h" // For stream usage in hardcoded feature generators
#include "openzl/compress/selectors/ml/gbt.h"
#include "tests/zstrong/test_zstrong_fixture.h"

using namespace ::testing;

namespace {
const double kEpsilon    = 1e-6;
const size_t kRandomSeed = 100;

/*
    Generate tree with sz nodes - where the ith node will have a
   corresponding ith feature. The value of the ith node will be the i + 1
   and the corresponding feature will have i + 1 + offset value. We always
   assume that there exist a left and right child at 2i + 1 and 2i + 2
   respectively. The missing child will be randomly chosen to be either the
   left or right child. We can force the missing child to always be the
   right child by setting forceRight to true for testing purposes.
*/

GBTPredictor_Tree generateTree(
        size_t sz,
        std::vector<GBTPredictor_Node>& nodes,
        size_t featureIdxOffset = 0,
        float valueOffset       = 0,
        bool forceRight         = false)
{
    std::mt19937_64 mt(kRandomSeed);
    std::uniform_int_distribution<uint8_t> dist(0, 100);

    for (size_t i = 0; i < sz; ++i) {
        // Update necessary variables if node is leaf node
        bool const isLeaf          = ((2 * i + 1) >= sz);
        const int featureIdx       = isLeaf ? -1 : (int)(i + featureIdxOffset);
        const size_t leftChildIdx  = isLeaf ? (size_t)-1 : 2 * i + 1;
        const size_t rightChildIdx = isLeaf ? (size_t)-1 : 2 * i + 2;
        const size_t missingChildIdx =
                (forceRight || dist(mt) % 2) ? rightChildIdx : leftChildIdx;
        const float value = (float)(i + 1) + valueOffset;

        nodes.push_back(
                {
                        .featureIdx      = featureIdx,
                        .value           = value,
                        .leftChildIdx    = leftChildIdx,
                        .rightChildIdx   = rightChildIdx,
                        .missingChildIdx = missingChildIdx,
                });
    }
    GBTPredictor_Tree tree = { .numNodes = nodes.size(),
                               .nodes    = nodes.data() };
    return tree;
}

GBTPredictor_Tree generateTreeAndFeatures(
        size_t sz,
        std::vector<float>& features,
        std::vector<GBTPredictor_Node>& nodes,
        float featureOffset = 0,
        float valueOffset   = 0,
        bool forceRight     = false)
{
    GBTPredictor_Tree tree =
            generateTree(sz, nodes, features.size(), valueOffset, forceRight);
    features.resize(sz, 0);
    iota(features.begin(), features.end(), featureOffset + 1.0f);
    return tree;
}

} // namespace

TEST(GBTTest, oneNodeTree)
{
    // Generate tree with 1 node
    std::vector<GBTPredictor_Node> nodes = { { .featureIdx      = -1,
                                               .value           = 1.0f,
                                               .leftChildIdx    = 0,
                                               .rightChildIdx   = 0,
                                               .missingChildIdx = 0 } };
    std::vector<float> features          = { 0.5f };
    const GBTPredictor_Tree tree         = { .numNodes = nodes.size(),
                                             .nodes    = nodes.data() };

    // Since tree only has 1 node, result should be 1
    EXPECT_NEAR(
            GBTPredictor_Tree_evaluate(&tree, features.data(), features.size()),
            1.0f,
            kEpsilon);
}

TEST(GBTTest, simpleTreeRight)
{
    /*
       Tree Structure (feature, value)
                    (1.5, 1)
                /              \\
           (2.5, 2)           (3.5, 3)
          /       \          /      \\
      (4.5, 4) (5.5, 5)  (6.5, 6) (7.5, 7)

      Since feature is always > value, result should be right most node
    */
    size_t sz = 7;
    std::vector<GBTPredictor_Node> nodes;
    std::vector<float> features;
    const GBTPredictor_Tree tree =
            generateTreeAndFeatures(sz, features, nodes, 0.5f);

    EXPECT_NEAR(
            GBTPredictor_Tree_evaluate(&tree, features.data(), features.size()),
            7.0f,
            kEpsilon);
}

TEST(GBTTest, simpleTreeLeft)
{
    // Generate full tree with 7 nodes
    size_t sz = 7;
    std::vector<GBTPredictor_Node> nodes;
    std::vector<float> features;
    const GBTPredictor_Tree tree =
            generateTreeAndFeatures(sz, features, nodes, -0.5f);

    // feature is always < value, result should be left most node
    EXPECT_NEAR(
            GBTPredictor_Tree_evaluate(&tree, features.data(), features.size()),
            4.0f,
            kEpsilon);
}

TEST(GBTTest, nanFeature)
{
    /*
      Tree Structure (feature, value)
                   (0.5, 1)
             //                 \
          (nan, 2)           (2.5, 3)
         /       \\          /       \
     (3.5, 4) (4.5, 5)  (5.5, 6) (6.5, 7)

     Since feature is always < value, result should be left most node.
     However, since value is nan and we force missing ind to be the right
     child the result should be 5.
   */
    size_t sz = 7;
    std::vector<GBTPredictor_Node> nodes;
    std::vector<float> features;
    const GBTPredictor_Tree tree =
            generateTreeAndFeatures(sz, features, nodes, -0.5f, 0.0f, true);
    // Set node to nan
    features[1] = std::nanf("");

    EXPECT_NEAR(
            GBTPredictor_Tree_evaluate(&tree, features.data(), features.size()),
            5.0f,
            kEpsilon);
}

TEST(GBTTest, infFeature)
{
    /*
      Tree Structure (feature, value)
                   (inf, 1)
             /                 \\
          (2.5, 2)           (3.5, 3)
         /       \          /       \\
     (4.5, 4) (5.5, 5)  (6.5, 6) (7.5, 7)

     Since feature is always > value, result should be right most node.
   */
    size_t sz = 7;
    std::vector<GBTPredictor_Node> nodes;
    std::vector<float> features;
    const GBTPredictor_Tree tree =
            generateTreeAndFeatures(sz, features, nodes, 0.5f);
    // Set root node to infinity
    features[0] = std::numeric_limits<float>::infinity();

    EXPECT_NEAR(
            GBTPredictor_Tree_evaluate(&tree, features.data(), features.size()),
            7.0f,
            kEpsilon);
}

TEST(GBTTest, outOfBoundsFeatureInd)
{
    /*
     Tree Structure (feature, value)
                  (0.5, 1)
            //                \
         (Out Of Bounds, 2)      (OOB, 3)
        /       \\          /      \
    (OOB, 4) (OOB, 5)  (OOB, 6) (OOB, 7)

    Feature always < value, so we will always go left. Since the 2nd node has
    out of bounds feature ind, and we force missing ind to be the right child,
    result should be 5.
  */
    size_t sz = 7;
    std::vector<GBTPredictor_Node> nodes;
    std::vector<float> features;
    const GBTPredictor_Tree tree =
            generateTreeAndFeatures(sz, features, nodes, -0.5f, 0.0f, true);
    features.resize(1); // Resize so feature index is out of bounds

    EXPECT_NEAR(
            GBTPredictor_Tree_evaluate(&tree, features.data(), features.size()),
            5.0f,
            kEpsilon);
}

class GBTBinaryForestTest : public Test {
    /* Generate 1 forest with 5 trees with 1 node each, where each node has a
     * value of 0.2f.
     */
   public:
    std::vector<float> binaryFeatures = { 1.0f };
    std::vector<GBTPredictor_Tree> trees;
    std::vector<GBTPredictor_Node> nodes;
    std::vector<GBTPredictor_Forest> binaryForest;

    void SetUp() override
    {
        const size_t kTreeNb = 5;
        GBTPredictor_Node node;
        node.featureIdx      = -1;
        node.value           = 0.2f;
        node.leftChildIdx    = 0;
        node.rightChildIdx   = 0;
        node.missingChildIdx = 0;
        nodes.clear();
        nodes.push_back(node);
        for (size_t i = 0; i < kTreeNb; i++) {
            trees.push_back(
                    { .numNodes = nodes.size(), .nodes = nodes.data() });
        }
        binaryForest.push_back(
                { .numTrees = trees.size(), .trees = trees.data() });
    }
};

TEST_F(GBTBinaryForestTest, binaryClassification)
{
    /* Result should be 1, since each tree in the 5 forests have value of 0.2.
     * The resulting sum is greater than 0, so the predicted label is 1.
     */
    const GBTPredictor predictor = { .numForests = binaryForest.size(),
                                     .forests    = binaryForest.data() };

    EXPECT_EQ(
            GBTPredictor_predict(
                    &predictor, binaryFeatures.data(), binaryFeatures.size()),
            (size_t)1);
}

TEST_F(GBTBinaryForestTest, getNumBinaryClass)
{
    const GBTPredictor binaryClassPredictor = {
        .numForests = binaryForest.size(), .forests = binaryForest.data()
    };
    EXPECT_EQ(GBTPredictor_getNumClasses(&binaryClassPredictor), (size_t)2);
}

class GBTMultiClassForestTest : public Test {
    /* Generate 3 forests, where each forest contains 1 tree with 7 nodes and
     * the only difference between each forest is the value and feature.
     */
   public:
    std::vector<float> multiClassFeatures;
    std::vector<GBTPredictor_Forest> multiClassForests;

    std::vector<GBTPredictor_Node> smallNodes;
    std::vector<GBTPredictor_Node> mediumNodes;
    std::vector<GBTPredictor_Node> largeNodes;

    const GBTPredictor_Tree largeTree = generateTree(7, largeNodes, 0.0f, 5.0f);
    const GBTPredictor_Tree mediumTree = generateTree(7, mediumNodes);
    const GBTPredictor_Tree smallTree  = generateTreeAndFeatures(
            7,
            multiClassFeatures,
            smallNodes,
            1.0f,
            -5.0f);
    void SetUp() override
    {
        multiClassForests.push_back({ .numTrees = 1, .trees = &smallTree });
        multiClassForests.push_back({ .numTrees = 1, .trees = &largeTree });
        multiClassForests.push_back({ .numTrees = 1, .trees = &mediumTree });
    }
};

TEST_F(GBTMultiClassForestTest, multiClassification)
{
    /* Result should be 1, the index of the forest containing the larger tree
     */
    const GBTPredictor predictor = { .numForests = multiClassForests.size(),
                                     .forests    = multiClassForests.data() };

    EXPECT_EQ(
            GBTPredictor_predict(
                    &predictor,
                    multiClassFeatures.data(),
                    multiClassFeatures.size()),
            (size_t)1);
}

TEST_F(GBTMultiClassForestTest, getNumMultiClass)
{
    const GBTPredictor multiClassPredictor = {
        .numForests = multiClassForests.size(),
        .forests    = multiClassForests.data()
    };

    EXPECT_EQ(GBTPredictor_getNumClasses(&multiClassPredictor), (size_t)3);
}

class GBTBinaryModelTest : public Test {
    /* Generate GBTModel for binary classification containing 1 forest with 1
     * tree that contains 7 nodes each, where each node has a value from 1-6 and
     * a corresponding ith feature. The features are hardcoded through the
     * featureGen_binaryModelTest lambda
     */
   protected:
    const size_t sz = 7;

    const std::vector<int> streamData = { 0, 1, 2, 3, 4 };

    const std::vector<Label> classLabels = { Label("zero"), Label("one") };

    // Feature values hardcoded in featureGen_binaryModelTest lambda
    const std::vector<Label> featureLabels = {
        Label("mean"),              // 2
        Label("nbElts"),            // 5
        Label("variance"),          // 2.5
        Label("cardinality"),       // 5
        Label("cardinality_upper"), // 5
        Label("cardinality_lower"), // 5
        Label("range_size"),        // 4
        Label("eltWidth"),          // 4
    };

   public:
    std::vector<GBTPredictor_Node> nodes;
    const std::vector<GBTPredictor_Tree> trees = { generateTree(sz, nodes) };

    const std::vector<GBTPredictor_Forest> binaryForest = {
        { .numTrees = trees.size(), .trees = trees.data() }
    };

    GBTPredictor binaryClassPredictor = { .numForests = binaryForest.size(),
                                          .forests    = binaryForest.data() };

    GBTModel model;

    std::unique_ptr<openzl::tests::WrappedStream<int>> stream;

    void SetUp() override
    {
        // Lambda for generating hardcoded features
        auto featureGen_binaryModelTest = [](const ZL_Input* inputStream,
                                             VECTOR(LabeledFeature)
                                                     * features) -> ZL_Report {
            ZL_ASSERT(ZL_Input_type(inputStream) == ZL_Type_numeric);

            LabeledFeature meanFeature             = { "mean", 2.0f };
            LabeledFeature nbEltsFeature           = { "nbElts", 5.0f };
            LabeledFeature varianceFeature         = { "variance", 2.5f };
            LabeledFeature cardinalityFeature      = { "cardinality", 5.0f };
            LabeledFeature cardinalityUpperFeature = { "cardinality_upper",
                                                       5.0f };
            LabeledFeature cardinalityLowerFeature = { "cardinality_lower",
                                                       5.0f };
            LabeledFeature rangeSizeFeature        = { "range_size", 4.0f };
            LabeledFeature eltWidthFeature         = { "eltWidth", 4.0f };

            bool badAlloc = false;
            badAlloc |= !VECTOR_PUSHBACK(*features, nbEltsFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, eltWidthFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, cardinalityFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, cardinalityUpperFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, cardinalityLowerFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, rangeSizeFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, meanFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, varianceFeature);

            ZL_RET_R_IF(
                    allocation, badAlloc, "Failed to add features to vector");
            return ZL_returnSuccess();
        };

        model = { .predictor        = &binaryClassPredictor,
                  .featureGenerator = featureGen_binaryModelTest,
                  .nbLabels         = classLabels.size(),
                  .classLabels      = classLabels.data(),
                  .nbFeatures       = featureLabels.size(),
                  .featureLabels    = featureLabels.data() };

        stream = std::make_unique<openzl::tests::WrappedStream<int>>(
                streamData, ZL_Type_numeric);
    }

    void TearDown() override
    {
        stream.reset();
    }
};

TEST_F(GBTBinaryModelTest, labeledBinaryClass)
{
    /*
                       Tree Structure (feature, value)
                              (mean = 2, 1)
                      /                         \\
            (nbElts = 5, 2)                (variance = 2.5, 3)
             /            \                  //            \
     (card = 5, 4) (card_u = 5, 5)    (card_l = 5, 6) (range = 4, 7)

     The final result depends on the 5th node of this tree, since 6 > 0 the
     resulting binary classification is 1.
   */
    ZL_RESULT_OF(Label) result = GBTModel_predict(&model, stream->getStream());
    ASSERT_FALSE(ZL_RES_isError(result));
    const std::string decodedLabel = ZL_RES_value(result);
    EXPECT_EQ(decodedLabel, "one");
}

TEST_F(GBTBinaryModelTest, swappedLabeledBinaryClass)
{
    /* From the above test, we know that the result depends on the 5th node of
     * the tree, we want to make sure that if we change the value of this node
     * that the resulting classification changes too. Set the value to -0.45,
     * and verify that the classification is now 0 since -0.45f < 0.
     */
    nodes[5].value             = -0.45f;
    ZL_RESULT_OF(Label) result = GBTModel_predict(&model, stream->getStream());
    ASSERT_FALSE(ZL_RES_isError(result));
    const std::string decodedLabel = ZL_RES_value(result);
    EXPECT_EQ(decodedLabel, "zero");
}

class GBTMultiClassModelTest : public GBTMultiClassForestTest {
    /* Generate GBTModel for multiclass classification containing 3 forests,
     * where each forest contains 1 tree with 7 nodes and the only difference
     * between each forest is the value and feature of the tree in the forest.
     * Features are hardcoded in featureGen_multiClassModelTest and each ith
     * node has a corresponding ith feature/featureLabel.
     */
   protected:
    const size_t testNodeIdx          = 2;
    const std::vector<int> streamData = { 0, 2, 4, 6, 8, 10 };

    const std::vector<Label> classLabels = { Label("class1"),
                                             Label("class2"),
                                             Label("class3") };

    const std::vector<Label> featureLabels = {
        Label("mean"),              // 5
        Label("range_size"),        // 10
        Label("variance"),          // 14
        Label("cardinality"),       // 6
        Label("cardinality_upper"), // 6
        Label("cardinality_lower"), // 5
        Label("nbElts"),            // 6
        Label("eltWidth"),          // 4
    };

   public:
    std::unique_ptr<GBTPredictor> multiClassPredictor;
    GBTModel model;

    std::unique_ptr<openzl::tests::WrappedStream<int>> stream;

    void SetUp() override
    {
        GBTMultiClassForestTest::SetUp();

        multiClassPredictor = std::make_unique<GBTPredictor>((GBTPredictor){
                multiClassForests.size(), multiClassForests.data() });

        auto featureGen_multiClassModelTest =
                [](const ZL_Input* inputStream,
                   VECTOR(LabeledFeature) * features) -> ZL_Report {
            ZL_ASSERT(ZL_Input_type(inputStream) == ZL_Type_numeric);

            LabeledFeature meanFeature             = { "mean", 5.0f };
            LabeledFeature rangeSizeFeature        = { "range_size", 10.0f };
            LabeledFeature varianceFeature         = { "variance", 14.0f };
            LabeledFeature cardinalityFeature      = { "cardinality", 6.0f };
            LabeledFeature cardinalityUpperFeature = { "cardinality_upper",
                                                       6.0f };
            LabeledFeature cardinalityLowerFeature = { "cardinality_lower",
                                                       5.0f };
            LabeledFeature nbEltsFeature           = { "nbElts", 6.0f };
            LabeledFeature eltWidthFeature         = { "eltWidth", 4.0f };

            bool badAlloc = false;
            badAlloc |= !VECTOR_PUSHBACK(*features, nbEltsFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, eltWidthFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, cardinalityFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, cardinalityUpperFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, cardinalityLowerFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, rangeSizeFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, meanFeature);
            badAlloc |= !VECTOR_PUSHBACK(*features, varianceFeature);

            ZL_RET_R_IF(
                    allocation, badAlloc, "Failed to add features to vector");
            return ZL_returnSuccess();
        };
        model = { .predictor        = multiClassPredictor.get(),
                  .featureGenerator = featureGen_multiClassModelTest,
                  .nbLabels         = classLabels.size(),
                  .classLabels      = classLabels.data(),
                  .nbFeatures       = featureLabels.size(),
                  .featureLabels    = featureLabels.data() };

        stream = std::make_unique<openzl::tests::WrappedStream<int>>(
                streamData, ZL_Type_numeric);
    }

    void TearDown() override
    {
        multiClassPredictor.reset();
        stream.reset();
        GBTMultiClassForestTest::TearDown();
    }
};

TEST_F(GBTMultiClassModelTest, labeledMultiClass)
{
    /*
     * In MultiClassForestTest Setup, largeTree contains nodes
     * with values that are larger than the nodes of the other trees. This means
     * that largeTree will always contain the maxValue, so the predictor will
     * always select the forest containing largeTree. Verify that result is the
     * label corresponding to the largeTree.
     */
    ZL_RESULT_OF(Label) result = GBTModel_predict(&model, stream->getStream());
    ASSERT_FALSE(ZL_RES_isError(result));
    const std::string decodedLabel = ZL_RES_value(result);
    EXPECT_EQ(decodedLabel, "class2");
}

TEST_F(GBTMultiClassModelTest, IncorrectNumClassLabels)
{
    /*
     * Verify that if the number of class labels is less than the number of
     * forests, we get an error.
     */
    model.nbLabels             = 0;
    model.classLabels          = {};
    ZL_RESULT_OF(Label) result = GBTModel_predict(&model, stream->getStream());
    ASSERT_TRUE(ZL_RES_isError(result));
}

TEST(GBTTest, verifyGBTNullModel)
{
    // Verify that we get error when model is null
    const ZL_Report report = GBTModel_validate(nullptr);
    ASSERT_TRUE(ZL_isError(report));
}

TEST_F(GBTBinaryModelTest, verifyValidGBTModel)
{
    // Verify that we don't get any errors when binary model is valid
    const ZL_Report report = GBTModel_validate(&model);
    ASSERT_FALSE(ZL_isError(report));
}

TEST_F(GBTMultiClassModelTest, verifyValidGBTModel)
{
    // Verify that we don't get any errors when multiclass model is valid
    const ZL_Report report = GBTModel_validate(&model);
    ASSERT_FALSE(ZL_isError(report));
}

class GBTValidModelTest : public GBTMultiClassModelTest {
   protected:
    const size_t sz = 7;

   public:
    void SetUp() override
    {
        GBTMultiClassModelTest::SetUp();
    }

    void createPredictorAndValidate(std::vector<GBTPredictor_Node> nodes)
    {
        const GBTPredictor_Tree invalid_trees    = { .numNodes = nodes.size(),
                                                     .nodes    = nodes.data() };
        const GBTPredictor_Forest invalid_forest = { .numTrees = 1,
                                                     .trees = &invalid_trees };
        GBTPredictor invalid_predictor           = { .numForests = 1,
                                                     .forests    = &invalid_forest };

        createModelAndValidate(&invalid_predictor);
    }

    void createModelAndValidate(GBTPredictor* predictor)
    {
        GBTModel tmp_model = { .predictor        = predictor,
                               .featureGenerator = FeatureGen_integer,
                               .nbLabels         = classLabels.size(),
                               .classLabels      = classLabels.data(),
                               .nbFeatures       = featureLabels.size(),
                               .featureLabels    = featureLabels.data() };

        const ZL_Report report = GBTModel_validate(&tmp_model);
        ASSERT_TRUE(ZL_isError(report));
    }
};

TEST_F(GBTValidModelTest, verifyGBTModelNullPredictor)
{
    // Verify that we get errors when predictor is null
    model.predictor        = nullptr;
    const ZL_Report report = GBTModel_validate(&model);
    ASSERT_TRUE(ZL_isError(report));
}

TEST_F(GBTValidModelTest, verifyGBTModelNullClassLabels)
{
    // Verify that we get errors when classLabels is null
    model.classLabels      = nullptr;
    const ZL_Report report = GBTModel_validate(&model);
    ASSERT_TRUE(ZL_isError(report));
}

TEST_F(GBTValidModelTest, verifyGBTModelNullFeatureLabels)
{
    // Verify that we get errors when featureLabels is null
    model.featureLabels    = nullptr;
    const ZL_Report report = GBTModel_validate(&model);
    ASSERT_TRUE(ZL_isError(report));
}

TEST_F(GBTValidModelTest, verifyGBTModelNullForests)
{
    // Verify that we get errors when forests is null
    GBTPredictor invalid_predictor = { .numForests = 1, .forests = nullptr };
    createModelAndValidate(&invalid_predictor);
}

TEST_F(GBTValidModelTest, verifyGBTModelNullTrees)
{
    // Verify that we get errors when a tree is null
    const GBTPredictor_Forest null_forest = { .numTrees = 1, .trees = nullptr };
    GBTPredictor invalid_predictor        = { .numForests = 1,
                                              .forests    = &null_forest };
    createModelAndValidate(&invalid_predictor);
}

TEST_F(GBTValidModelTest, verifyGBTModelNullNodes)
{
    // Verify that we get errors when a node is null
    const GBTPredictor_Node* null_nodes = nullptr;
    const GBTPredictor_Tree null_tree = { .numNodes = 1, .nodes = null_nodes };
    const GBTPredictor_Forest invalid_forest = { .numTrees = 1,
                                                 .trees    = &null_tree };
    GBTPredictor invalid_predictor           = { .numForests = 1,
                                                 .forests    = &invalid_forest };
    createModelAndValidate(&invalid_predictor);
}

TEST_F(GBTValidModelTest, verifyNegOutOfBoundsFeature)
{
    // Verify that we get errors when featureIdx is out of bounds
    std::vector<GBTPredictor_Node> invalid_nodes;
    generateTree(sz, invalid_nodes);
    invalid_nodes[testNodeIdx].featureIdx = -2;

    createPredictorAndValidate(invalid_nodes);
}

TEST_F(GBTValidModelTest, verifyPosOutOfBoundsFeature)
{
    // Verify that we get errors when featureIdx is out of bounds
    std::vector<GBTPredictor_Node> invalid_nodes;
    generateTree(sz, invalid_nodes);
    invalid_nodes[testNodeIdx].featureIdx = featureLabels.size();

    createPredictorAndValidate(invalid_nodes);
}

TEST_F(GBTValidModelTest, verifyCyclicLeftChild)
{
    /**
     * Verify that we get errors when left child points to parent node or
     * any node before parent
     */
    std::vector<GBTPredictor_Node> cyclic_nodes;
    generateTree(sz, cyclic_nodes);
    cyclic_nodes[testNodeIdx].leftChildIdx = 0;

    createPredictorAndValidate(cyclic_nodes);
}

TEST_F(GBTValidModelTest, verifyCyclicRightChild)
{
    /**
     * Verify that we get errors when right child points to parent node or
     * any node before parent
     */
    std::vector<GBTPredictor_Node> cyclic_nodes;
    generateTree(sz, cyclic_nodes);
    cyclic_nodes[testNodeIdx].rightChildIdx = 0;

    createPredictorAndValidate(cyclic_nodes);
}

TEST_F(GBTValidModelTest, verifyCyclicMissingChild)
{
    /**
     * Verify that we get errors when missing child points to parent node or
     * any node before parent
     */
    std::vector<GBTPredictor_Node> cyclic_nodes;
    generateTree(sz, cyclic_nodes);
    cyclic_nodes[testNodeIdx].missingChildIdx = 0;

    createPredictorAndValidate(cyclic_nodes);
}

TEST_F(GBTValidModelTest, verifyOutOfBoundsLeftChild)
{
    // Verify that we get errors when left child is out of bounds
    std::vector<GBTPredictor_Node> invalid_nodes;
    generateTree(sz, invalid_nodes);
    invalid_nodes[testNodeIdx].leftChildIdx = sz;

    createPredictorAndValidate(invalid_nodes);
}

TEST_F(GBTValidModelTest, verifyOutOfBoundsRightChild)
{
    // Verify that we get errors when right child is out of bounds
    std::vector<GBTPredictor_Node> invalid_nodes;
    generateTree(sz, invalid_nodes);
    invalid_nodes[testNodeIdx].rightChildIdx = sz;

    createPredictorAndValidate(invalid_nodes);
}

TEST_F(GBTValidModelTest, verifyOutOfBoundsMissingChild)
{
    // Verify that we get errors when missing child is out of bounds
    std::vector<GBTPredictor_Node> invalid_nodes;
    generateTree(sz, invalid_nodes);
    invalid_nodes[testNodeIdx].missingChildIdx = sz;

    createPredictorAndValidate(invalid_nodes);
}
