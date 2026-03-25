// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/unitBench/scenarios/split_byrange_graph.h"

#include <stdlib.h> /* abort */

#include "openzl/codecs/zl_concat.h"     /* ZL_NODE_CONCAT_NUMERIC */
#include "openzl/codecs/zl_conversion.h" /* ZL_NODE_INTERPRET_AS_LE8/16 */
#include "openzl/codecs/zl_delta.h"      /* ZL_NODE_DELTA_INT */
#include "openzl/codecs/zl_generic.h"    /* ZL_GRAPH_NUMERIC */
#include "openzl/codecs/zl_split.h"      /* ZL_NODE_SPLIT_BYRANGE */
// NOLINTNEXTLINE(facebook-unused-include-check)
#include "openzl/codecs/zl_tokenize.h" /* ZL_Compressor_registerTokenizeGraph */
#include "openzl/zl_compressor.h"
#include "openzl/zl_errors.h"       /* ZL_TRY_LET_T, ZL_RET_R_IF_ERR */
#include "openzl/zl_graph_api.h"    /* ZL_FunctionGraphDesc, ZL_Edge_runNode */
#include "openzl/zl_public_nodes.h" /* ZL_NODE_INTERPRET_AS_LE* */

/* split_byrange -> STORE: measures pure range-detection overhead (8-bit) */
ZL_GraphID splitByRange8_graph(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE8,
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, ZL_NODE_SPLIT_BYRANGE, ZL_GRAPH_STORE));
}

/* split_byrange -> STORE: measures pure range-detection overhead (16-bit) */
ZL_GraphID splitByRange16_graph(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE16,
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, ZL_NODE_SPLIT_BYRANGE, ZL_GRAPH_STORE));
}

/* split_byrange -> STORE: measures pure range-detection overhead (32-bit) */
ZL_GraphID splitByRange32_graph(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE32,
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, ZL_NODE_SPLIT_BYRANGE, ZL_GRAPH_STORE));
}

/* split_byrange -> STORE: measures pure range-detection overhead (64-bit) */
ZL_GraphID splitByRange64_graph(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE64,
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, ZL_NODE_SPLIT_BYRANGE, ZL_GRAPH_STORE));
}

/* split_byrange -> ZSTD: realistic pipeline (32-bit) */
ZL_GraphID splitByRange32_zstd_graph(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE32,
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, ZL_NODE_SPLIT_BYRANGE, ZL_GRAPH_ZSTD));
}

/* split_byrange -> ZSTD: realistic pipeline (64-bit) */
ZL_GraphID splitByRange64_zstd_graph(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE64,
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, ZL_NODE_SPLIT_BYRANGE, ZL_GRAPH_ZSTD));
}

/* split_byrange -> tokenize_sorted(delta_int+numeric, numeric) (64-bit) */
ZL_GraphID splitByRange64_tokenSort_graph(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }

    ZL_GraphID alphabetGraph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_DELTA_INT, ZL_GRAPH_NUMERIC);

    ZL_GraphID tokenSort = ZL_Compressor_registerTokenizeGraph(
            cgraph, ZL_Type_numeric, true, alphabetGraph, ZL_GRAPH_NUMERIC);

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE64,
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, ZL_NODE_SPLIT_BYRANGE, tokenSort));
}

/* tokenize_sorted → split_byrange on indices (64-bit)
 * Pure static graph: split happens after tokenization, on the index stream.
 * The index stream naturally clusters by range (sorted alphabet), so
 * split_byrange detects the same bands without needing a Function Graph. */
ZL_GraphID tokenSort64_splitIndices_graph(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }

    ZL_GraphID alphabetGraph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_DELTA_INT, ZL_GRAPH_NUMERIC);

    ZL_GraphID indicesGraph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_SPLIT_BYRANGE, ZL_GRAPH_NUMERIC);

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE64,
            ZL_Compressor_registerTokenizeGraph(
                    cgraph,
                    ZL_Type_numeric,
                    true,
                    alphabetGraph,
                    indicesGraph));
}

