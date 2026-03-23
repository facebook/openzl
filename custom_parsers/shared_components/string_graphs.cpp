// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/shared_components/string_graphs.h"

#include <vector>

#include "openzl/zl_selector.h" // ZL_AUTO_FORMAT_VERSION

namespace openzl::custom_parsers {
ZL_GraphID ZL_Compressor_registerStringTokenize(ZL_Compressor* compressor)
{
    // Note: in managed compression, the ML selector is used instead of zstd
    // for these numeric successors
    std::vector<ZL_GraphID> prefixSuccessors = { ZL_GRAPH_COMPRESS_GENERIC,
                                                 ZL_GRAPH_ZSTD };
    const ZL_GraphID prefixGraph = ZL_Compressor_registerStaticGraph_fromNode(
            compressor,
            ZL_NODE_PREFIX,
            prefixSuccessors.data(),
            prefixSuccessors.size());
    return ZL_Compressor_registerTokenizeGraph(
            compressor,
            ZL_Type_string,
            /* sort */ true,
            prefixGraph,
            ZL_GRAPH_ZSTD);
}

// output 0: indices
// output 1: nulls (should be just a bunch of 0s as string lengths)
// output 2: non-nulls
static ZL_Report
nullAwareDispatchGraphFn(ZL_Graph* gctx, ZL_Edge* inputs[], size_t) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    const auto successors = ZL_Graph_getCustomGraphs(gctx);
    ZL_ERR_IF_NE(successors.nbGraphIDs, 3, node_invalid_input);

    auto* input        = ZL_Edge_getData(inputs[0]);
    const auto numStrs = ZL_Input_numElts(input);
    std::vector<uint16_t> dispatchIdx(numStrs, 1); // 0 -> null, 1 -> non-null
    const auto* strLens = ZL_Input_stringLens(input);
    for (size_t i = 0; i < numStrs; ++i) {
        if (!strLens[i]) {
            dispatchIdx[i] = 0;
        }
    }
    auto result =
            ZL_Edge_runDispatchStringNode(inputs[0], 2, dispatchIdx.data());
    ZL_ERR_IF_ERR(result);
    ZL_ERR_IF_NE(ZL_RES_value(result).nbEdges, 3, node_invalid_input);
    const auto edgeList = ZL_RES_value(result);
    ZL_ERR_IF_ERR(
            ZL_Edge_setDestination(edgeList.edges[0], successors.graphids[0]));
    ZL_ERR_IF_ERR(
            ZL_Edge_setDestination(edgeList.edges[1], successors.graphids[1]));
    ZL_ERR_IF_ERR(
            ZL_Edge_setDestination(edgeList.edges[2], successors.graphids[2]));
    return ZL_returnSuccess();
}

ZL_GraphID registerNullAwareDispatch(
        ZL_Compressor* compressor,
        const std::string& name,
        const std::array<ZL_GraphID, 3>& successors)
{
    ZL_Type inputType = ZL_Type_string;

    ZL_GraphID nullAwareDispatchGraph =
            ZL_Compressor_getGraph(compressor, "null_aware_dispatch");
    if (nullAwareDispatchGraph.gid == ZL_GRAPH_ILLEGAL.gid) {
        ZL_FunctionGraphDesc desc = {
            .name                = "!null_aware_dispatch",
            .graph_f             = nullAwareDispatchGraphFn,
            .inputTypeMasks      = &inputType,
            .nbInputs            = 1,
            .lastInputIsVariable = false,
        };
        nullAwareDispatchGraph =
                ZL_Compressor_registerFunctionGraph(compressor, &desc);
    }
    ZL_ParameterizedGraphDesc const nullAwareDispatchGraphDesc = {
        .name           = name.c_str(),
        .graph          = nullAwareDispatchGraph,
        .customGraphs   = successors.data(),
        .nbCustomGraphs = successors.size(),
    };
    nullAwareDispatchGraph = ZL_Compressor_registerParameterizedGraph(
            compressor, &nullAwareDispatchGraphDesc);

    assert(nullAwareDispatchGraph.gid != ZL_GRAPH_ILLEGAL.gid);
    return nullAwareDispatchGraph;
};

} // namespace openzl::custom_parsers
