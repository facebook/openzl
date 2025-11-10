// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/csv/csv_parser.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "custom_parsers/csv/csv_lexer.h"
#include "openzl/common/logging.h"
#include "openzl/compress/graphs/generic_clustering_graph.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"

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

static ZL_Report
csvParserGraphFn(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs)
{
    ZL_RET_R_IF_NE(node_invalid_input, nbInputs, 1);
    ZL_RET_R_IF_NULL(node_invalid_input, inputs[0]);
    /* TODO: The line end token is assumed to be '\n'. We should allow
     * multiple types of line end characters. */
    const ZL_Input* input = ZL_Edge_getData(inputs[0]);
    ZL_RET_R_IF_NE(node_invalid_input, ZL_Input_type(input), ZL_Type_serial);
    // Clustering graph is registered inside as a custom graph
    // Expecting 3 custom graphs right now: clustering, delimiters, header
    // Clustering - (self explanatory)
    // Delimiters - ZL_GRAPH_COMPRESS_GENERIC
    // Header - ZL_GRAPH_COMPRESS_GENERIC
    ZL_GraphIDList customGraphs = ZL_Graph_getCustomGraphs(gctx);
    ZL_RET_R_IF_NE(node_invalid_input, customGraphs.nbGraphIDs, 3);

    ZL_RefParam refParam =
            ZL_Graph_getLocalRefParam(gctx, ZL_CSV_CHUNKED_LEXED_RESULT_ID);
    const ZL_CSV_lexResult* lexed = (const ZL_CSV_lexResult*)refParam.paramRef;
    // +1 for delimiters and newlines; +1 for header
    size_t nbOutputs = lexed->nbColumns + 2;

    // Run newly created Node, collect outputs at intermediate output
    ZL_TRY_LET_T(
            ZL_EdgeList,
            io,
            ZL_Edge_runConvertSerialToStringNode(
                    inputs[0], lexed->stringLens, lexed->nbStrs));

    if (0) { // dump these streams for debugging
        const ZL_Input* data = ZL_Edge_getData(io.edges[0]);
        print(ZL_Input_ptr(data),
              ZL_Input_contentSize(data),
              "/tmp/sdd/psam.streans.txt");
        print(ZL_Input_stringLens(data),
              ZL_Input_numElts(data),
              "/tmp/sdd/psam.streams.strLens");
        print(lexed->dispatchIndices,
              lexed->nbStrs,
              "/tmp/sdd/psam.streams.dispatchIndices");
    }
    ZL_TRY_LET_T(
            ZL_EdgeList,
            so,
            ZL_Edge_runDispatchStringNode(
                    io.edges[0], (int)nbOutputs, lexed->dispatchIndices));

    // Set edge tag metadata for identification for clustering to the column
    for (size_t n = 0; n < lexed->nbColumns; n++) {
        ZL_RET_R_IF_ERR(ZL_Edge_setIntMetadata(
                so.edges[n + 1], ZL_CLUSTERING_TAG_METADATA_ID, (int)n));
    }
    // Successor for dispatch indices
    ZL_RET_R_IF_ERR(
            ZL_Edge_setDestination(so.edges[0], ZL_GRAPH_COMPRESS_GENERIC));
    // columns go to clustering
    ZL_RET_R_IF_ERR(ZL_Edge_setParameterizedDestination(
            so.edges + 1, lexed->nbColumns, customGraphs.graphids[0], NULL));
    // Successor for delimiters, whitespace, and newlines
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(
            so.edges[lexed->nbColumns + 1], customGraphs.graphids[1]));
    // Successor for header
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(
            so.edges[lexed->nbColumns + 2], customGraphs.graphids[2]));
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
