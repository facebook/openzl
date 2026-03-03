// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/csv/csv_parser.h"

#include <errno.h>
#include <stdlib.h>

#include "custom_parsers/csv/csv_lexer.h"
#include "openzl/codecs/zl_clustering.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/overflow.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_selector.h"

ZL_RESULT_DECLARE_TYPE(ZL_RefParam);

static void print(const void* ptr, size_t size, char* name)
{
    FILE* f = fopen(name, "w");
    if (f == NULL) {
        exit(errno);
    }
    size_t s = fwrite(ptr, 1, size, f);
    if (s != size) {
        exit(errno);
    }
    fclose(f);
}

static ZL_RESULT_OF(ZL_RefParam)
        tryGetRefParam(ZL_Graph* gctx, int key, size_t structSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_RefParam, gctx);
    ZL_RefParam refParam = ZL_Graph_getLocalRefParam(gctx, key);
    ZL_ERR_IF_EQ(refParam.paramId, ZL_LP_INVALID_PARAMID, node_invalid_input);
    ZL_ERR_IF_NE(refParam.paramSize % structSize, 0, node_invalid_input);
    return ZL_RESULT_WRAP_VALUE(ZL_RefParam, refParam);
}

static ZL_Report tryGetIntParam(ZL_Graph* gctx, int key)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    ZL_IntParam intParam = ZL_Graph_getLocalIntParam(gctx, key);
    ZL_ERR_IF_EQ(intParam.paramId, ZL_LP_INVALID_PARAMID, node_invalid_input);
    return ZL_returnValue((size_t)intParam.paramValue);
}

/**
 * Create dispatch indices for each token. Each field is sent to the output
 * corresponding to its column number. Each delimiter, whitespace, and newline
 * is sent to the `numCols` output. If @p hasHeader is true, the first line is
 * sent to the `numCols + 1` output.
 */
static const uint16_t* createDispatchIndices(
        ZL_Graph* gctx,
        const ZL_CSV_TokenType* types,
        const uint32_t* cols,
        size_t numTokens,
        size_t numCols,
        bool hasHeader)
{
    size_t tokensSize;
    if (ZL_overflowMulST(numTokens, sizeof(uint16_t), &tokensSize)) {
        return NULL;
    }
    uint16_t* dispatchIndices = ZL_Graph_getScratchSpace(gctx, tokensSize);
    if (dispatchIndices == NULL) {
        return NULL;
    }

    size_t i = 0;
    if (hasHeader && i < numTokens) {
        do {
            dispatchIndices[i] = (uint16_t)(numCols + 1);
            ++i;
        } while (i < numTokens && types[i] != ZL_CSV_TokenType_Newline);
    }

    for (; i < numTokens; ++i) {
        dispatchIndices[i] =
                (uint16_t)(types[i] == ZL_CSV_TokenType_Field ? cols[i]
                                                              : numCols);
    }
    return dispatchIndices;
}

