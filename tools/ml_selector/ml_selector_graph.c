// Copyright (c) Meta Platforms, Inc. and affiliates.
// Note: This file is work in progress and is not ready for use yet.

#include "tools/ml_selector/ml_selector_graph.h"
#include "openzl/common/a1cbor_helpers.h"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_reflection.h"

/**
 * Declare types in order to use ZL_RESULT_OF, this allows function to return
 * either success values or errors rather than using out-parameters
 */
ZL_RESULT_DECLARE_TYPE(GBTPredictor);
ZL_RESULT_DECLARE_TYPE(GBTModel);
ZL_RESULT_DECLARE_TYPE(GBTPredictor_Forest);
ZL_RESULT_DECLARE_TYPE(GBTPredictor_Tree);
ZL_RESULT_DECLARE_TYPE(GBTPredictor_Node);

static void* MLSel_arenaCalloc(void* opaque, size_t size)
{
    void* buffer = ZL_Graph_getScratchSpace((ZL_Graph*)opaque, size);
    if (buffer != NULL) {
        memset(buffer, 0, size);
    }
    return buffer;
}

static A1C_Arena MLSel_wrapArena(ZL_Graph* graph)
{
    A1C_Arena arena;
    arena.calloc = MLSel_arenaCalloc;
    arena.opaque = graph;
    return arena;
}

static ZL_RESULT_OF(ZL_MLSelectorConfig) MLSel_getConfig(ZL_Graph* graph)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    ZL_RefParam configInfo =
            ZL_Graph_getLocalRefParam(graph, ZL_GENERIC_ML_SELECTOR_CONFIG_ID);
    const char* serializedConfig = configInfo.paramRef;

    size_t configSize = configInfo.paramSize;
    /**
     * a1cArena is used to decode config, memory is automatically freed
     *  since we are using scratch space from ZL_Graph_getScratchSpace
     */
    A1C_Arena a1cArena = MLSel_wrapArena(graph);
    return MLSelector_deserializeMLSelectorConfig(
            ZL_ERR_CTX_PTR, serializedConfig, configSize, &a1cArena);
}

/** @brief Retrieves list of successors and ZL_MLSelectorConfig from graph and
 * selects successor based on prediction made by model specified inside the
 * ZL_MLSelectorConfig.
 *
 * @param graph      Graph containing ZL_MLSelectorConfig and list of successors
 * @param inputs     Array of input edges to be routed to selected successor
 * @param nbInputs   Number of input edges in the inputs array
 * @return           Failure if unable to get config from graph or if the
 * selected successor is out of bounds or if unable to select successor. Success
 * otherwise.
 */
static ZL_Report
MLSel_compress(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_TRY_LET_T(ZL_MLSelectorConfig, config, MLSel_getConfig(graph));

    ZL_GraphIDList succList = ZL_Graph_getCustomGraphs(graph);

    if (config.model == ZL_GBT) {
        GBTModel* gbt = config.runtimeConfig;
        ZL_ERR_IF_NE(
                gbt->nbLabels,
                succList.nbGraphIDs,
                successor_invalid,
                "GBT model has %zu labels, but graph has %zu successors",
                gbt->nbLabels,
                succList.nbGraphIDs);
        for (size_t ind = 0; ind < nbInputs; ind++) {
            ZL_TRY_LET(
                    size_t,
                    selectedSuccessor,
                    GBTModel_predictInd(
                            gbt, ZL_Edge_getData(inputs[ind]), graph));
            ZL_ERR_IF_GE(
                    selectedSuccessor,
                    succList.nbGraphIDs,
                    successor_invalid,
                    "Selected successor is out of bounds");
            ZL_GraphID succ = succList.graphids[selectedSuccessor];
            ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputs[ind], succ));
        }
    } else {
        return ZL_returnError(ZL_ErrorCode_graph_invalid);
    }

    return ZL_returnSuccess();
}

