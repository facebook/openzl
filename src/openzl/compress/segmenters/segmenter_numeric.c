// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/segmenters/segmenter_numeric.h"
#include "openzl/codecs/zl_segmenters.h" // ZL_SEGMENT_NUM*_FROM_SERIAL
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_input.h"
#include "openzl/zl_selector.h" // ZL_LP_INVALID_PARAMID
#include "openzl/zl_version.h"

#include <limits.h> // INT_MAX

ZL_Report SEGM_numeric(ZL_Segmenter* sctx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    size_t const numInputs = ZL_Segmenter_numInputs(sctx);
    ZL_ASSERT_EQ(numInputs, 1);
    const ZL_Input* const input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ASSERT_NN(input);
    size_t const width = ZL_Input_eltWidth(input);
    ZL_ASSERT(width == 1 || width == 2 || width == 4 || width == 8);

    // Note: Currently, shared compile-time default chunk size.
    // Tomorrow: global parameter, then local parameter.
    size_t const chunkByteSizeMax = ZL_DEFAULT_SEGMENTER_CHUNK_BYTE_SIZE;
    size_t const chunkEltSizeMax  = chunkByteSizeMax / width;
    ZL_GraphID const headGraph    = ZL_GRAPH_NUMERIC_COMPRESS;

    size_t numElts = ZL_Input_numElts(input);
    if (ZL_Segmenter_getCParam(sctx, ZL_CParam_formatVersion)
        < ZL_CHUNK_VERSION_MIN) {
        ZL_ERR_IF_ERR(
                ZL_Segmenter_processChunk(sctx, &numElts, 1, headGraph, NULL));
        return ZL_returnSuccess();
    }

    while (numElts > chunkEltSizeMax) {
        ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
                sctx, &chunkEltSizeMax, 1, headGraph, NULL));
        numElts -= chunkEltSizeMax;
    }
    ZL_ASSERT_LE(numElts, chunkEltSizeMax);
    ZL_ERR_IF_ERR(
            ZL_Segmenter_processChunk(sctx, &numElts, 1, headGraph, NULL));

    return ZL_returnSuccess();
}

/* ---- Serial-input numeric segmenter ---- */

ZL_Report SEGM_numFromSerial(ZL_Segmenter* sctx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    size_t const numInputs = ZL_Segmenter_numInputs(sctx);
    ZL_ASSERT_EQ(numInputs, 1);
    const ZL_Input* const input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ASSERT_NN(input);

    /* read element width from local params */
    ZL_IntParam const widthParam = ZL_Segmenter_getLocalIntParam(
            sctx, SEGM_NUM_FROM_SERIAL_ELT_WIDTH_ID);
    size_t const width = (widthParam.paramId != ZL_LP_INVALID_PARAMID)
            ? (size_t)widthParam.paramValue
            : 1;
    ZL_ERR_IF_NOT(
            width == 1 || width == 2 || width == 4 || width == 8,
            parameter_invalid);

    /* read chunk size from local params, or use default */
    ZL_IntParam const chunkParam = ZL_Segmenter_getLocalIntParam(
            sctx, SEGM_NUM_FROM_SERIAL_CHUNK_BYTE_SIZE_ID);
    size_t const chunkByteSizeMax =
            (chunkParam.paramId != ZL_LP_INVALID_PARAMID)
            ? (size_t)chunkParam.paramValue
            : SEGM_NUM_FROM_SERIAL_DEFAULT_CHUNK_SIZE;

    /* align chunk size down to element width */
    size_t const chunkByteSizeAligned =
            chunkByteSizeMax - (chunkByteSizeMax % width);
    ZL_ERR_IF_NOT(chunkByteSizeAligned > 0, parameter_invalid);

    /* retrieve successor graph */
    ZL_GraphIDList const customGraphs = ZL_Segmenter_getCustomGraphs(sctx);
    ZL_ASSERT(customGraphs.nbGraphIDs >= 1);
    ZL_GraphID const headGraph = customGraphs.graphids[0];

    size_t totalBytes = ZL_Input_contentSize(input);
    /* Old wire formats cannot encode segmented output. */
    if (ZL_Segmenter_getCParam(sctx, ZL_CParam_formatVersion)
        < ZL_CHUNK_VERSION_MIN) {
        ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
                sctx, &totalBytes, 1, headGraph, NULL));
        return ZL_returnSuccess();
    }

    while (totalBytes > chunkByteSizeAligned) {
        ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
                sctx, &chunkByteSizeAligned, 1, headGraph, NULL));
        totalBytes -= chunkByteSizeAligned;
    }
    ZL_ASSERT_LE(totalBytes, chunkByteSizeAligned);
    ZL_ERR_IF_ERR(
            ZL_Segmenter_processChunk(sctx, &totalBytes, 1, headGraph, NULL));

    return ZL_returnSuccess();
}

