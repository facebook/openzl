// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/segmenters/segmenter_serial.h"
#include "openzl/codecs/zl_generic.h"    // ZL_GRAPH_COMPRESS_GENERIC
#include "openzl/codecs/zl_segmenters.h" // ZL_SEGMENT_SERIAL
#include "openzl/common/assertion.h"
#include "openzl/zl_compress.h" // ZL_MIN_CHUNK_SIZE
#include "openzl/zl_compressor.h"
#include "openzl/zl_input.h"
#include "openzl/zl_selector.h" // ZL_LP_INVALID_PARAMID
#include "openzl/zl_version.h"

#include <limits.h> // INT_MAX

ZL_Report SEGM_serial(ZL_Segmenter* sctx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    size_t const numInputs = ZL_Segmenter_numInputs(sctx);
    ZL_ASSERT_EQ(numInputs, 1);
    const ZL_Input* const input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ASSERT_NN(input);

    /* read chunk size from local params, or use default. Reject negative
     * paramValue before the size_t cast — a corrupted compressor could
     * otherwise wrap to a huge size and silently disable chunking. */
    ZL_IntParam const chunkParam = ZL_Segmenter_getLocalIntParam(
            sctx, ZL_SEGMENT_SERIAL_CHUNK_BYTE_SIZE_PARAM);
    if (chunkParam.paramId != ZL_LP_INVALID_PARAMID) {
        ZL_ERR_IF_LT(chunkParam.paramValue, 0, parameter_invalid);
    }
    size_t const chunkByteSizeMax =
            (chunkParam.paramId != ZL_LP_INVALID_PARAMID)
            ? (size_t)chunkParam.paramValue
            : ZL_DEFAULT_SEGMENTER_CHUNK_BYTE_SIZE;
    ZL_ERR_IF_NOT(chunkByteSizeMax > 0, parameter_invalid);

    /* retrieve successor graph (release-mode-checked: a corrupted compressor
     * could parameterize to nbCustomGraphs == 0 and cause an OOB read) */
    ZL_GraphIDList const customGraphs = ZL_Segmenter_getCustomGraphs(sctx);
    ZL_ERR_IF_LT(customGraphs.nbGraphIDs, 1, parameter_invalid);
    ZL_GraphID const headGraph = customGraphs.graphids[0];

    size_t totalBytes = ZL_Input_contentSize(input);
    /* Old wire formats cannot encode segmented output. */
    if (ZL_Segmenter_getCParam(sctx, ZL_CParam_formatVersion)
        < ZL_CHUNK_VERSION_MIN) {
        ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
                sctx, &totalBytes, 1, headGraph, NULL));
        return ZL_returnSuccess();
    }

    while (totalBytes > chunkByteSizeMax) {
        ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
                sctx, &chunkByteSizeMax, 1, headGraph, NULL));
        totalBytes -= chunkByteSizeMax;
    }
    ZL_ASSERT_LE(totalBytes, chunkByteSizeMax);
    ZL_ERR_IF_ERR(
            ZL_Segmenter_processChunk(sctx, &totalBytes, 1, headGraph, NULL));

    return ZL_returnSuccess();
}

/* ---- Registration helper ---- */

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildSerialSegmenter2(
        ZL_Compressor* compressor,
        size_t chunkByteSize,
        ZL_GraphID successorGraph)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);

    /* Use default successor if none specified */
    if (!ZL_GraphID_isValid(successorGraph)) {
        successorGraph = ZL_GRAPH_COMPRESS_GENERIC;
    }

    /* chunkByteSize == 0 means "use the segmenter's built-in default"
     * (matches the SDDL2 and C++ wrapper conventions). */
    if (chunkByteSize == 0) {
        chunkByteSize = ZL_DEFAULT_SEGMENTER_CHUNK_BYTE_SIZE;
    }
    /* Enforce ZL_MIN_CHUNK_SIZE: ZL_compressBound() assumes chunks of at
     * least this size; smaller chunks may produce output exceeding the
     * bound. */
    ZL_ERR_IF_LT(chunkByteSize, ZL_MIN_CHUNK_SIZE, parameter_invalid);
    /* Must fit in int (ZL_IntParam::paramValue is int) */
    ZL_ERR_IF_NOT(chunkByteSize <= (size_t)INT_MAX, parameter_invalid);

    /* Parameterize with chunk size and successor graph */
    ZL_IntParam intParams[] = {
        { .paramId    = ZL_SEGMENT_SERIAL_CHUNK_BYTE_SIZE_PARAM,
          .paramValue = (int)chunkByteSize },
    };
    ZL_LocalParams localParams = {
        .intParams = { .intParams = intParams, .nbIntParams = 1 },
    };
    ZL_ParameterizedGraphDesc const desc = {
        .graph          = ZL_SEGMENT_SERIAL,
        .customGraphs   = &successorGraph,
        .nbCustomGraphs = 1,
        .localParams    = &localParams,
    };
    ZL_GraphID const result =
            ZL_Compressor_registerParameterizedGraph(compressor, &desc);
    ZL_ERR_IF_NOT(ZL_GraphID_isValid(result), graph_invalid);
    return ZL_WRAP_VALUE(result);
}

ZL_GraphID ZL_Compressor_buildSerialSegmenter(
        ZL_Compressor* compressor,
        size_t chunkByteSize,
        ZL_GraphID successorGraph)
{
    ZL_RESULT_OF(ZL_GraphID)
    res = ZL_Compressor_buildSerialSegmenter2(
            compressor, chunkByteSize, successorGraph);
    if (ZL_RES_isError(res)) {
        return ZL_GRAPH_ILLEGAL;
    } else {
        return ZL_RES_value(res);
    }
}