static ZL_Report GBTModel_serializeNode(
        ZL_ErrorContext* errCtx,
        A1C_Item* parent,
        A1C_Arena* a1cArena,
        const GBTPredictor_Node* node)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    A1C_MapBuilder nodeMapBuilder = A1C_Item_map_builder(parent, 5, a1cArena);
    {
        A1C_MAP_TRY_ADD_R(pair, nodeMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "featureIdx");
        A1C_Item_int64(&pair->val, (A1C_Int64)node->featureIdx);
    }
    {
        A1C_MAP_TRY_ADD_R(pair, nodeMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "value");
        A1C_Item_float64(&pair->val, node->value);
    }
    {
        A1C_MAP_TRY_ADD_R(pair, nodeMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "leftChildIdx");
        A1C_Item_int64(&pair->val, (A1C_Int64)node->leftChildIdx);
    }
    {
        A1C_MAP_TRY_ADD_R(pair, nodeMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "rightChildIdx");
        A1C_Item_int64(&pair->val, (A1C_Int64)node->rightChildIdx);
    }
    {
        A1C_MAP_TRY_ADD_R(pair, nodeMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "missingChildIdx");
        A1C_Item_int64(&pair->val, (A1C_Int64)node->missingChildIdx);
    }
    return ZL_returnSuccess();
}

static ZL_Report GBTModel_serializeTree(
        ZL_ErrorContext* errCtx,
        A1C_Item* parent,
        A1C_Arena* a1cArena,
        const GBTPredictor_Tree* tree)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    A1C_MapBuilder treeMapBuilder = A1C_Item_map_builder(parent, 2, a1cArena);
    {
        A1C_MAP_TRY_ADD_R(pair, treeMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "numNodes");
        A1C_Item_int64(&pair->val, (A1C_Int64)tree->numNodes);
    }
    {
        A1C_MAP_TRY_ADD_R(pair, treeMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "nodes");
        A1C_Item* nodes = A1C_Item_array(&pair->val, tree->numNodes, a1cArena);
        ZL_RET_R_IF_NULL(allocation, nodes);
        for (size_t i = 0; i < tree->numNodes; i++) {
            ZL_RET_R_IF_ERR(GBTModel_serializeNode(
                    errCtx, &nodes[i], a1cArena, &tree->nodes[i]));
        }
    }
    return ZL_returnSuccess();
}

static ZL_Report GBTModel_serializeForest(
        ZL_ErrorContext* errCtx,
        A1C_Item* parent,
        A1C_Arena* a1cArena,
        const GBTPredictor_Forest* forest)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    A1C_MapBuilder forestMapBuilder = A1C_Item_map_builder(parent, 2, a1cArena);
    {
        A1C_MAP_TRY_ADD_R(pair, forestMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "numTrees");
        A1C_Item_int64(&pair->val, (A1C_Int64)forest->numTrees);
    }
    {
        A1C_MAP_TRY_ADD_R(pair, forestMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "trees");
        A1C_Item* trees =
                A1C_Item_array(&pair->val, forest->numTrees, a1cArena);
        ZL_RET_R_IF_NULL(allocation, trees);
        for (size_t i = 0; i < forest->numTrees; i++) {
            ZL_RET_R_IF_ERR(GBTModel_serializeTree(
                    errCtx, &trees[i], a1cArena, &forest->trees[i]));
        }
    }
    return ZL_returnSuccess();
}

static ZL_Report GBTModel_serializePredictor(
        ZL_ErrorContext* errCtx,
        A1C_Item* parent,
        A1C_Arena* a1cArena,
        const GBTPredictor* predictor)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    A1C_MapBuilder predictorMapBuilder =
            A1C_Item_map_builder(parent, 2, a1cArena);
    {
        A1C_MAP_TRY_ADD_R(pair, predictorMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "numForests");
        A1C_Item_int64(&pair->val, (A1C_Int64)predictor->numForests);
    }
    {
        A1C_MAP_TRY_ADD_R(pair, predictorMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "forests");
        A1C_Item* forests =
                A1C_Item_array(&pair->val, predictor->numForests, a1cArena);
        ZL_RET_R_IF_NULL(allocation, forests);
        for (size_t i = 0; i < predictor->numForests; i++) {
            ZL_RET_R_IF_ERR(GBTModel_serializeForest(
                    errCtx, &forests[i], a1cArena, &predictor->forests[i]));
        }
    }
    return ZL_returnSuccess();
}