/* ---- Registration helper ---- */

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildNumFromSerialSegmenter2(
        ZL_Compressor* compressor,
        size_t eltByteWidth,
        size_t chunkByteSize,
        ZL_GraphID successorGraph)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);

    /* Select the standard segmenter head and default successor
     * for this element width */
    ZL_GraphID baseGraph;
    ZL_GraphID defaultSuccessor;
    switch (eltByteWidth) {
        case 1:
            baseGraph        = ZL_SEGMENT_NUM8_FROM_SERIAL;
            defaultSuccessor = ZL_GRAPH_INTERPRET_NUM8_COMPRESS;
            break;
        case 2:
            baseGraph        = ZL_SEGMENT_NUM16_FROM_SERIAL;
            defaultSuccessor = ZL_GRAPH_INTERPRET_NUM16_COMPRESS;
            break;
        case 4:
            baseGraph        = ZL_SEGMENT_NUM32_FROM_SERIAL;
            defaultSuccessor = ZL_GRAPH_INTERPRET_NUM32_COMPRESS;
            break;
        case 8:
            baseGraph        = ZL_SEGMENT_NUM64_FROM_SERIAL;
            defaultSuccessor = ZL_GRAPH_INTERPRET_NUM64_COMPRESS;
            break;
        default:
            ZL_ERR_IF_NOT(0, parameter_invalid);
    }

    /* Use default successor if none specified */
    if (!ZL_GraphID_isValid(successorGraph)) {
        successorGraph = defaultSuccessor;
    }

    /* Chunk size must be at least one element wide and fit in int
     * (ZL_IntParam::paramValue is int) */
    ZL_ERR_IF_NOT(chunkByteSize >= eltByteWidth, parameter_invalid);
    ZL_ERR_IF_NOT(chunkByteSize <= (size_t)INT_MAX, parameter_invalid);

    /* Parameterize with element width, chunk size, and successor graph.
     * Both params must be provided because parameterization replaces
     * (not merges) the local params from the base graph. */
    ZL_IntParam intParams[] = {
        { .paramId    = SEGM_NUM_FROM_SERIAL_ELT_WIDTH_ID,
          .paramValue = (int)eltByteWidth },
        { .paramId    = SEGM_NUM_FROM_SERIAL_CHUNK_BYTE_SIZE_ID,
          .paramValue = (int)chunkByteSize },
    };
    ZL_LocalParams localParams = {
        .intParams = { .intParams = intParams, .nbIntParams = 2 },
    };
    ZL_ParameterizedGraphDesc const desc = {
        .graph          = baseGraph,
        .customGraphs   = &successorGraph,
        .nbCustomGraphs = 1,
        .localParams    = &localParams,
    };
    ZL_GraphID const result =
            ZL_Compressor_registerParameterizedGraph(compressor, &desc);
    ZL_ERR_IF_NOT(ZL_GraphID_isValid(result), graph_invalid);
    return ZL_WRAP_VALUE(result);
}

ZL_GraphID ZL_Compressor_buildNumFromSerialSegmenter(
        ZL_Compressor* compressor,
        size_t eltByteWidth,
        size_t chunkByteSize,
        ZL_GraphID successorGraph)
{
    ZL_RESULT_OF(ZL_GraphID)
    res = ZL_Compressor_buildNumFromSerialSegmenter2(
            compressor, eltByteWidth, chunkByteSize, successorGraph);
    if (ZL_RES_isError(res)) {
        return ZL_GRAPH_ILLEGAL;
    } else {
        return ZL_RES_value(res);
    }
}
