// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/lz/encode_lz_binding.h"

#include "openzl/codecs/lz/common_field_lz.h"
#include "openzl/codecs/lz/encode_field_lz_literals_selector.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_reflection.h"

/**
 * Set the maximum bytes to process to 4B to avoid overflow in the match finder.
 * It could likely be higher, but this is close enough to 2^32-1.
 */
static const size_t kFieldLzContentSizeBytes = 4000000000u;

static void* allocEICtx(void* opaque, size_t size)
{
    ZL_Encoder* eictx = opaque;
    return ZL_Encoder_getScratchSpace(eictx, size);
}

static ZL_FieldLz_Allocator getAlloc(ZL_Encoder* eictx)
{
    return (ZL_FieldLz_Allocator){ .alloc = allocEICtx, .opaque = eictx };
}

ZL_Report EI_fieldLz(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in    = ins[0];
    size_t const nbElts   = ZL_Input_numElts(in);
    size_t const eltWidth = ZL_Input_eltWidth(in);
    size_t const maxNbSeq = ZL_FieldLz_maxNbSequences(nbElts, eltWidth);

    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_struct);
    // TODO(terrelln): Enable field-lz for more field sizes
    if (!ZL_isPow2(eltWidth) || eltWidth == 1 || eltWidth > 8) {
        ZL_ERR(GENERIC);
    }
    ZL_ERR_IF_GT(
            ZL_Input_contentSize(in),
            kFieldLzContentSizeBytes,
            temporaryLibraryLimitation,
            "FieldLZ only supports up to 4B of input due to 32-bit overflow in the match finder");

    ZL_Output* literals =
            ZL_Encoder_createTypedStream(eictx, 0, nbElts, eltWidth);
    ZL_Output* tokens  = ZL_Encoder_createTypedStream(eictx, 1, maxNbSeq, 2);
    ZL_Output* offsets = ZL_Encoder_createTypedStream(eictx, 2, maxNbSeq, 4);
    ZL_Output* extraLiteralLengths =
            ZL_Encoder_createTypedStream(eictx, 3, maxNbSeq, 4);
    ZL_Output* extraMatchLengths =
            ZL_Encoder_createTypedStream(eictx, 4, maxNbSeq, 4);

    if (literals == NULL || tokens == NULL || offsets == NULL
        || extraLiteralLengths == NULL || extraMatchLengths == NULL) {
        ZL_ERR(allocation);
    }

    ZL_FieldLz_OutSequences dst = {
        .literalElts         = ZL_Output_ptr(literals),
        .nbLiteralElts       = 0,
        .literalEltsCapacity = nbElts,

        .tokens   = (uint16_t*)ZL_Output_ptr(tokens),
        .nbTokens = 0,

        .offsets   = (uint32_t*)ZL_Output_ptr(offsets),
        .nbOffsets = 0,

        .extraLiteralLengths   = (uint32_t*)ZL_Output_ptr(extraLiteralLengths),
        .nbExtraLiteralLengths = 0,

        .extraMatchLengths   = (uint32_t*)ZL_Output_ptr(extraMatchLengths),
        .nbExtraMatchLengths = 0,

        .sequencesCapacity = maxNbSeq
    };

    int compressionLevel;
    ZL_IntParam compressionLevelOverride = ZL_Encoder_getLocalIntParam(
            eictx, ZL_FIELD_LZ_COMPRESSION_LEVEL_OVERRIDE_PID);
    if (compressionLevelOverride.paramId == ZL_LP_INVALID_PARAMID) {
        compressionLevel =
                ZL_Encoder_getCParam(eictx, ZL_CParam_compressionLevel);
    } else {
        compressionLevel = compressionLevelOverride.paramValue;
    }

    ZL_Report const ret = ZS2_FieldLz_compress(
            &dst,
            ZL_Input_ptr(in),
            nbElts,
            eltWidth,
            compressionLevel,
            getAlloc(eictx));
    if (ZL_isError(ret)) {
        return ret;
    }

    uint8_t header[ZL_VARINT_LENGTH_64];
    size_t const headerSize = ZL_varintEncode(nbElts, header);

    ZL_Encoder_sendCodecHeader(eictx, header, headerSize);

    ZL_ERR_IF_ERR(ZL_Output_commit(literals, dst.nbLiteralElts));
    ZL_ERR_IF_ERR(ZL_Output_commit(tokens, dst.nbTokens));
    ZL_ERR_IF_ERR(ZL_Output_commit(offsets, dst.nbOffsets));
    ZL_ERR_IF_ERR(
            ZL_Output_commit(extraLiteralLengths, dst.nbExtraLiteralLengths));
    ZL_ERR_IF_ERR(ZL_Output_commit(extraMatchLengths, dst.nbExtraMatchLengths));

    ZL_LOG(TRANSFORM, "#literals = %zu", dst.nbLiteralElts);
    ZL_LOG(TRANSFORM, "#tokens = %zu", dst.nbTokens);
    ZL_LOG(TRANSFORM, "#offsets = %zu", dst.nbOffsets);
    ZL_LOG(TRANSFORM, "#extraLiteralLengths = %zu", dst.nbExtraLiteralLengths);
    ZL_LOG(TRANSFORM, "#extraMatchLengths = %zu", dst.nbExtraMatchLengths);

    return ZL_returnValue(5);
}