/**
 * @brief serializes the @p model using @p a1cArena for allocations. All
 * allocated memory is tied to @p a1cArena 's underlying arena. Serialized data
 * remains valid until arena is freed. When caller frees the arena, all memory
 * is cleaned up.
 *
 * @returns The ZL_Report result of the serialization. Returns an error if the
 * config is malformed or if any allocation failure happens.
 * @param errCtx Error context for reporting errors
 * @param model The GBTModel to be serialized
 * @param parent The parent item to which the serialized data is added
 * @param a1cArena The arena wrapper in which memory allocations for
 * serialization happens
 */
static ZL_Report GBTModel_serialize(
        ZL_ErrorContext* errCtx,
        const GBTModel* model,
        A1C_Item* parent,
        A1C_Arena* a1cArena)
{
    A1C_MapBuilder rootMapBuilder = A1C_Item_map_builder(parent, 6, a1cArena);

    // Serialize predictor
    {
        A1C_MAP_TRY_ADD_R(pair, rootMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "predictor");
        ZL_RET_R_IF_ERR(GBTModel_serializePredictor(
                errCtx, &pair->val, a1cArena, model->predictor));
    }
    // Serialize featureGenerator using enum
    {
        FeatureGenId fg = FeatureGen_getId(model->featureGenerator);
        ZL_RET_R_IF_EQ(invalidName, fg, FeatureGenId_Invalid);

        A1C_MAP_TRY_ADD_R(pair, rootMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "featureGenerator");
        A1C_Item_int64(&pair->val, (A1C_Int64)fg);
    }

    // Serialize nbLabels
    {
        A1C_MAP_TRY_ADD_R(pair, rootMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "nbLabels");
        A1C_Item_int64(&pair->val, (A1C_Int64)model->nbLabels);
    }

    // Serialize classLabels array
    {
        A1C_MAP_TRY_ADD_R(pair, rootMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "classLabels");
        A1C_Item* labels =
                A1C_Item_array(&pair->val, model->nbLabels, a1cArena);
        ZL_RET_R_IF_NULL(allocation, labels);
        for (size_t i = 0; i < model->nbLabels; i++) {
            A1C_Item_string_refCStr(&labels[i], model->classLabels[i]);
        }
    }

    // Serialize nbFeatures
    {
        A1C_MAP_TRY_ADD_R(pair, rootMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "nbFeatures");
        A1C_Item_int64(&pair->val, (A1C_Int64)model->nbFeatures);
    }

    // Serialize featureLabels array
    {
        A1C_MAP_TRY_ADD_R(pair, rootMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "featureLabels");
        A1C_Item* labels =
                A1C_Item_array(&pair->val, model->nbFeatures, a1cArena);
        ZL_RET_R_IF_NULL(allocation, labels);
        for (size_t i = 0; i < model->nbFeatures; i++) {
            A1C_Item_string_refCStr(&labels[i], model->featureLabels[i]);
        }
    }

    return ZL_returnSuccess();
}
static ZL_RESULT_OF(GBTPredictor_Node) GBTModel_deserializeNode(
        ZL_ErrorContext* errCtx,
        const A1C_Item* a1cItem)
{
    GBTPredictor_Node node = { 0 };
    ZL_RESULT_DECLARE_SCOPE(GBTPredictor_Node, errCtx);
    A1C_TRY_EXTRACT_MAP(nodeMap, a1cItem);

    A1C_TRY_EXTRACT_INT64(featureIdx, A1C_Map_get_cstr(&nodeMap, "featureIdx"));
    node.featureIdx = (int)featureIdx;

    A1C_TRY_EXTRACT_FLOAT64(value, A1C_Map_get_cstr(&nodeMap, "value"));
    node.value = (float)value;

    A1C_TRY_EXTRACT_INT64(
            leftChildIdx, A1C_Map_get_cstr(&nodeMap, "leftChildIdx"));
    node.leftChildIdx = (size_t)leftChildIdx;

    A1C_TRY_EXTRACT_INT64(
            rightChildIdx, A1C_Map_get_cstr(&nodeMap, "rightChildIdx"));
    node.rightChildIdx = (size_t)rightChildIdx;

    A1C_TRY_EXTRACT_INT64(
            missingChildIdx, A1C_Map_get_cstr(&nodeMap, "missingChildIdx"));
    node.missingChildIdx = (size_t)missingChildIdx;

    return ZL_WRAP_VALUE(node);
}