/* ------------------------------------------------------------------ */
/* Function Graph: split_byrange → tokenize(sorted) per segment →    */
/*   concat all alphabets → delta_int+NUMERIC on merged alphabet,    */
/*   NUMERIC on each index stream.                                   */
/*                                                                    */
/* Purpose: avoid duplicating alphabet compression overhead across   */
/* segments by merging them back into a single stream before          */
/* compression, while keeping indices separate (narrower index width  */
/* per segment is the win from splitting).                            */
/* ------------------------------------------------------------------ */

static ZL_Report
splitByRange_concatAlpha_fn(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
{
    (void)nbInputs;

    /* Step 1: Split by range */
    ZL_TRY_LET_T(
            ZL_EdgeList,
            segments,
            ZL_Edge_runNode(inputs[0], ZL_NODE_SPLIT_BYRANGE));

    size_t const N = segments.nbEdges;

    /* Step 2: Tokenize each segment (sorted numeric) */
    ZL_NodeIDList nids  = ZL_Graph_getCustomNodes(graph);
    ZL_NodeID tokenNode = nids.nodeids[0];

    ZL_Edge** alphabets =
            (ZL_Edge**)ZL_Graph_getScratchSpace(graph, N * sizeof(ZL_Edge*));

    for (size_t i = 0; i < N; i++) {
        ZL_TRY_LET_T(
                ZL_EdgeList,
                tokenized,
                ZL_Edge_runNode(segments.edges[i], tokenNode));
        /* tokenized.edges[0] = alphabet, tokenized.edges[1] = indices */
        alphabets[i] = tokenized.edges[0];
        /* Send indices to NUMERIC */
        ZL_RET_R_IF_ERR(
                ZL_Edge_setDestination(tokenized.edges[1], ZL_GRAPH_NUMERIC));
    }

    /* Step 3: Concat all alphabets into one stream */
    ZL_TRY_LET_T(
            ZL_EdgeList,
            concatResult,
            ZL_Edge_runMultiInputNode(alphabets, N, ZL_NODE_CONCAT_NUMERIC));
    /* concatResult.edges[0] = sizes, concatResult.edges[1] = merged data */

    /* Step 4: Send concat sizes to NUMERIC */
    ZL_RET_R_IF_ERR(
            ZL_Edge_setDestination(concatResult.edges[0], ZL_GRAPH_NUMERIC));

    /* Step 5: Send merged alphabet to delta_int → NUMERIC */
    ZL_GraphIDList gids = ZL_Graph_getCustomGraphs(graph);
    ZL_RET_R_IF_ERR(
            ZL_Edge_setDestination(concatResult.edges[1], gids.graphids[0]));

    return ZL_returnSuccess();
}

ZL_GraphID splitByRange64_concatAlpha_tokenSort_graph(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }

    /* Register tokenize node (sorted numeric) */
    ZL_RESULT_OF(ZL_NodeID)
    tokenNodeR = ZL_Compressor_parameterizeTokenizeNode(
            cgraph, ZL_Type_numeric, true);
    if (ZL_RES_isError(tokenNodeR)) {
        abort();
    }
    ZL_NodeID tokenNode = ZL_RES_value(tokenNodeR);

    /* Register alphabet successor graph: delta_int → NUMERIC */
    ZL_GraphID alphabetGraph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_DELTA_INT, ZL_GRAPH_NUMERIC);

    ZL_Type inType                 = ZL_Type_numeric;
    ZL_GraphID customGraphs[]      = { alphabetGraph };
    ZL_NodeID customNodes[]        = { tokenNode };
    ZL_FunctionGraphDesc graphDesc = {
        .name           = "splitByRange_concatAlpha_tokenSort",
        .graph_f        = splitByRange_concatAlpha_fn,
        .inputTypeMasks = &inType,
        .nbInputs       = 1,
        .customGraphs   = customGraphs,
        .nbCustomGraphs = 1,
        .customNodes    = customNodes,
        .nbCustomNodes  = 1,
    };

    ZL_GraphID innerGraph =
            ZL_Compressor_registerFunctionGraph(cgraph, &graphDesc);

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE64, innerGraph);
}