static ZL_Report tokensDynGraph(ZL_Graph* gctx, ZL_Edge* tokens)
{
    if (ZL_Graph_getCParam(gctx, ZL_CParam_decompressionLevel) <= 1
        || ZL_Input_numElts(ZL_Edge_getData(tokens)) <= 128) {
        ZL_TRY_LET_T(
                ZL_EdgeList,
                streams,
                ZL_Edge_runNode(tokens, ZL_NODE_INTERPRET_TOKEN_AS_LE));
        ZL_ASSERT_EQ(streams.nbEdges, 1);
        return ZL_Edge_setDestination(streams.edges[0], ZL_GRAPH_BITPACK);
    } else {
        return ZL_Edge_setDestination(tokens, ZL_GRAPH_HUFFMAN);
    }
}

static ZL_Report
quantizeDynGraph(ZL_Graph* gctx, ZL_Edge* stream, ZL_NodeID quantizeNode)
{
    ZL_TRY_LET_T(ZL_EdgeList, streams, ZL_Edge_runNode(stream, quantizeNode));
    ZL_ASSERT_EQ(streams.nbEdges, 2);

    ZL_Edge* const codes = streams.edges[0];
    ZL_GraphID codesGraph;
    if (ZL_Graph_getCParam(gctx, ZL_CParam_decompressionLevel) <= 1) {
        codesGraph = ZL_GRAPH_BITPACK;
    } else {
        codesGraph = ZL_GRAPH_FSE;
    }
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(codes, codesGraph));

    ZL_Edge* const extraBits = streams.edges[1];
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(extraBits, ZL_GRAPH_STORE));

    return ZL_returnSuccess();
}

static size_t getMinStreamSize(ZL_Graph* gctx)
{
    int const value = ZL_Graph_getCParam(gctx, ZL_CParam_minStreamSize);
    return value < 0 ? 0 : (size_t)value;
}

#define FIELDLZ_NUM_SUCCESSORS 5