static ZL_RESULT_OF(GBTPredictor_Tree) GBTModel_deserializeTree(
        ZL_ErrorContext* errCtx,
        const A1C_Item* treeItem,
        A1C_Arena* a1cArena)
{
    GBTPredictor_Tree tree = { 0 };
    ZL_RESULT_DECLARE_SCOPE(GBTPredictor_Tree, errCtx);
    A1C_TRY_EXTRACT_MAP(treeMap, treeItem);

    A1C_TRY_EXTRACT_INT64(numNodes, A1C_Map_get_cstr(&treeMap, "numNodes"));
    tree.numNodes = (size_t)numNodes;

    A1C_TRY_EXTRACT_ARRAY(nodesArray, A1C_Map_get_cstr(&treeMap, "nodes"));
    ZL_ERR_IF_NE(
            nodesArray.size,
            tree.numNodes,
            GENERIC,
            "Tree numNodes doesn't match nodes array size");

    GBTPredictor_Node* nodes = a1cArena->calloc(
            a1cArena->opaque, sizeof(GBTPredictor_Node) * tree.numNodes);

    for (size_t i = 0; i < tree.numNodes; i++) {
        const A1C_Item* nodeItem = A1C_Array_get(&nodesArray, i);
        ZL_ERR_IF_NULL(nodeItem, corruption);
        ZL_TRY_LET(
                GBTPredictor_Node,
                node,
                GBTModel_deserializeNode(errCtx, nodeItem));
        nodes[i] = node;
    }

    tree.nodes = nodes;
    return ZL_WRAP_VALUE(tree);
}

static ZL_RESULT_OF(GBTPredictor_Forest) GBTModel_deserializeForest(
        ZL_ErrorContext* errCtx,
        const A1C_Item* forestItem,
        A1C_Arena* a1cArena)
{
    GBTPredictor_Forest forest = { 0 };
    ZL_RESULT_DECLARE_SCOPE(GBTPredictor_Forest, errCtx);
    A1C_TRY_EXTRACT_MAP(forestMap, forestItem);

    A1C_TRY_EXTRACT_INT64(numTrees, A1C_Map_get_cstr(&forestMap, "numTrees"));
    forest.numTrees = (size_t)numTrees;

    A1C_TRY_EXTRACT_ARRAY(treesArray, A1C_Map_get_cstr(&forestMap, "trees"));
    ZL_ERR_IF_NE(
            treesArray.size,
            forest.numTrees,
            GENERIC,
            "Forest numTrees doesn't match trees array size");

    GBTPredictor_Tree* trees = a1cArena->calloc(
            a1cArena->opaque, sizeof(GBTPredictor_Tree) * forest.numTrees);

    for (size_t i = 0; i < forest.numTrees; i++) {
        const A1C_Item* treeItem = A1C_Array_get(&treesArray, i);
        ZL_ERR_IF_NULL(treeItem, corruption);
        ZL_TRY_LET(
                GBTPredictor_Tree,
                tree,
                GBTModel_deserializeTree(errCtx, treeItem, a1cArena));
        trees[i] = tree;
    }

    forest.trees = trees;
    return ZL_WRAP_VALUE(forest);
}

