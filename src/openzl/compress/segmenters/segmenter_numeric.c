// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/segmenters/segmenter_numeric.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h"

ZL_Report SEGM_numeric(ZL_Segmenter* sctx)
{
    size_t const numInputs = ZL_Segmenter_numInputs(sctx);
    ZL_ASSERT_EQ(numInputs, 1);
    const ZL_Input* const input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ASSERT_NN(input);
    size_t const width = ZL_Input_eltWidth(input);
    ZL_ASSERT(width == 1 || width == 2 || width == 4 || width == 8);

    // Note: Currently, static chunk size.
    // Tomorrow: global parameter, then local parameter.
    size_t const chunkByteSizeMax = 16 << 20;
    size_t const chunkEltSizeMax  = chunkByteSizeMax / width;

    // Note: Currently, static head graph.
    // Tomorrow: selectable
    ZL_GraphID const headGraph = ZL_GRAPH_NUMERIC_COMPRESS;

    size_t numElts = ZL_Input_numElts(input);
    while (numElts > chunkEltSizeMax) {
        ZL_RET_R_IF_ERR(ZL_Segmenter_processChunk(
                sctx, &chunkEltSizeMax, 1, headGraph, NULL));
        numElts -= chunkEltSizeMax;
    }
    ZL_ASSERT_LE(numElts, chunkEltSizeMax);
    ZL_RET_R_IF_ERR(
            ZL_Segmenter_processChunk(sctx, &numElts, 1, headGraph, NULL));

    return ZL_returnSuccess();
}
