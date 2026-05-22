// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/lz/encode_lz_binding.h"

#include "openzl/codecs/common/fast_table.h"
#include "openzl/codecs/lz/common_field_lz.h"
#include "openzl/codecs/lz/encode_field_lz_literals_selector.h"
#include "openzl/codecs/lz/encode_lz_kernel.h"
#include "openzl/codecs/zl_entropy.h"
#include "openzl/codecs/zl_lz.h"
#include "openzl/codecs/zl_mux_lengths.h"
#include "openzl/codecs/zl_partition.h"
#include "openzl/compress/dyngraph_interface.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_graph_api.h"

#define ZL_LZ_MIN_WINDOW_LOG 10
#define ZL_LZ_MAX_WINDOW_LOG 28

/**
 * Set the maximum bytes to process to 4B to avoid overflow in the match finder.
 * It could likely be higher, but this is close enough to 2^32-1.
 */
static const size_t kLzContentSizeBytes = 4000000000u;

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
            kLzContentSizeBytes,
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

static int getCompressionLevelEncoder(const ZL_Encoder* eictx)
{
    const ZL_IntParam compressionLevel =
            ZL_Encoder_getLocalIntParam(eictx, ZL_LzParam_compressionLevel);
    if (compressionLevel.paramId == ZL_LP_INVALID_PARAMID) {
        return ZL_Encoder_getCParam(eictx, ZL_CParam_compressionLevel);
    } else {
        return compressionLevel.paramValue;
    }
}

static int getAcceleration(const ZL_Encoder* eictx)
{
    const ZL_IntParam acceleration =
            ZL_Encoder_getLocalIntParam(eictx, ZL_LzParam_acceleration);
    if (acceleration.paramId == ZL_LP_INVALID_PARAMID) {
        const int compressionLevel = getCompressionLevelEncoder(eictx);
        return compressionLevel < 0 ? -compressionLevel : 1;
    } else {
        return ZL_MAX(acceleration.paramValue, 1);
    }
}

static uint32_t getWindowLog(const ZL_Encoder* eictx, size_t srcSize)
{
    const ZL_IntParam windowLogParam =
            ZL_Encoder_getLocalIntParam(eictx, ZL_LzParam_windowLog);

    int windowLog = ZL_LZ_MAX_WINDOW_LOG;
    if (windowLogParam.paramId != ZL_LP_INVALID_PARAMID) {
        windowLog = windowLogParam.paramValue;
    }

    windowLog = ZL_MIN(windowLog, ZL_nextPow2(srcSize));

    return (uint32_t)ZL_MAX(
            ZL_LZ_MIN_WINDOW_LOG, ZL_MIN(windowLog, ZL_LZ_MAX_WINDOW_LOG));
}

