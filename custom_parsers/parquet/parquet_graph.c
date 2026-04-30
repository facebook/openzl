// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "parquet_graph.h"

#include "custom_parsers/parquet/parquet_lexer.h"
#include "openzl/compress/graphs/generic_clustering_graph.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_segmenter.h"

#define ZL_TRY_SET_EL(_var, _expr) ZL_TRY_SET(ZL_EdgeList, _var, _expr)

#define ZL_PARQUET_TOKENS_PID 1
#define ZL_PARQUET_CHUNK_SIZE_PID 2

// Run the conversion node for the given type and width.
static ZL_Report
runConversion(ZL_Edge* in, ZL_Edge** out, ZL_Type type, size_t width)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(in);
    if (type == ZL_Type_serial) {
        *out = in;
        return ZL_returnSuccess();
    }

    ZL_EdgeList el;
    if (type == ZL_Type_numeric) {
        if (width == 1) {
            ZL_TRY_SET_EL(el, ZL_Edge_runNode(in, ZL_NODE_INTERPRET_AS_LE8));
        } else if (width == 2) {
            ZL_TRY_SET_EL(el, ZL_Edge_runNode(in, ZL_NODE_INTERPRET_AS_LE16));
        } else if (width == 4) {
            ZL_TRY_SET_EL(el, ZL_Edge_runNode(in, ZL_NODE_INTERPRET_AS_LE32));
        } else if (width == 8) {
            ZL_TRY_SET_EL(el, ZL_Edge_runNode(in, ZL_NODE_INTERPRET_AS_LE64));
        } else {
            return ZL_REPORT_ERROR(GENERIC, "Unsupported width %zu", width);
        }
    } else if (type == ZL_Type_struct) {
        ZL_IntParam const intParam = { ZL_trlip_tokenSize, (int32_t)width };
        ZL_LocalParams lParams     = { .intParams = { &intParam, 1 } };
        ZL_TRY_SET_EL(
                el,
                ZL_Edge_runNode_withParams(
                        in, ZL_NODE_CONVERT_SERIAL_TO_TOKENX, &lParams));
    } else {
        return ZL_REPORT_ERROR(GENERIC, "Unsupported type %d", type);
    }

    ZL_ERR_IF_NE(el.nbEdges, 1, GENERIC, "Unexpected number of edges");
    *out = el.edges[0];

    return ZL_returnSuccess();
}