static ZL_RESULT_OF(GBTPredictor) GBTModel_deserializePredictor(
        ZL_ErrorContext* errCtx,
        const A1C_Item* predictorItem,
        A1C_Arena* a1cArena)
{
    GBTPredictor predictor = { 0 };
    ZL_RESULT_DECLARE_SCOPE(GBTPredictor, errCtx);
    A1C_TRY_EXTRACT_MAP(predictorMap, predictorItem);

    A1C_TRY_EXTRACT_INT64(
            numForests, A1C_Map_get_cstr(&predictorMap, "numForests"));
    predictor.numForests = (size_t)numForests;

    A1C_TRY_EXTRACT_ARRAY(
            forestsArray, A1C_Map_get_cstr(&predictorMap, "forests"));
    ZL_ERR_IF_NE(
            forestsArray.size,
            predictor.numForests,
            GENERIC,
            "Predictor numForests doesn't match forests array size");

    GBTPredictor_Forest* forests = a1cArena->calloc(
            a1cArena->opaque,
            sizeof(GBTPredictor_Forest) * predictor.numForests);

    for (size_t i = 0; i < predictor.numForests; i++) {
        const A1C_Item* forestItem = A1C_Array_get(&forestsArray, i);
        ZL_ERR_IF_NULL(forestItem, corruption);
        ZL_TRY_LET(
                GBTPredictor_Forest,
                forest,
                GBTModel_deserializeForest(errCtx, forestItem, a1cArena));
        forests[i] = forest;
    }

    predictor.forests = forests;
    return ZL_WRAP_VALUE(predictor);
}

/** @brief Deserializes the @p model and returns the result. Uses @p a1cArena
 * to initialize decoder, memory is automatically cleaned when graph execution
 * completes.
 *
 * @returns Failure if the config is invalid or an allocation fails. On success
 * returns success status and the deserialized config.
 * @param errCtx Error context for reporting errors
 * @param parent root A1C_Item containing CBOR data
 * @param a1cArena The arena wrapper needed for deserialization
 */