ZL_Report EI_lz(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in   = ins[0];
    size_t const srcSize = ZL_Input_numElts(in);

    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ERR_IF_GT(
            srcSize,
            kLzContentSizeBytes,
            temporaryLibraryLimitation,
            "LZ only supports up to 4B of input");

    size_t const maxNumSeq = ZL_Lz_maxNumSequences(srcSize);
    // Offsets are up to than 2^windowLog-1; use 32-bits when they won't fit u16
    const uint32_t windowLog = getWindowLog(eictx, srcSize);
    const size_t offsetEltWidth =
            (windowLog > 16) ? sizeof(uint32_t) : sizeof(uint16_t);

    // Create output streams
    size_t const literalsCapacity = srcSize + ZL_LZ_LIT_OVER_LENGTH;
    ZL_Output* const literals =
            ZL_Encoder_createTypedStream(eictx, 0, literalsCapacity, 1);
    ZL_Output* const offsets =
            ZL_Encoder_createTypedStream(eictx, 1, maxNumSeq, offsetEltWidth);
    ZL_Output* const literalLengths =
            ZL_Encoder_createTypedStream(eictx, 2, maxNumSeq, 2);
    ZL_Output* const matchLengths =
            ZL_Encoder_createTypedStream(eictx, 3, maxNumSeq, 2);

    ZL_ERR_IF_NULL(literals, allocation);
    ZL_ERR_IF_NULL(offsets, allocation);
    ZL_ERR_IF_NULL(literalLengths, allocation);
    ZL_ERR_IF_NULL(matchLengths, allocation);

    // Allocate hash table from scratch space
    size_t const hashTableSize =
            ZS_FastTable_tableSize(ZL_Lz_tableLog(windowLog));
    void* hashTableMem = ZL_Encoder_getScratchSpace(eictx, hashTableSize);
    ZL_ERR_IF_NULL(hashTableMem, allocation);

    ZL_Lz_OutSequences dst = {
        .literals         = (uint8_t*)ZL_Output_ptr(literals),
        .literalsCapacity = literalsCapacity,
        .numLiterals      = 0,

        .offsets           = ZL_Output_ptr(offsets),
        .offsetWidth       = offsetEltWidth,
        .literalLengths    = (uint16_t*)ZL_Output_ptr(literalLengths),
        .matchLengths      = (uint16_t*)ZL_Output_ptr(matchLengths),
        .sequencesCapacity = maxNumSeq,
        .numSequences      = 0,
    };

    ZL_Lz_encode(
            &dst,
            (const uint8_t*)ZL_Input_ptr(in),
            srcSize,
            hashTableMem,
            windowLog,
            getAcceleration(eictx));

    // Write the original size as a varint codec header
    uint8_t header[ZL_VARINT_LENGTH_64];
    size_t const headerSize = ZL_varintEncode(srcSize, header);
    ZL_Encoder_sendCodecHeader(eictx, header, headerSize);

    ZL_ERR_IF_ERR(ZL_Output_commit(literals, dst.numLiterals));
    ZL_ERR_IF_ERR(ZL_Output_commit(offsets, dst.numSequences));
    ZL_ERR_IF_ERR(ZL_Output_commit(literalLengths, dst.numSequences));
    ZL_ERR_IF_ERR(ZL_Output_commit(matchLengths, dst.numSequences));

    ZL_ERR_IF_ERR(ZL_Output_setIntMetadata(
            matchLengths, ZL_LZ_MIN_MATCH_LENGTH_METADATA_ID, ZL_LZ_MIN_MATCH));

    return ZL_returnSuccess();
}

