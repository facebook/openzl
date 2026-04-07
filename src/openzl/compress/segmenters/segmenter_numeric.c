// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/segmenters/segmenter_numeric.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_selector.h" // ZL_LP_INVALID_PARAMID ...which is probably not where this constant should be

ZL_Report SEGM_numeric(ZL_Segmenter* sctx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    size_t const numInputs = ZL_Segmenter_numInputs(sctx);
    ZL_ASSERT_EQ(numInputs, 1);
    const ZL_Input* const input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ASSERT_NN(input);
    size_t const width = ZL_Input_eltWidth(input);
    ZL_ASSERT(width == 1 || width == 2 || width == 4 || width == 8);

    ZL_IntParam const chunkParam = ZL_Segmenter_getLocalIntParam(
            sctx, ZL_SEGM_NUMERIC_CHUNK_BYTE_SIZE_MAX_PID);
    size_t const chunkByteSizeMax =
            (chunkParam.paramId != ZL_LP_INVALID_PARAMID)
            ? (size_t)chunkParam.paramValue
            : (16 << 20) /* default to 16MB */;
    ZL_ERR_IF_LT(
            chunkByteSizeMax,
            width,
            nodeParameter_invalid,
            "chunk size must produce at least one element");
    size_t const chunkEltSizeMax = chunkByteSizeMax / width;

    ZL_GraphIDList const customGraphs = ZL_Segmenter_getCustomGraphs(sctx);
    ZL_GraphID const headGraph        = (customGraphs.nbGraphIDs >= 1)
                   ? customGraphs.graphids[0]
                   : ZL_GRAPH_NUMERIC_COMPRESS;

    size_t numElts = ZL_Input_numElts(input);
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