static ZL_Report
csvParserGraphFn(ZL_Graph* gctx, ZL_Edge* inputs[], size_t numInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);

    ZL_ASSERT_EQ(numInputs, 1);
    ZL_ASSERT_NN(inputs[0]);

    /* TODO: The line end token is assumed to be '\n'. We should allow
     * multiple types of line end characters. */
    const ZL_Input* input = ZL_Edge_getData(inputs[0]);
    ZL_ASSERT_EQ(ZL_Input_type(input), ZL_Type_serial);

    // Clustering graph is registered inside as a custom graph
    // Expecting 3 custom graphs right now: clustering, delimiters, header
    // Clustering - (self explanatory)
    // Delimiters - ZL_GRAPH_COMPRESS_GENERIC
    // Header - ZL_GRAPH_COMPRESS_GENERIC
    ZL_GraphIDList customGraphs = ZL_Graph_getCustomGraphs(gctx);
    ZL_ERR_IF_NE(customGraphs.nbGraphIDs, 3, node_invalid_input);

    ZL_TRY_LET(
            ZL_RefParam,
            refParam,
            tryGetRefParam(
                    gctx, ZL_CSV_CHUNKED_TYPES_ID, sizeof(ZL_CSV_TokenType)));
    const ZL_CSV_TokenType* tokens = (const ZL_CSV_TokenType*)refParam.paramRef;
    const size_t numTokens = refParam.paramSize / sizeof(ZL_CSV_TokenType);

    ZL_TRY_SET(
            ZL_RefParam,
            refParam,
            tryGetRefParam(gctx, ZL_CSV_CHUNKED_SIZES_ID, sizeof(uint32_t)));
    const uint32_t* sizes = (const uint32_t*)refParam.paramRef;
    ZL_ERR_IF_NE(
            refParam.paramSize / sizeof(uint32_t),
            numTokens,
            node_invalid_input);

    ZL_TRY_SET(
            ZL_RefParam,
            refParam,
            tryGetRefParam(gctx, ZL_CSV_CHUNKED_COLS_ID, sizeof(uint32_t)));
    const uint32_t* cols = (const uint32_t*)refParam.paramRef;
    ZL_ERR_IF_NE(
            refParam.paramSize / sizeof(uint32_t),
            numTokens,
            node_invalid_input);

    ZL_TRY_LET(
            size_t,
            hasHeader,
            tryGetIntParam(gctx, ZL_CSV_CHUNKED_HAS_HEADER_ID));
    ZL_TRY_LET(
            size_t, numCols, tryGetIntParam(gctx, ZL_CSV_CHUNKED_NUM_COLS_ID));

    // +1 for delimiters and newlines; +1 for header
    const size_t numOutputs = numCols + 1 + (hasHeader ? 1 : 0);

    // Run newly created Node, collect outputs at intermediate output
    ZL_TRY_LET_T(
            ZL_EdgeList,
            io,
            ZL_Edge_runConvertSerialToStringNode(inputs[0], sizes, numTokens));

    const uint16_t* dispatchIndices = createDispatchIndices(
            gctx, tokens, cols, numTokens, numCols, hasHeader);
    ZL_ERR_IF_NULL(dispatchIndices, allocation);

    ZL_TRY_LET(
            ZL_EdgeList,
            so,
            ZL_Edge_runDispatchStringNode(
                    io.edges[0], (int)numOutputs, dispatchIndices));
    ZL_ASSERT_EQ(so.nbEdges, numOutputs + 1);

    // Set edge tag metadata for identification for clustering to the column
    for (size_t n = 0; n < numCols; n++) {
        ZL_ERR_IF_ERR(ZL_Edge_setIntMetadata(
                so.edges[n + 1], ZL_CLUSTERING_TAG_METADATA_ID, (int)n));
    }
    // Successor for dispatch indices
    ZL_ERR_IF_ERR(
            ZL_Edge_setDestination(so.edges[0], ZL_GRAPH_COMPRESS_GENERIC));
    // columns go to clustering
    ZL_ERR_IF_ERR(ZL_Edge_setParameterizedDestination(
            so.edges + 1, numCols, customGraphs.graphids[0], NULL));
    // Successor for delimiters, whitespace, and newlines
    ZL_ERR_IF_ERR(ZL_Edge_setDestination(
            so.edges[numCols + 1], customGraphs.graphids[1]));
    // Successor for header
    if (hasHeader) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                so.edges[numCols + 2], customGraphs.graphids[2]));
    }
    return ZL_returnSuccess();
}

ZL_GraphID ZL_CsvParser_registerGraph(
        ZL_Compressor* compressor,
        const ZL_GraphID clusteringGraph)
{
    ZL_GraphID* successors = (ZL_GraphID[]){ clusteringGraph,
                                             ZL_GRAPH_COMPRESS_GENERIC,
                                             ZL_GRAPH_COMPRESS_GENERIC };

    ZL_GraphID csvParserGraph =
            ZL_Compressor_getGraph(compressor, "CSV Parser");
    if (csvParserGraph.gid == ZL_GRAPH_ILLEGAL.gid) {
        ZL_FunctionGraphDesc csvParser = {
            .name           = "!CSV Parser",
            .graph_f        = csvParserGraphFn,
            .inputTypeMasks = (ZL_Type[]){ ZL_Type_serial },
            .nbInputs       = 1,
            .customGraphs   = NULL,
            .nbCustomGraphs = 0,
            .localParams    = {},
        };
        csvParserGraph =
                ZL_Compressor_registerFunctionGraph(compressor, &csvParser);
    }

    ZL_ParameterizedGraphDesc const csvParserGraphDesc = {
        .graph          = csvParserGraph,
        .customGraphs   = successors,
        .nbCustomGraphs = 3,
    };
    return ZL_Compressor_registerParameterizedGraph(
            compressor, &csvParserGraphDesc);
}