static ZL_Report parquetGraphFn(ZL_Graph* graph, ZL_Edge* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    // Get the tokens
    ZL_RefParam const tokensParam =
            ZL_Graph_getLocalRefParam(graph, ZL_PARQUET_TOKENS_PID);
    const ZL_ParquetToken* tokens = tokensParam.paramRef;
    ZL_ERR_IF_NE(tokensParam.paramSize % sizeof(ZL_ParquetToken), 0, GENERIC);
    size_t const nbTokens = tokensParam.paramSize / sizeof(ZL_ParquetToken);

    // Allocate space for the dispatch instructions
    size_t* const segmentSizes =
            ZL_Graph_getScratchSpace(graph, nbTokens * sizeof(size_t));
    uint32_t* const dispatchTags =
            ZL_Graph_getScratchSpace(graph, nbTokens * sizeof(uint32_t));

    // Allocate space for token metadata. Note: the tag here is different from
    // the "dispatch tag" above. This tag identifies the schema element that a
    // given data page belongs to.
    uint32_t* const tags =
            ZL_Graph_getScratchSpace(graph, nbTokens * sizeof(uint32_t));
    ZL_Type* const types =
            ZL_Graph_getScratchSpace(graph, nbTokens * sizeof(ZL_Type));
    size_t* const widths =
            ZL_Graph_getScratchSpace(graph, nbTokens * sizeof(size_t));

    // Header stream metadata
    tags[0]   = 0;
    types[0]  = ZL_Type_serial;
    widths[0] = 1;

    uint32_t nbTags = 0;
    for (size_t i = 0; i < nbTokens; ++i) {
        const ZL_ParquetToken token = tokens[i];
        bool isDataPage = token.type == ZL_ParquetTokenType_DataPage;
        // Fill in dispatch instructions.
        segmentSizes[i] = token.size;
        dispatchTags[i] = isDataPage ? ++nbTags : 0;

        // Fill in token metadata.
        if (isDataPage) {
            tags[nbTags]   = token.tag;
            types[nbTags]  = token.dataType;
            widths[nbTags] = token.dataWidth;
        }
    }

    ZL_DispatchInstructions di = {
        .segmentSizes = segmentSizes,
        .nbSegments   = nbTokens,
        .tags         = dispatchTags,
        .nbTags       = nbTags + 1,
    };

    // Split the input according to segmentSizes
    ZL_ERR_IF_NE(nbIns, 1, graph_invalidNumInputs);
    ZL_Edge* const edge = ins[0];
    ZL_TRY_LET(ZL_EdgeList, el, ZL_Edge_runDispatchNode(edge, &di));
    ZL_ERR_IF_NE(el.nbEdges, nbTags + 3, GENERIC);

    // Set the destination for the tags and segment sizes
    ZL_ERR_IF_ERR(
            ZL_Edge_setDestination(el.edges[0], ZL_GRAPH_COMPRESS_GENERIC));
    ZL_ERR_IF_ERR(
            ZL_Edge_setDestination(el.edges[1], ZL_GRAPH_COMPRESS_GENERIC));

    ZL_Edge** const edges = el.edges + 2;
    size_t const nbEdges  = el.nbEdges - 2;

    // Set the metadata for each edge and run the conversion
    for (size_t i = 0; i < nbEdges; ++i) {
        ZL_Edge* in  = edges[i];
        ZL_Edge* out = NULL;

        // Run the conversion
        ZL_ERR_IF_ERR(runConversion(in, &out, types[i], widths[i]));

        // Set the tag metadata for the clustering node
        ZL_ERR_IF_ERR(ZL_Edge_setIntMetadata(
                out, ZL_CLUSTERING_TAG_METADATA_ID, (int)tags[i]));
        edges[i] = out;
    }

    // Set the input clustering graph as the destination of the edges
    ZL_GraphIDList graphs = ZL_Graph_getCustomGraphs(graph);
    ZL_ERR_IF_NE(graphs.nbGraphIDs, 1, GENERIC);
    ZL_ERR_IF_ERR(ZL_Edge_setParameterizedDestination(
            edges, nbEdges, graphs.graphids[0], NULL));

    return ZL_returnSuccess();
}

static ZL_Report commitChunk(
        ZL_Segmenter* seg,
        size_t* size,
        const ZL_ParquetToken* tokens,
        size_t tokensSize,
        ZL_GraphID parquetGraph)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(seg);
    ZL_RefParam const tokensRef = {
        .paramId   = ZL_PARQUET_TOKENS_PID,
        .paramRef  = tokens,
        .paramSize = tokensSize * sizeof(ZL_ParquetToken),
    };
    ZL_LocalParams lParams              = { .refParams = { .nbRefParams = 1,
                                                           .refParams   = &tokensRef } };
    ZL_RuntimeGraphParameters const par = {
        .localParams = &lParams,
    };

    ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(seg, size, 1, parquetGraph, &par));

    return ZL_returnSuccess();
}

static ZL_Report parquetSegmenterInner(
        ZL_Segmenter* seg,
        ZL_ParquetLexer* lexer)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(seg);
    size_t const nbIns = ZL_Segmenter_numInputs(seg);
    ZL_ERR_IF_NE(nbIns, 1, graph_invalidNumInputs);
    ZL_Input const* const input = ZL_Segmenter_getInput(seg, 0);
    const size_t size           = ZL_Input_numElts(input);
    ZL_ErrorContext* errCtx     = ZL_GET_DEFAULT_ERROR_CONTEXT(seg);

    // Will return an error if the input is not a valid Parquet file.
    ZL_ERR_IF_ERR(
            ZL_ParquetLexer_init(lexer, ZL_Input_ptr(input), size, errCtx));

    // Get the custom graph
    ZL_GraphIDList graphs = ZL_Segmenter_getCustomGraphs(seg);
    ZL_ERR_IF_NE(graphs.nbGraphIDs, 1, GENERIC);
    ZL_GraphID const parquetGraph = graphs.graphids[0];

    // Get the max chunk size
    size_t chunkSize = (size_t)ZL_Segmenter_getLocalIntParam(
                               seg, ZL_PARQUET_CHUNK_SIZE_PID)
                               .paramValue;

    // Allocate space for token metadata.
    ZL_TRY_LET(
            size_t, maxNbTokens, ZL_ParquetLexer_maxNumTokens(lexer, errCtx));
    size_t nbTokens               = 0;
    ZL_ParquetToken* const tokens = ZL_Segmenter_getScratchSpace(
            seg, maxNbTokens * sizeof(ZL_ParquetToken));

    ZL_ParquetToken* currTokensPtr = tokens;
    size_t currTokensSize          = 0;
    size_t currChunkSize           = 0;
    while (!ZL_ParquetLexer_finished(lexer)) {
        ZL_ERR_IF_GE(nbTokens, maxNbTokens, GENERIC);
        ZL_ParquetToken* const token = currTokensPtr + currTokensSize;
        ZL_ERR_IF_ERR(ZL_ParquetLexer_lex(lexer, token, 1, errCtx));

        if (chunkSize != 0 && currChunkSize != 0
            && currChunkSize + token->size > chunkSize) {
            ZL_ERR_IF_ERR(commitChunk(
                    seg,
                    &currChunkSize,
                    currTokensPtr,
                    currTokensSize,
                    parquetGraph));
            currTokensPtr += currTokensSize;
            currTokensSize = 0;
            currChunkSize  = 0;
        }

        currChunkSize += token->size;
        currTokensSize += 1;
        ++nbTokens;
    }

    // Commit the last segment
    ZL_ERR_IF_ERR(commitChunk(
            seg, &currChunkSize, currTokensPtr, currTokensSize, parquetGraph));

    return ZL_returnSuccess();
}

