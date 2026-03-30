// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <math.h>

#include "openzl/common/assertion.h"
#include "openzl/compress/selectors/ml/gbt.h"
#include "openzl/zl_public_nodes.h"
#include "openzl/zl_selector.h" //ZL_AUTO_FORMAT_VERSION

const size_t kMaxFeaturesCapacity = 1024;

float GBTPredictor_Tree_evaluate(
        const GBTPredictor_Tree* tree,
        const float* features,
        size_t nbFeatures)
{
    size_t node = 0;
    while (true) {
        const int featureIdx = tree->nodes[node].featureIdx;
        if (featureIdx == -1) {
            return tree->nodes[node].value;
        }

        size_t nextNode;
        if (featureIdx >= (int)nbFeatures) {
            nextNode = tree->nodes[node].missingChildIdx;
        } else {
            const float featureValue = features[featureIdx];

            if (ZL_UNLIKELY(isnan(featureValue))) {
                nextNode = tree->nodes[node].missingChildIdx;
            } else {
                const bool less = featureValue < tree->nodes[node].value;
                nextNode        = less ? tree->nodes[node].leftChildIdx
                                       : tree->nodes[node].rightChildIdx;
            }
        }

        ZL_ASSERT_GT(nextNode, node);
        ZL_ASSERT_LT(nextNode, tree->numNodes);
        node = nextNode;
    }
}

float GBTPredictor_Forest_evaluate(
        const GBTPredictor_Forest* forest,
        const float* features,
        size_t nbFeatures)
{
    float value = 0;
    for (size_t treeIdx = 0; treeIdx < forest->numTrees; treeIdx++) {
        value += GBTPredictor_Tree_evaluate(
                &forest->trees[treeIdx], features, nbFeatures);
    }
    return value;
}

size_t GBTPredictor_predict(
        const GBTPredictor* predictor,
        const float* features,
        size_t nbFeatures)
{
    if (predictor->forests == NULL) {
        // Empty model, always choose the first class
        return 0;
    }

    size_t maxInd  = 0;
    float maxValue = -INFINITY;
    for (size_t forestIdx = 0; forestIdx < predictor->numForests; forestIdx++) {
        const float currentValue = GBTPredictor_Forest_evaluate(
                &predictor->forests[forestIdx], features, nbFeatures);
        if (currentValue > maxValue) {
            maxValue = currentValue;
            maxInd   = forestIdx;
        }
    }

    if (predictor->numForests == 1) {
        if (maxValue < 0) {
            return 0;
        }
        return 1;
    }

    return maxInd;
}

size_t GBTPredictor_getNumClasses(const GBTPredictor* predictor)
{
    if (predictor->numForests == 1) {
        // binary classification
        return 2;
    }
    ZL_ASSERT_NE(predictor->numForests, 2);
    return predictor->numForests;
}

ZL_RESULT_OF(size_t)
GBTModel_predictInd(const GBTModel* model, const ZL_Input* in, ZL_Graph* graph)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    VECTOR(LabeledFeature) featuresMap = VECTOR_EMPTY(kMaxFeaturesCapacity);
    const ZL_Report report = model->featureGenerator(in, &featuresMap);

    if (ZL_isError(report)) {
        VECTOR_DESTROY(featuresMap);
        ZL_ERR_IF_ERR(report);
    }

    float* featuresData = (float*)malloc(model->nbFeatures * sizeof(float));
    if (featuresData == NULL) {
        VECTOR_DESTROY(featuresMap);
        ZL_ERR(allocation);
    }

    for (size_t i = 0; i < model->nbFeatures; i++) {
        featuresData[i] = NAN;
        for (size_t j = 0; j < VECTOR_SIZE(featuresMap); j++) {
            if (!strcmp(VECTOR_AT(featuresMap, j).label,
                        model->featureLabels[i])) {
                featuresData[i] = VECTOR_AT(featuresMap, j).value;
            }
        }
    }

    const size_t classInd = GBTPredictor_predict(
            model->predictor, featuresData, model->nbFeatures);
    free(featuresData);
    VECTOR_DESTROY(featuresMap);
    ZL_ERR_IF_GE(
            classInd,
            model->nbSuccessors,
            GENERIC,
            "Predicted class index larger than number of successors");
    return ZL_RESULT_WRAP_VALUE(size_t, classInd);
}

ZL_RESULT_OF(size_t)
GBTModel_predict(const GBTModel* model, const ZL_Input* in)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, NULL);
    return GBTModel_predictInd(model, in, NULL);
}

size_t GBTModel_Desc_predict(const void* opaque, const ZL_Input* in)
{
    const GBTModel* model = (const GBTModel*)opaque;
    ZL_RESULT_OF(size_t)
    result = GBTModel_predict(model, in);
    if (ZL_RES_isError(result)) {
        return model->nbSuccessors;
    }
    return ZL_RES_value(result);
}

