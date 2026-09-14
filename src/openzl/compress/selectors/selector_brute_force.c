// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/selectors/selector_brute_force.h"

#include "openzl/codecs/zl_brute_force_selector.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/compress/implicit_conversion.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_selector.h"

ZL_GraphID SI_selector_brute_force(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    ZL_ASSERT_NN(selCtx);
    ZL_ASSERT_NN(inputStream);
    ZL_ASSERT(nbCustomGraphs == 0 || customGraphs != NULL);

    const ZL_Type inputType = ZL_Input_type(inputStream);

    // brute force all graphs
    size_t bestSize = ZL_Input_contentSize(inputStream);
    if (inputType == ZL_Type_string) {
        bestSize += ZL_Input_numElts(inputStream) * sizeof(uint32_t);
    }
    int64_t bestIdx = -1;
    for (size_t i = 0; i < nbCustomGraphs; ++i) {
        // Skip successors that can't be fed the input type, since the
        // successor list is user-provided and isn't validated at registration.
        if (!ICONV_isCompatible(
                    inputType,
                    ZL_Selector_getInput0MaskForGraph(
                            selCtx, customGraphs[i]))) {
            continue;
        }
        ZL_GraphReport gr =
                ZL_Selector_tryGraph(selCtx, inputStream, customGraphs[i]);
        if (ZL_isError(gr.finalCompressedSize)) {
            continue;
        }
        size_t currSize = ZL_validResult(gr.finalCompressedSize);
        // printf("curr: %zu, best: %zu\n", currSize, bestSize);
        if (currSize < bestSize) {
            bestSize = currSize;
            bestIdx  = (int64_t)i;
        }
    }
    if (bestIdx == -1) {
        return ZL_GRAPH_STORE;
    }
    return customGraphs[bestIdx];
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildBruteForceSelectorGraph(
        ZL_Compressor* cgraph,
        const ZL_GraphID* successors,
        size_t numSuccessors)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, cgraph);
    ZL_ERR_IF_EQ(
            numSuccessors,
            0,
            parameter_invalid,
            "brute force selector requires at least one successor");
    const ZL_GraphParameters params = {
        .customGraphs   = successors,
        .nbCustomGraphs = numSuccessors,
    };
    return ZL_Compressor_parameterizeGraph(
            cgraph, ZL_GRAPH_BRUTE_FORCE, &params);
}