ZL_Report EI_fieldLzDynGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns)
{
    ZL_RET_R_IF(graph_invalidNumInputs, nbIns != 1);
    ZL_Edge* input     = inputs[0];
    ZL_Input const* in = ZL_Edge_getData(input);
    ZL_ASSERT_NE(ZL_Input_type(in) & (ZL_Type_struct | ZL_Type_numeric), 0);

    // Call to Zstd for unsupported widths
    size_t const eltWidth = ZL_Input_eltWidth(in);
    if (!(eltWidth == 2 || eltWidth == 4 || eltWidth == 8)) {
        return ZL_Edge_setDestination(input, ZL_GRAPH_ZSTD);
    }

    // Convert to struct
    // TODO(terrelln): We could allow ZL_Type_numeric literals graphs when
    // the input type is numeric.
    bool const inputIsNumeric = ZL_Input_type(in) == ZL_Type_numeric;
    if (inputIsNumeric) {
        ZL_TRY_LET_T(
                ZL_EdgeList,
                streams,
                ZL_Edge_runNode(input, ZL_NODE_CONVERT_NUM_TO_TOKEN));
        ZL_ASSERT_EQ(streams.nbEdges, 1);
        input = streams.edges[0];
        in    = ZL_Edge_getData(input);
    }
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_struct);

    // Run FieldLZ node
    // TODO(terrelln): Now that we have a dynamic graph, we can do the LZ
    // parse outside of a node, and then determine whether we've found a
    // significant number of matches. If we haven't we can skip the FieldLZ
    // node entirely, and go straight to the literals graph.
    ZL_IntParam compressionLevelOverride = ZL_Graph_getLocalIntParam(
            gctx, ZL_FIELD_LZ_COMPRESSION_LEVEL_OVERRIDE_PID);
    ZL_LocalParams localParams = { .intParams = { NULL, 0 } };
    if (compressionLevelOverride.paramId != ZL_LP_INVALID_PARAMID) {
        localParams.intParams.intParams   = &compressionLevelOverride;
        localParams.intParams.nbIntParams = 1;
    }
    ZL_TRY_LET_T(
            ZL_EdgeList,
            streams,
            ZL_Edge_runNode_withParams(input, ZL_NODE_FIELD_LZ, &localParams));
    ZL_ASSERT_EQ(streams.nbEdges, FIELDLZ_NUM_SUCCESSORS);

    // Allow overriding each of the successors with a custom graph.
    // Also, store if the streams are below the configured limit.
    uint8_t successorSet[FIELDLZ_NUM_SUCCESSORS] = { 0 };
    ZL_GraphIDList customGraphs  = ZL_Graph_getCustomGraphs(gctx);
    size_t const streamSizeLimit = getMinStreamSize(gctx);
    for (int i = 0; i < FIELDLZ_NUM_SUCCESSORS; ++i) {
        size_t const streamSize =
                ZL_Input_contentSize(ZL_Edge_getData(streams.edges[i]));
        if (streamSize < streamSizeLimit) {
            ZL_RET_R_IF_ERR(
                    ZL_Edge_setDestination(streams.edges[i], ZL_GRAPH_STORE));
            successorSet[i] = 1;
            continue;
        }

        ZL_IntParam const param = ZL_Graph_getLocalIntParam(gctx, i);
        if (param.paramId == i) {
            ZL_RET_R_IF_LT(nodeParameter_invalid, param.paramValue, 0);
            ZL_RET_R_IF_GT(
                    nodeParameter_invalid,
                    (size_t)param.paramValue,
                    customGraphs.nbGraphIDs);
            ZL_GraphID const graph = customGraphs.graphids[param.paramValue];
            ZL_RET_R_IF_ERR(ZL_Edge_setDestination(streams.edges[i], graph));
            successorSet[i] = 1;
        }
    }

    // Get the outputs
    ZL_Edge* const literals = successorSet[0] ? NULL : streams.edges[0];
    ZL_Edge* const tokens   = successorSet[1] ? NULL : streams.edges[1];
    ZL_Edge* const offsets  = successorSet[2] ? NULL : streams.edges[2];
    ZL_Edge* const extraLiteralLengths =
            successorSet[3] ? NULL : streams.edges[3];
    ZL_Edge* const extraMatchLengths =
            successorSet[4] ? NULL : streams.edges[4];

    // Run the successors
    if (literals != NULL) {
        ZL_RET_R_IF_ERR(
                ZL_Edge_setDestination(literals, ZL_GRAPH_FIELD_LZ_LITERALS));
    }
    if (tokens != NULL) {
        ZL_RET_R_IF_ERR(tokensDynGraph(gctx, tokens));
    }
    if (offsets != NULL) {
        ZL_RET_R_IF_ERR(
                quantizeDynGraph(gctx, offsets, ZL_NODE_QUANTIZE_OFFSETS));
    }
    if (extraLiteralLengths != NULL) {
        ZL_RET_R_IF_ERR(quantizeDynGraph(
                gctx, extraLiteralLengths, ZL_NODE_QUANTIZE_LENGTHS));
    }
    if (extraMatchLengths != NULL) {
        ZL_RET_R_IF_ERR(quantizeDynGraph(
                gctx, extraMatchLengths, ZL_NODE_QUANTIZE_LENGTHS));
    }

    return ZL_returnSuccess();
}