static ZL_RESULT_OF(GBTModel) GBTModel_deserialize(
        ZL_ErrorContext* errCtx,
        const A1C_Item* parent,
        A1C_Arena* a1cArena)
{
    GBTModel model;
    ZL_RESULT_DECLARE_SCOPE(GBTModel, errCtx);
    A1C_TRY_EXTRACT_MAP(rootMap, parent);

    // Deserialize predictor
    const A1C_Item* predictorItem = A1C_Map_get_cstr(&rootMap, "predictor");
    ZL_ERR_IF_NULL(predictorItem, corruption);

    ZL_TRY_LET(
            GBTPredictor,
            predictorValue,
            GBTModel_deserializePredictor(errCtx, predictorItem, a1cArena));

    GBTPredictor* predictor =
            a1cArena->calloc(a1cArena->opaque, sizeof(GBTPredictor));

    ZL_ERR_IF_NULL(predictor, allocation);
    *predictor      = predictorValue;
    model.predictor = predictor;

    // Deserialize featureGenerator using enum
    A1C_TRY_EXTRACT_INT64(
            featureGeneratorEnum,
            A1C_Map_get_cstr(&rootMap, "featureGenerator"));

    ZL_RESULT_OF(FeatureGenerator)
    featureGenerator =
            FeatureGen_getFeatureGen((FeatureGenId)featureGeneratorEnum);
    ZL_ERR_IF_ERR(featureGenerator);
    model.featureGenerator = ZL_RES_value(featureGenerator);

    // Deserialize nbLabels
    A1C_TRY_EXTRACT_INT64(nbLabels, A1C_Map_get_cstr(&rootMap, "nbLabels"));
    model.nbLabels = (size_t)nbLabels;

    // Deserialize classLabels array
    A1C_TRY_EXTRACT_ARRAY(
            classLabelsArray, A1C_Map_get_cstr(&rootMap, "classLabels"));
    ZL_ERR_IF_NE(
            classLabelsArray.size,
            model.nbLabels,
            GENERIC,
            "nbLabels doesn't match classLabels array size");

    Label* classLabels =
            a1cArena->calloc(a1cArena->opaque, model.nbLabels * sizeof(Label));
    for (size_t i = 0; i < model.nbLabels; i++) {
        const A1C_Item* labelItem = A1C_Array_get(&classLabelsArray, i);
        ZL_ERR_IF_NULL(labelItem, corruption);
        A1C_TRY_EXTRACT_STRING(labelStr, labelItem);

        // Allocate space for the string and copy it (with null terminator)
        char* label = (char*)a1cArena->calloc(
                a1cArena->opaque, (labelStr.size + 1) * sizeof(char));
        ZL_ERR_IF_NULL(label, allocation);
        memcpy(label, labelStr.data, labelStr.size);
        label[labelStr.size] = '\0';
        classLabels[i]       = label;
    }
    model.classLabels = classLabels;

    // Deserialize nbFeatures
    A1C_TRY_EXTRACT_INT64(nbFeatures, A1C_Map_get_cstr(&rootMap, "nbFeatures"));
    model.nbFeatures = (size_t)nbFeatures;

    // Deserialize featureLabels array
    A1C_TRY_EXTRACT_ARRAY(
            featureLabelsArray, A1C_Map_get_cstr(&rootMap, "featureLabels"));
    ZL_ERR_IF_NE(
            featureLabelsArray.size,
            model.nbFeatures,
            GENERIC,
            "nbFeatures doesn't match featureLabels array size");

    Label* featureLabels = a1cArena->calloc(
            a1cArena->opaque, model.nbFeatures * sizeof(Label));

    for (size_t i = 0; i < model.nbFeatures; i++) {
        const A1C_Item* labelItem = A1C_Array_get(&featureLabelsArray, i);
        ZL_ERR_IF_NULL(labelItem, corruption);
        A1C_TRY_EXTRACT_STRING(labelStr, labelItem);

        // Allocate space for the string and copy it (with null terminator)
        char* label = (char*)a1cArena->calloc(
                a1cArena->opaque, (labelStr.size + 1) * sizeof(char));
        ZL_ERR_IF_NULL(label, allocation);
        memcpy(label, labelStr.data, labelStr.size);
        label[labelStr.size] = '\0';
        featureLabels[i]     = label;
    }
    model.featureLabels = featureLabels;

    return ZL_WRAP_VALUE(model);
}
ZL_RESULT_OF(ZL_SerializedMLConfig)
MLSelector_serializeMLSelectorConfig(
        ZL_ErrorContext* errCtx,
        const ZL_MLSelectorConfig* config,
        A1C_Arena* arena)
{
    ZL_SerializedMLConfig dst = { .data = NULL, .size = 0 };
    ZL_RESULT_DECLARE_SCOPE(ZL_SerializedMLConfig, errCtx);
    A1C_Item* root = A1C_Item_root(arena);
    ZL_ERR_IF_NULL(root, allocation);
    // serialize model type
    A1C_MapBuilder rootMapBuilder = A1C_Item_map_builder(root, 2, arena);
    {
        A1C_MAP_TRY_ADD_T(ZL_SerializedMLConfig, pair, rootMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "model");
        A1C_Item_int64(&pair->val, (A1C_Int64)config->model);
    }
    // Serialize predictor
    {
        A1C_MAP_TRY_ADD_T(ZL_SerializedMLConfig, pair, rootMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "runtimeConfig");
        if (config->model == ZL_GBT) {
            ZL_RET_T_IF_ERR(
                    ZL_SerializedMLConfig,
                    GBTModel_serialize(
                            errCtx, config->runtimeConfig, &pair->val, arena));
        } else {
            ZL_RET_T_ERR(
                    ZL_SerializedMLConfig,
                    graph_invalid,
                    "model type not supported");
        }
    }
    dst.size = A1C_Item_encodedSize(root);
    dst.data = arena->calloc(arena->opaque, dst.size);
    ZL_ERR_IF_NULL(dst.data, allocation);
    A1C_Error error;
    size_t res = A1C_Item_encode(root, (uint8_t*)dst.data, dst.size, &error);
    if (res == 0) {
        ZL_RET_T_WRAP_ERR(
                ZL_SerializedMLConfig, A1C_Error_convert(NULL, error));
    }
    ZL_RET_T_IF_NE(ZL_SerializedMLConfig, allocation, res, dst.size);
    return ZL_RESULT_WRAP_VALUE(ZL_SerializedMLConfig, dst);
}

