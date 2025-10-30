// Copyright (c) Meta Platforms, Inc. and affiliates.
// Note: This file is work in progress and is not ready for use yet.

#include "tools/ml_selector/ml_selector_graph.h"
#include "openzl/common/a1cbor_helpers.h"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_errors.h"

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
 * selects successor based on what is specified inside the ZL_MLSelectorConfig.
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

    ZL_ERR_IF_GE(
            config.selectedSuccessor, succList.nbGraphIDs, successor_invalid);

    ZL_GraphID succ = succList.graphids[config.selectedSuccessor];

    ZL_ERR_IF_ERR(
            ZL_Edge_setParameterizedDestination(inputs, nbInputs, succ, NULL));

    return ZL_returnSuccess();
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
    A1C_MapBuilder rootMapBuilder = A1C_Item_map_builder(root, 1, arena);
    {
        A1C_MAP_TRY_ADD_T(ZL_SerializedMLConfig, pair, rootMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "selectedSuccessor");
        A1C_Item_int64(&pair->val, (A1C_Int64)config->selectedSuccessor);
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
    if (root == NULL) {
        ZL_MLSelectorConfig hd = { .selectedSuccessor = 0 };
        return ZL_RESULT_WRAP_VALUE(ZL_MLSelectorConfig, hd);
    }
    A1C_TRY_EXTRACT_T_MAP(ZL_MLSelectorConfig, rootMap, root);

    A1C_TRY_EXTRACT_T_INT64(
            ZL_MLSelectorConfig,
            selectedSuccessor,
            A1C_Map_get_cstr(&rootMap, "selectedSuccessor"));
    dst.selectedSuccessor = (size_t)selectedSuccessor;

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

    ZL_LocalParams params = (ZL_LocalParams){
        .copyParams = { .copyParams = &configParam, .nbCopyParams = 1 },
    };

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
     * By freeing the arena, we are freeing all the memory used by a1c_arena. We
     * can free arena here because we make a copy param of the serialized
     * config, so the lifetime of the serialized config is tied to the graph.
     */
    ALLOC_Arena_freeArena(arena);
    return ZL_RESULT_WRAP_VALUE(ZL_GraphID, graph);
}