static ZL_Report tokensDynGraph(ZL_Graph* gctx, ZL_Edge* tokens)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    if (ZL_Graph_getCParam(gctx, ZL_CParam_decompressionLevel) <= 1
        || ZL_Input_numElts(ZL_Edge_getData(tokens)) <= 128) {
        ZL_TRY_LET(
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
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    ZL_TRY_LET(ZL_EdgeList, streams, ZL_Edge_runNode(stream, quantizeNode));
    ZL_ASSERT_EQ(streams.nbEdges, 2);

    ZL_Edge* const codes = streams.edges[0];
    ZL_GraphID codesGraph;
    if (ZL_Graph_getCParam(gctx, ZL_CParam_decompressionLevel) <= 1) {
        codesGraph = ZL_GRAPH_BITPACK;
    } else {
        codesGraph = ZL_GRAPH_FSE;
    }
    ZL_ERR_IF_ERR(ZL_Edge_setDestination(codes, codesGraph));

    ZL_Edge* const extraBits = streams.edges[1];
    ZL_ERR_IF_ERR(ZL_Edge_setDestination(extraBits, ZL_GRAPH_STORE));

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
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    ZL_ERR_IF(nbIns != 1, graph_invalidNumInputs);
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
        ZL_TRY_LET(
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
    ZL_TRY_LET(
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
            ZL_ERR_IF_ERR(
                    ZL_Edge_setDestination(streams.edges[i], ZL_GRAPH_STORE));
            successorSet[i] = 1;
            continue;
        }

        ZL_IntParam const param = ZL_Graph_getLocalIntParam(gctx, i);
        if (param.paramId == i) {
            ZL_ERR_IF_LT(param.paramValue, 0, nodeParameter_invalid);
            ZL_ERR_IF_GE(
                    (size_t)param.paramValue,
                    customGraphs.nbGraphIDs,
                    nodeParameter_invalid);
            ZL_GraphID const graph = customGraphs.graphids[param.paramValue];
            ZL_ERR_IF_ERR(ZL_Edge_setDestination(streams.edges[i], graph));
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
        ZL_ERR_IF_ERR(
                ZL_Edge_setDestination(literals, ZL_GRAPH_FIELD_LZ_LITERALS));
    }
    if (tokens != NULL) {
        ZL_ERR_IF_ERR(tokensDynGraph(gctx, tokens));
    }
    if (offsets != NULL) {
        ZL_ERR_IF_ERR(
                quantizeDynGraph(gctx, offsets, ZL_NODE_QUANTIZE_OFFSETS));
    }
    if (extraLiteralLengths != NULL) {
        ZL_ERR_IF_ERR(quantizeDynGraph(
                gctx, extraLiteralLengths, ZL_NODE_QUANTIZE_LENGTHS));
    }
    if (extraMatchLengths != NULL) {
        ZL_ERR_IF_ERR(quantizeDynGraph(
                gctx, extraMatchLengths, ZL_NODE_QUANTIZE_LENGTHS));
    }

    return ZL_returnSuccess();
}

ZL_Report
EI_fieldLzLiteralsDynGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    (void)gctx;
    ZL_ERR_IF(nbIns != 1, graph_invalidNumInputs);
    ZL_Edge* literals = inputs[0];
    // Transpose
    // TODO(terrelln): Determine if we should transpose at all.
    // E.g. if we have a small number of literals don't transpose.
    size_t const eltWidth = ZL_Input_eltWidth(ZL_Edge_getData(literals));
    if (eltWidth == 1) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                literals, ZL_GRAPH_FIELD_LZ_LITERALS_CHANNEL));
        return ZL_returnSuccess();
    }
    ZL_NodeID const transpose = ZL_Graph_getTransposeSplitNode(gctx, eltWidth);
    ZL_TRY_LET(ZL_EdgeList, streams, ZL_Edge_runNode(literals, transpose));
    ZL_ASSERT_EQ(streams.nbEdges, eltWidth);

    // TODO(terrelln): Share information between channels.
    // E.g. if stream i is uncompressible, then stream i+1 is likely to be
    // uncompressible, if we're compressing numeric data.
    for (size_t i = 0; i < streams.nbEdges; ++i) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(
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

static int getCompressionLevelGraph(const ZL_Graph* graph)
{
    const ZL_IntParam compressionLevel =
            ZL_Graph_getLocalIntParam(graph, ZL_LzParam_compressionLevel);
    if (compressionLevel.paramId == ZL_LP_INVALID_PARAMID) {
        return ZL_Graph_getCParam(graph, ZL_CParam_compressionLevel);
    } else {
        return compressionLevel.paramValue;
    }
}

static ZL_GraphID getGraph(
        const ZL_Graph* gctx,
        ZL_GraphIDList customGraphs,
        int overrideParam,
        ZL_GraphID defaultGraph)
{
    const ZL_IntParam param = ZL_Graph_getLocalIntParam(gctx, overrideParam);
    if (param.paramId == ZL_LP_INVALID_PARAMID) {
        return defaultGraph;
    } else if ((size_t)param.paramValue >= customGraphs.nbGraphIDs) {
        return ZL_GRAPH_ILLEGAL;
    } else {
        return customGraphs.graphids[param.paramValue];
    }
}

static ZL_Report setEntropyDestinationOrOverride(
        const ZL_Graph* gctx,
        ZL_Edge* edge,
        ZL_GraphIDList customGraphs,
        int overrideParam,
        ZL_GraphID entropyGraph,
        int minGainForHuffmanBytes)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(edge);

    const ZL_GraphID override =
            getGraph(gctx, customGraphs, overrideParam, ZL_GRAPH_ILLEGAL);
    if (override.gid != ZL_GRAPH_ILLEGAL.gid) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(edge, override));
    } else {
        ZL_ERR_IF_ERR(ZL_Edge_setEntropyDestination(
                edge, entropyGraph, minGainForHuffmanBytes, -1));
    }
    return ZL_returnSuccess();
}