ZL_RESULT_OF(ZL_MLSelectorConfig)
MLSelector_deserializeMLSelectorConfig(
        ZL_ErrorContext* errCtx,
        const char* config,
        size_t size,
        A1C_Arena* arena)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    ZL_MLSelectorConfig dst;
    A1C_Decoder decoder;
    A1C_DecoderConfig decoderConfig =
            (A1C_DecoderConfig){ .maxDepth            = 0,
                                 .limitBytes          = 0,
                                 .referenceSource     = true,
                                 .rejectUnknownSimple = true };
    A1C_Decoder_init(&decoder, *arena, decoderConfig);
    const A1C_Item* root =
            A1C_Decoder_decode(&decoder, (const uint8_t*)config, size);
    ZL_RET_T_IF_NULL(ZL_MLSelectorConfig, allocation, root);

    A1C_TRY_EXTRACT_T_MAP(ZL_MLSelectorConfig, rootMap, root);

    A1C_TRY_EXTRACT_T_INT64(
            ZL_MLSelectorConfig, model, A1C_Map_get_cstr(&rootMap, "model"));
    dst.model           = (ZL_MLSelectorModelType)model;
    void* runtimeConfig = NULL;
    if (dst.model == ZL_GBT) {
        const A1C_Item* runtimeConfigItem =
                A1C_Map_get_cstr(&rootMap, "runtimeConfig");
        ZL_RET_T_IF_NULL(ZL_MLSelectorConfig, corruption, runtimeConfigItem);
        GBTModel* gbtModelCopy =
                (GBTModel*)arena->calloc(arena->opaque, sizeof(GBTModel));

        ZL_RESULT_OF(GBTModel)
        gbtModelResult = GBTModel_deserialize(errCtx, runtimeConfigItem, arena);

        if (ZL_RES_isError(gbtModelResult)) {
            return ZL_RESULT_WRAP_ERROR(
                    ZL_MLSelectorConfig, ZL_RES_error(gbtModelResult));
        }

        *gbtModelCopy = ZL_RES_value(gbtModelResult);
        runtimeConfig = gbtModelCopy;
    }
    dst.runtimeConfig = runtimeConfig;

    return ZL_RESULT_WRAP_VALUE(ZL_MLSelectorConfig, dst);
}

ZL_RESULT_OF(ZL_GraphID)
ZL_MLSelector_registerBaseGraph(ZL_Compressor* compressor)
{
    ZL_GraphID mlSelectorGraph =
            ZL_Compressor_getGraph(compressor, "mlSelector");
    if (mlSelectorGraph.gid == ZL_GRAPH_ILLEGAL.gid) {
        ZL_FunctionGraphDesc mlSelectorGraphDesc = {
            .name           = "!mlSelector",
            .graph_f        = MLSel_compress,
            .inputTypeMasks = (const ZL_Type[]){ ZL_Type_any },
            .nbInputs       = 1,
            .customGraphs   = NULL,
            .nbCustomGraphs = 0,
            .localParams    = {},
        };
        mlSelectorGraph = ZL_Compressor_registerFunctionGraph(
                compressor, &mlSelectorGraphDesc);
    }
    return ZL_RESULT_WRAP_VALUE(ZL_GraphID, mlSelectorGraph);
}

