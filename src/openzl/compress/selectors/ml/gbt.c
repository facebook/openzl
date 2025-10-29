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
        if (maxValue < 0.5) {
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
    const ZL_Report report =
            model->featureGenerator(in, &featuresMap, model->featureContext);

    if (ZL_isError(report)) {
        VECTOR_DESTROY(featuresMap);
        ZL_ERR_IF_ERR(report, "Error in generating features");
    }

    float* featuresData = (float*)malloc(model->nbFeatures * sizeof(float));
    if (featuresData == NULL) {
        VECTOR_DESTROY(featuresMap);
        ZL_ERR(allocation, "Error allocating features");
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
            model->nbLabels,
            GENERIC,
            "Predicted class index larger than number of classes");
    return ZL_RESULT_WRAP_VALUE(size_t, classInd);
}

ZL_RESULT_OF(Label)
GBTModel_predict(const GBTModel* model, const ZL_Input* in)
{
    ZL_RESULT_DECLARE_SCOPE(Label, NULL);

    ZL_TRY_LET_CONST(size_t, classInd, GBTModel_predictInd(model, in, NULL));
    const Label classification = model->classLabels[classInd];
    return ZL_RESULT_WRAP_VALUE(Label, classification);
}

const char* GBTModel_Desc_predict(const void* opaque, const ZL_Input* in)
{
    const GBTModel* model = (const GBTModel*)opaque;
    ZL_RESULT_OF(Label)
    result = GBTModel_predict(model, in);
    if (ZL_RES_isError(result)) {
        return "";
    }
    const char* decodedLabel = ZL_RES_value(result);
    return decodedLabel;
}

ZL_Report GBTPredictor_validate_forest(
        const GBTPredictor* predictor,
        const size_t forest_idx,
        const int nbFeatures)
{
    const GBTPredictor_Forest* forest = &predictor->forests[forest_idx];
    ZL_RET_R_IF_NULL(
            GENERIC,
            forest->trees,
            "GBTModel's %u forest's tree array is null",
            forest_idx);

    for (size_t j = 0; j < forest->numTrees; j++) {
        const GBTPredictor_Tree* tree = &forest->trees[j];
        ZL_RET_R_IF_NULL(
                GENERIC,
                tree->nodes,
                "GBTModel's %u forest's %u tree is null",
                forest_idx,
                j);
        ZL_RET_R_IF_ERR(GBTPredictor_validate_tree(tree, nbFeatures));
    }

    return ZL_returnSuccess();
}

ZL_Report GBTPredictor_validate_tree(
        const GBTPredictor_Tree* tree,
        int nbFeatures)
{
    for (size_t currNodeIdx = 0; currNodeIdx < tree->numNodes; currNodeIdx++) {
        const GBTPredictor_Node node = tree->nodes[currNodeIdx];

        // Feature index is out of bounds
        if (nbFeatures != -1) {
            // Only check if available
            ZL_RET_R_IF_GE(
                    GENERIC,
                    node.featureIdx,
                    nbFeatures,
                    "Feature index is out of bounds");
        }

        ZL_RET_R_IF_LT(
                GENERIC, node.featureIdx, -1, "Feature index is out of bounds");

        // If feature index is -1, then node is leaf node so we do
        // not need to verify left/right/missing child indices
        if (node.featureIdx == -1) {
            continue;
        }

        // Verify that the node value is a valid numeric float
        ZL_RET_R_IF(GENERIC, isnan(node.value), "Node value is nan");
        ZL_RET_R_IF(
                GENERIC,
                isinf(node.value),
                "Node value is positive or negative infinity");

        // If children indices come before the current node index
        // then there might be a cycle, we expect the index to always
        // advance
        ZL_RET_R_IF_GE(
                GENERIC,
                currNodeIdx,
                node.leftChildIdx,
                "Left child index is less than current node index");
        ZL_RET_R_IF_GE(
                GENERIC,
                currNodeIdx,
                node.rightChildIdx,
                "Right child index is less than current node index");
        ZL_RET_R_IF_GE(
                GENERIC,
                currNodeIdx,
                node.missingChildIdx,
                "Missing child index is less than current node index");

        // Check if child indices are out of bounds
        ZL_RET_R_IF_GE(
                GENERIC,
                node.leftChildIdx,
                tree->numNodes,
                "Left child index is out of bounds");
        ZL_RET_R_IF_GE(
                GENERIC,
                node.rightChildIdx,
                tree->numNodes,
                "Right child index is out of bounds");
        ZL_RET_R_IF_GE(
                GENERIC,
                node.missingChildIdx,
                tree->numNodes,
                "Missing child index is out of bounds");
    }
    return ZL_returnSuccess();
}

ZL_Report GBTPredictor_validate(const GBTPredictor* predictor, int nbFeatures)
{
    for (size_t i = 0; i < predictor->numForests; i++) {
        ZL_RET_R_IF_ERR(GBTPredictor_validate_forest(predictor, i, nbFeatures));
    }
    return ZL_returnSuccess();
}

ZL_Report GBTModel_validate(const GBTModel* model)
{
    // Check if any model elements are null
    ZL_RET_R_IF_NULL(GENERIC, model, "GBTModel is null");
    ZL_RET_R_IF_NULL(GENERIC, model->predictor, "GBTModel's predictor is null");
    ZL_RET_R_IF_NULL(
            GENERIC,
            model->classLabels,
            "GBTModel's classLabels array is null");
    ZL_RET_R_IF_NULL(
            GENERIC,
            model->featureLabels,
            "GBTModel's featureLabels array is null");
    ZL_RET_R_IF_NULL(
            GENERIC, model->predictor->forests, "GBTModel's forests is null");

    ZL_RET_R_IF_ERR(
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