ZL_Report GBTPredictor_validate_forest(
        const GBTPredictor* predictor,
        const size_t forest_idx,
        const int nbFeatures)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    const GBTPredictor_Forest* forest = &predictor->forests[forest_idx];
    ZL_ERR_IF_NULL(
            forest->trees,
            GENERIC,
            "GBTModel's %u forest's tree array is null",
            forest_idx);

    for (size_t j = 0; j < forest->numTrees; j++) {
        const GBTPredictor_Tree* tree = &forest->trees[j];
        ZL_ERR_IF_NULL(
                tree->nodes,
                GENERIC,
                "GBTModel's %u forest's %u tree is null",
                forest_idx,
                j);
        ZL_ERR_IF_ERR(GBTPredictor_validate_tree(tree, nbFeatures));
    }

    return ZL_returnSuccess();
}

ZL_Report GBTPredictor_validate_tree(
        const GBTPredictor_Tree* tree,
        int nbFeatures)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    for (size_t currNodeIdx = 0; currNodeIdx < tree->numNodes; currNodeIdx++) {
        const GBTPredictor_Node node = tree->nodes[currNodeIdx];

        // Feature index is out of bounds
        if (nbFeatures != -1) {
            // Only check if available
            ZL_ERR_IF_GE(
                    node.featureIdx,
                    nbFeatures,
                    GENERIC,
                    "Feature index is out of bounds");
        }

        ZL_ERR_IF_LT(
                node.featureIdx, -1, GENERIC, "Feature index is out of bounds");

        // If feature index is -1, then node is leaf node so we do
        // not need to verify left/right/missing child indices
        if (node.featureIdx == -1) {
            continue;
        }

        // Verify that the node value is a valid numeric float
        ZL_ERR_IF(isnan(node.value), GENERIC, "Node value is nan");
        ZL_ERR_IF(
                isinf(node.value),
                GENERIC,
                "Node value is positive or negative infinity");

        // If children indices come before the current node index
        // then there might be a cycle, we expect the index to always
        // advance
        ZL_ERR_IF_GE(
                currNodeIdx,
                node.leftChildIdx,
                GENERIC,
                "Left child index is less than current node index");
        ZL_ERR_IF_GE(
                currNodeIdx,
                node.rightChildIdx,
                GENERIC,
                "Right child index is less than current node index");
        ZL_ERR_IF_GE(
                currNodeIdx,
                node.missingChildIdx,
                GENERIC,
                "Missing child index is less than current node index");

        // Check if child indices are out of bounds
        ZL_ERR_IF_GE(
                node.leftChildIdx,
                tree->numNodes,
                GENERIC,
                "Left child index is out of bounds");
        ZL_ERR_IF_GE(
                node.rightChildIdx,
                tree->numNodes,
                GENERIC,
                "Right child index is out of bounds");
        ZL_ERR_IF_GE(
                node.missingChildIdx,
                tree->numNodes,
                GENERIC,
                "Missing child index is out of bounds");
    }
    return ZL_returnSuccess();
}

ZL_Report GBTPredictor_validate(const GBTPredictor* predictor, int nbFeatures)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    for (size_t i = 0; i < predictor->numForests; i++) {
        ZL_ERR_IF_ERR(GBTPredictor_validate_forest(predictor, i, nbFeatures));
    }
    return ZL_returnSuccess();
}

ZL_Report GBTModel_validate(const GBTModel* model)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    // Check if any model elements are null
    ZL_ERR_IF_NULL(model, GENERIC, "GBTModel is null");
    ZL_ERR_IF_NULL(model->predictor, GENERIC, "GBTModel's predictor is null");
    ZL_ERR_IF_NULL(
            model->featureLabels,
            GENERIC,
            "GBTModel's featureLabels array is null");
    ZL_ERR_IF_NULL(
            model->predictor->forests, GENERIC, "GBTModel's forests is null");

    if (model->predictor->numForests == 1) {
        ZL_ERR_IF_NE(
                2,
                model->nbSuccessors,
                GENERIC,
                "There is only one forest(binary classification), but number of successors is not 2");
    } else {
        ZL_ERR_IF_NE(
                model->predictor->numForests,
                model->nbSuccessors,
                GENERIC,
                "Multiclass classification. Number of forests not equal to number of successors");
    }

    ZL_ERR_IF_ERR(
            GBTPredictor_validate(model->predictor, (int)model->nbFeatures));
    return ZL_returnSuccess();
}

ZL_GraphID ZL_Compressor_registerGBTModelGraph(
        ZL_Compressor* cgraph,
        const GBTModel* gbtModel,
        ZL_LabeledGraphID* labeledGraphs,
        size_t labeledGraphsSize)
{
    ZL_Report report = GBTModel_validate(gbtModel);
    if (ZL_isError(report)) {
        return ZL_GRAPH_ILLEGAL;
    }

    ZS2_MLModel_Desc zs2_model = {
        .predict = GBTModel_Desc_predict,
        .free    = NULL,
        .opaque  = gbtModel,
    };

    ZL_MLSelectorDesc mlSelector = {
        .model        = zs2_model,
        .inStreamType = ZL_Type_numeric,
        .graphs       = labeledGraphs,
        .nbGraphs     = labeledGraphsSize,
        .name         = NULL,
    };

    ZL_GraphID result =
            ZL_Compressor_registerMLSelectorGraph(cgraph, &mlSelector);

    return result;
}