ZL_Report
EI_fieldLzLiteralsDynGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns)
{
    (void)gctx;
    ZL_RET_R_IF(graph_invalidNumInputs, nbIns != 1);
    ZL_Edge* literals = inputs[0];
    // Transpose
    // TODO(terrelln): Determine if we should transpose at all.
    // E.g. if we have a small number of literals don't transpose.
    size_t const eltWidth = ZL_Input_eltWidth(ZL_Edge_getData(literals));
    if (eltWidth == 1) {
        ZL_RET_R_IF_ERR(ZL_Edge_setDestination(
                literals, ZL_GRAPH_FIELD_LZ_LITERALS_CHANNEL));
        return ZL_returnSuccess();
    }
    ZL_NodeID const transpose = ZL_Graph_getTransposeSplitNode(gctx, eltWidth);
    ZL_TRY_LET_T(ZL_EdgeList, streams, ZL_Edge_runNode(literals, transpose));
    ZL_ASSERT_EQ(streams.nbEdges, eltWidth);

    // TODO(terrelln): Share information between channels.
    // E.g. if stream i is uncompressible, then stream i+1 is likely to be
    // uncompressible, if we're compressing numeric data.
    for (size_t i = 0; i < streams.nbEdges; ++i) {
        ZL_RET_R_IF_ERR(ZL_Edge_setDestination(
                streams.edges[i], ZL_GRAPH_FIELD_LZ_LITERALS_CHANNEL));
    }

    return ZL_returnSuccess();
}

ZL_GraphID SI_fieldLzLiteralsChannelSelector(
        ZL_Selector const* selCtx,
        ZL_Input const* input,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)customGraphs;
    ZL_ASSERT_EQ(nbCustomGraphs, 0);
    // Wrap the existing selector to make it compatible with graph_registry.c
    ZS2_transposedLiteralStreamSelector_Successors const successors =
            ZS2_transposedLiteralStreamSelector_successors_init();
    return ZS2_transposedLiteralStreamSelector_impl(selCtx, input, &successors);
}

ZL_GraphID ZL_Compressor_registerFieldLZGraph_withLiteralsGraph(
        ZL_Compressor* cgraph,
        ZL_GraphID literals)
{
    ZL_IntParam const literalsGraph = {
        .paramId    = ZL_FIELD_LZ_LITERALS_GRAPH_OVERRIDE_INDEX_PID,
        .paramValue = 0,
    };
    ZL_LocalIntParams const intParams = { .intParams   = &literalsGraph,
                                          .nbIntParams = 1 };
    ZL_LocalParams const localParams  = { .intParams = intParams };

    ZL_ParameterizedGraphDesc desc = {
        .name           = "field_lz_with_literals_graph",
        .graph          = ZL_GRAPH_FIELD_LZ,
        .customGraphs   = &literals,
        .nbCustomGraphs = 1,
        .localParams    = &localParams,
    };

    return ZL_Compressor_registerParameterizedGraph(cgraph, &desc);
}

ZL_GraphID ZL_Compressor_registerFieldLZGraph(ZL_Compressor* cgraph)
{
    (void)cgraph;
    return ZL_GRAPH_FIELD_LZ;
}

ZL_GraphID ZL_Compressor_registerFieldLZGraph_withLevel(
        ZL_Compressor* cgraph,
        int compressionLevel)
{
    ZL_LocalParams localParams = {
        .intParams = ZL_INTPARAMS(
                { ZL_FIELD_LZ_COMPRESSION_LEVEL_OVERRIDE_PID,
                  compressionLevel })
    };

    ZL_ParameterizedGraphDesc desc = {
        .name        = "field_lz_with_level",
        .graph       = ZL_GRAPH_FIELD_LZ,
        .localParams = &localParams,
    };

    return ZL_Compressor_registerParameterizedGraph(cgraph, &desc);
}