ZL_Report EI_lzDynGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_Edge* input         = inputs[0];
    const size_t inputSize = ZL_Input_contentSize(ZL_Edge_getData(input));
    // Huffman must save at least this much to be considered.
    // The combination of a small percent of the source size and a fixed
    // component strongly discourages Huffman for small inputs where the speed
    // penalty is large, and has little impact on large inputs.
    const int minGainForHuffmanBytes = (int)(inputSize / 200) + 50;

    const ZL_LocalParams* localParams = GCTX_getAllLocalParams(gctx);

    // Run the LZ node & forward the graph's parameters
    ZL_TRY_LET(
            ZL_EdgeList,
            streams,
            ZL_Edge_runNode_withParams(input, ZL_NODE_LZ, localParams));
    ZL_ASSERT_EQ(streams.nbEdges, 4);

    ZL_Edge* const literals       = streams.edges[0];
    ZL_Edge* const offsets        = streams.edges[1];
    ZL_Edge* const literalLengths = streams.edges[2];
    ZL_Edge* const matchLengths   = streams.edges[3];

    const int compressionLevel        = getCompressionLevelGraph(gctx);
    const ZL_GraphIDList customGraphs = ZL_Graph_getCustomGraphs(gctx);
    const ZL_GraphID huffOrStore =
            compressionLevel >= 0 ? ZL_GRAPH_HUFFMAN : ZL_GRAPH_STORE;

    const ZL_GraphID offsetsGraph = getGraph(
            gctx,
            customGraphs,
            ZL_LzParam_offsetsGraphIdx,
            ZL_GRAPH_PARTITION_BITPACK);
    const ZL_GraphID muxLengthsGraph = getGraph(
            gctx,
            customGraphs,
            ZL_LzParam_muxLengthsGraphIdx,
            ZL_GRAPH_ILLEGAL);

    ZL_ERR_IF_ERR(setEntropyDestinationOrOverride(
            gctx,
            literals,
            customGraphs,
            ZL_LzParam_literalsGraphIdx,
            huffOrStore,
            minGainForHuffmanBytes));
    ZL_ERR_IF_ERR(ZL_Edge_setDestination(offsets, offsetsGraph));

    ZL_Edge* muxInputs[2] = { literalLengths, matchLengths };

    if (muxLengthsGraph.gid != ZL_GRAPH_ILLEGAL.gid) {
        ZL_ERR_IF_ERR(ZL_Edge_setParameterizedDestination(
                muxInputs, 2, muxLengthsGraph, NULL));
    } else {
        // Run mux_lengths node (auto-computes split point and min match length)
        ZL_TRY_LET(
                ZL_EdgeList,
                muxStreams,
                ZL_Edge_runMultiInputNode(muxInputs, 2, ZL_NODE_MUX_LENGTHS));
        ZL_ASSERT_EQ(muxStreams.nbEdges, 2);

        ZL_ERR_IF_ERR(setEntropyDestinationOrOverride(
                gctx,
                muxStreams.edges[0],
                customGraphs,
                ZL_LzParam_muxedBytesGraphIdx,
                huffOrStore,
                minGainForHuffmanBytes));

        ZL_ERR_IF_ERR(setEntropyDestinationOrOverride(
                gctx,
                muxStreams.edges[1],
                customGraphs,
                ZL_LzParam_overflowLengthsGraphIdx,
                ZL_GRAPH_COMPRESS_SMALL_LENGTHS,
                minGainForHuffmanBytes));
    }

    return ZL_returnSuccess();
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