ZL_RESULT_OF(ZL_GraphID)
ZL_MLSelector_registerGraph(
        ZL_Compressor* compressor,
        const ZL_MLSelectorConfig* config,
        const ZL_GraphID* successors,
        size_t nbSuccessors)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);

    // Cannot serialize if config or runtimeConfig is null
    ZL_ERR_IF_NULL(config, graph_nonserializable);
    ZL_ERR_IF_NULL(config->runtimeConfig, graph_nonserializable);

    // Make sure config is valid
    if (config->model == ZL_GBT) {
        GBTModel* gbtModel = (GBTModel*)config->runtimeConfig;
        ZL_ERR_IF_ERR(GBTModel_validate(gbtModel));
        ZL_ERR_IF_LT(
                FeatureGen_getId(gbtModel->featureGenerator),
                0,
                compressionParameter_invalid,
                "Must use standard feature generator");
    }

    // Need separate heap arena to allocate memory for serialized data
    Arena* arena = ALLOC_HeapArena_create();

    // a1cArena wraps the heap arena and is used to encode and serialize data
    A1C_Arena a1cArena = A1C_Arena_wrap(arena);

    ZL_RESULT_OF(ZL_SerializedMLConfig)
    serializedResult = MLSelector_serializeMLSelectorConfig(
            ZL_ERR_CTX_PTR, config, &a1cArena);

    if (ZL_RES_isError(serializedResult)) {
        ALLOC_Arena_freeArena(arena);
        ZL_ERR_IF_ERR(serializedResult);
    }

    ZL_SerializedMLConfig serializedConfig = ZL_RES_value(serializedResult);

    ZL_CopyParam configParam = (ZL_CopyParam){
        .paramId   = ZL_GENERIC_ML_SELECTOR_CONFIG_ID,
        .paramPtr  = serializedConfig.data,
        .paramSize = serializedConfig.size,
    };

    ZL_LocalParams params =
            (ZL_LocalParams){ .copyParams = { .copyParams   = &configParam,
                                              .nbCopyParams = 1 } };

    ZL_RESULT_OF(ZL_GraphID)
    baseGraph = ZL_MLSelector_registerBaseGraph(compressor);

    if (ZL_RES_isError(baseGraph)) {
        return baseGraph;
    }

    ZL_GraphID mlSelectorGraph = ZL_RES_value(baseGraph);

    ZL_ParameterizedGraphDesc const graphDesc = {
        .graph          = mlSelectorGraph,
        .customGraphs   = successors,
        .nbCustomGraphs = nbSuccessors,
        .localParams    = &params,
    };

    const ZL_GraphID graph =
            ZL_Compressor_registerParameterizedGraph(compressor, &graphDesc);

    /**
     * By freeing the arena, we are freeing all the memory used by
     * a1c_arena. We can free arena here because we make a copy param of the
     * serialized config, so the lifetime of the serialized config is tied
     * to the graph.
     */
    ALLOC_Arena_freeArena(arena);
    return ZL_RESULT_WRAP_VALUE(ZL_GraphID, graph);
}

ZL_RESULT_OF(ZL_GraphID)
MLSelector_registerGraphWithEmptyGBTModel(
        ZL_Compressor* compressor,
        const ZL_GraphID* successors,
        size_t nbSuccessors)
{
    const GBTPredictor_Node emptyNode = {
        .featureIdx      = -1, // leaf node
        .value           = 0.0f,
        .leftChildIdx    = 0,
        .rightChildIdx   = 0,
        .missingChildIdx = 0,
    };

    const GBTPredictor_Tree emptyTree = {
        .numNodes = 1,
        .nodes    = &emptyNode,
    };

    const GBTPredictor_Forest emptyForest = {
        .numTrees = 1,
        .trees    = &emptyTree,
    };

    const GBTPredictor emptyPredictor = {
        .numForests = 1,
        .forests    = &emptyForest,
    };

    const Label featureLabels[] = { "placeholder" };

    Arena* arena = ALLOC_HeapArena_create();

    Label* classLabels =
            ALLOC_Arena_malloc(arena, sizeof(Label) * nbSuccessors);

    if (classLabels == NULL) {
        ALLOC_Arena_freeArena(arena);
        ZL_RET_T_ERR(ZL_GraphID, allocation, "Failed to allocate classLabels");
    }

    for (size_t i = 0; i < nbSuccessors; i++) {
        classLabels[i] = ZL_Compressor_Graph_getName(compressor, successors[i]);
    }

    GBTModel emptyModel = {
        .predictor        = &emptyPredictor,
        .featureGenerator = FeatureGen_integer,
        .nbLabels         = nbSuccessors,
        .classLabels      = classLabels,
        .nbFeatures       = 1,
        .featureLabels    = featureLabels,
    };

    ZL_MLSelectorConfig config = {
        .model         = ZL_GBT,
        .runtimeConfig = (void*)&emptyModel,
    };

    ZL_RESULT_OF(ZL_GraphID)
    result = ZL_MLSelector_registerGraph(
            compressor, &config, successors, nbSuccessors);

    ALLOC_Arena_freeArena(arena);
    return result;
}