// Wrapper around the inner graph function that allocates a lexer
static ZL_Report parquetSegmenterFn(ZL_Segmenter* seg)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(seg);
    ZL_ParquetLexer* lexer = ZL_ParquetLexer_create();
    ZL_ERR_IF_NULL(lexer, allocation);
    ZL_Report ret = parquetSegmenterInner(seg, lexer);
    ZL_ParquetLexer_free(lexer);
    return ret;
}

ZL_GraphID ZL_Parquet_registerGraph_withChunkSize(
        ZL_Compressor* compressor,
        ZL_GraphID clusteringGraph,
        int chunkSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(compressor);

    // Register the dispatch graph
    ZL_GraphID graph = ZL_Compressor_getGraph(compressor, "Parquet Graph");
    if (graph.gid == ZL_GRAPH_ILLEGAL.gid) {
        ZL_FunctionGraphDesc desc = {
            .name           = "!Parquet Graph",
            .graph_f        = parquetGraphFn,
            .inputTypeMasks = (ZL_Type[]){ ZL_Type_serial },
            .nbInputs       = 1,
            .customGraphs   = &clusteringGraph,
            .nbCustomGraphs = 1,
        };

        graph = ZL_Compressor_registerFunctionGraph(compressor, &desc);
    }

    // Register the parameterized graph
    ZL_ParameterizedGraphDesc const graphDesc = {
        .graph          = graph,
        .customGraphs   = &clusteringGraph,
        .nbCustomGraphs = 1,
    };
    ZL_GraphID parquetGraph =
            ZL_Compressor_registerParameterizedGraph(compressor, &graphDesc);

    // Register the parser/segmenter
    ZL_GraphID parser = ZL_Compressor_getGraph(compressor, "Parquet Parser");
    if (parser.gid == ZL_GRAPH_ILLEGAL.gid) {
        // Register the anchor graph
        ZL_SegmenterDesc desc = {
            .name           = "!Parquet Parser",
            .segmenterFn    = parquetSegmenterFn,
            .inputTypeMasks = (ZL_Type[]){ ZL_Type_serial },
            .numInputs      = 1,
        };

        parser = ZL_Compressor_registerSegmenter(compressor, &desc);
    }

    // Register the parameterized graph
    ZL_IntParam intParams[] = { {
            .paramId    = ZL_PARQUET_CHUNK_SIZE_PID,
            .paramValue = chunkSize,
    } };
    ZL_LocalParams params   = (ZL_LocalParams){
          .intParams = { .intParams = intParams, .nbIntParams = 1 },
    };
    ZL_ParameterizedGraphDesc const parserDesc = {
        .graph          = parser,
        .customGraphs   = &parquetGraph,
        .nbCustomGraphs = 1,
        .localParams    = &params,
    };
    return ZL_Compressor_registerParameterizedGraph(compressor, &parserDesc);
}

ZL_GraphID ZL_Parquet_registerGraph(
        ZL_Compressor* compressor,
        ZL_GraphID clusteringGraph)
{
    return ZL_Parquet_registerGraph_withChunkSize(
            compressor, clusteringGraph, 0);
}
