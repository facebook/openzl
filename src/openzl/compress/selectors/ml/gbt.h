// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_COMPRESS_SELECTORS_ML_GBT_H
#define ZSTRONG_COMPRESS_SELECTORS_ML_GBT_H

#include "openzl/compress/selectors/ml/features.h"
#include "openzl/compress/selectors/ml/mlselector.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/**
 * Represents a Node in a tree. Internal nodes will have two children and a leaf
 * node will have no children. Internal node will have a valid featureIdx, while
 * a leaf node will have featureIdx == -1. The value can be either a threshold
 * for internal nodes or as a value for leaf nodes, it should always be a
 * valid numeric value.
 */
typedef struct {
    int featureIdx;
    float value;
    size_t leftChildIdx;
    size_t rightChildIdx;
    size_t missingChildIdx;
} GBTPredictor_Node;

/**
 * Represents a single gradient boosted binary decision tree. The tree is a
 * collection of nodes, where the first node is the root. Each internal node in
 * the tree is a comparison between a feature given by featureIdx and a
 * threshold given by the node's value. If the feature value is less than the
 * threshold then the left child is taken otherwise the right child is taken. If
 * the feature value is missing then the missing child is taken. When reaching a
 * leaf node, which is identified by having featureIdx of -1, then the value of
 * the node is returned as the tree's.
 */
typedef struct {
    size_t numNodes;
    const GBTPredictor_Node* nodes;
} GBTPredictor_Tree;

/**
 * Represents a forest, which is a collection of trees.
 */
typedef struct {
    size_t numTrees;
    const GBTPredictor_Tree* trees;
} GBTPredictor_Forest;

/**
 * A Gradient Boosted Trees (GBT) predictor that can be used to evaluate models
 * trained by XGBoost or LightGBM. The model represents a list of forests, where
 * each forest is a collection of trees. Prediction is done by evaluating all
 * trees and getting a sum of the values per forest. The forest with the highest
 * value is chosen as the predicted class. Binary-classification is a special
 * case in which only one forest is needed and if its combined value is compared
 * to 0.5 to decide on the class.
 */
typedef struct {
    size_t numForests;
    const GBTPredictor_Forest* forests;
} GBTPredictor;

/**
 * Evaluates a tree by traversing it. Assumes that the tree is valid, may be
 * undefined behavior if tree is not valid.
 *
 * @returns the value of the leaf node after traversing through the GBTree
 */
float GBTPredictor_Tree_evaluate(
        const GBTPredictor_Tree* tree,
        const float* features,
        size_t nbFeatures);

/**
 * Evaluates a forest by evaluating each tree in the forest. Assuming the tree
 * is valid.
 *
 * @returns the total sum of all trees in the forest.
 */
float GBTPredictor_Forest_evaluate(
        const GBTPredictor_Forest* forest,
        const float* features,
        size_t nbFeatures);

/**
 * Calculates the prediction for a single predictor and set of features.
 *
 * @returns the index of the classified class.
 */
size_t GBTPredictor_predict(
        const GBTPredictor* predictor,
        const float* features,
        size_t nbFeatures);

/**
 * @returns the number of classes supported by the predictor.
 */
size_t GBTPredictor_getNumClasses(const GBTPredictor* predictor);

/**
 * Represents the label from classification decision.
 */
typedef const char* Label;

/**
 * Defines a new type that can either be an error or Label
 */
ZL_RESULT_DECLARE_TYPE(Label);

/**
 * Defines type GBTModel, which is composed of 3 parts. First, the predictor,
 * which can take a vector of features as input and return a class id. Second,
 * the feature generator, which takes a stream and returns the features. The
 * feature generator can use the optional featureContext opaque ptr. Lastly, the
 * labels for the classes and features, which allows us to easily translate
 * class ids to class names and also ensure portability between training and
 * inference time.
 */
typedef struct {
    const GBTPredictor* predictor;
    FeatureGenerator featureGenerator;
    const void* featureContext;
    size_t nbLabels;
    const Label* classLabels;
    size_t nbFeatures;
    const Label* featureLabels;
} GBTModel;

/**
 * Calculates the prediction for a single model and set of features generated
 from the input stream.

 @returns the error if the model fails to predict a classification, otherwise
 returns index of prediction.
 */
ZL_RESULT_OF(size_t)
GBTModel_predictInd(const GBTModel* model, const ZL_Input* in, ZL_Graph* graph);

/**
 * Wrapper around GBTModel_predictInd and returns the class label instead of
 * index.

 @returns the error if the model fails to predict a classification, otherwise
 returns the classified label.
 */
ZL_RESULT_OF(Label)
GBTModel_predict(const GBTModel* model, const ZL_Input* in);

/**
 * Predicts the label of the input stream using GBTModel_predict
 *
 * @returns an empty string if the model fails to predict a classification,
 * otherwise returns the string representation of the classification
 */
const char* GBTModel_Desc_predict(const void* model, const ZL_Input* in);

/**
 * Validates the model by making sure each tree inside of the model is
 * valid, this means that there are no cycles and all of the feature/child
 * indices are not out of bounds. Additionally, we also verify that all model
 * elements are valid by making sure none of them are null.
 *
 * @returns an error if the model is invalid, otherwise returns success
 */
ZL_Report GBTModel_validate(const GBTModel* model);

/**
 * Validates a GBTPredictor by verifying each tree is valid, this means that
 * there are no cycles and all the feature/child indices are not out of bounds.

 * NOTE: If nbFeatures is not available please set it to -1. We will not check
 * node.featureIdx < nbFeatures if nbFeatures < 0
 */
ZL_Report GBTPredictor_validate(const GBTPredictor* predictor, int nbFeatures);

/**
 * Validates a forest by verifying each tree inside the forest is valid, this
 * means there are no cycles and all of the feature/child indices are not out of
 * bounds.
 */
ZL_Report GBTPredictor_validate_forest(
        const GBTPredictor* predictor,
        const size_t forest_idx,
        const int nbFeatures);

/**
 * Validates a single tree inside by verifying there are no
 * cycles in the tree, ensuring the value of each node is a valid numeric float
 * and none of the child/feature indices are out of bounds.

 * NOTE: Only checks if node.featureIdx < nbFeatures if nbFeatures >= 0
 */
ZL_Report GBTPredictor_validate_tree(
        const GBTPredictor_Tree* tree,
        int nbFeatures);

/**
 * Creates a typed selector based on the information from a GBTModel
 * NOTE: This function does not take ownership of `model`.
 *
 * `model` will be referenced by the new graph and needs to
 * outlive it. The user is responsible of destorying `model` once
 * the graph is destroyed.
 * TODO: support taking ownership of `model`.
 *
 * @returns The Graph ID of the newly created selector.
 */
ZL_GraphID ZL_Compressor_registerGBTModelGraph(
        ZL_Compressor* cgraph,
        const GBTModel* model,
        ZL_LabeledGraphID* labeledGraphs,
        size_t labeledGraphsSize);

ZL_END_C_DECLS

#endif
