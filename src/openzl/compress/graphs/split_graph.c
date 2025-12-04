// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graphs/split_graph.h"
#include "openzl/common/assertion.h"

ZL_Report ZL_splitFnGraph(ZL_Graph* graph, ZL_Edge** inputs, size_t numInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    ZL_ASSERT_EQ(numInputs, 1);
    ZL_ASSERT_NN(inputs);
    ZL_ASSERT_NN(graph);

    const ZL_NodeIDList nodes = ZL_Graph_getCustomNodes(graph);
    ZL_ERR_IF_NE(nodes.nbNodeIDs, 1, parameter_invalid);

    ZL_TRY_LET(
            ZL_EdgeList, fields, ZL_Edge_runNode(inputs[0], nodes.nodeids[0]));

    const ZL_GraphIDList graphs = ZL_Graph_getCustomGraphs(graph);
    ZL_ERR_IF_NE(graphs.nbGraphIDs, fields.nbEdges, parameter_invalid);

    for (size_t i = 0; i < fields.nbEdges; ++i) {
        ZL_ERR_IF_ERR(
                ZL_Edge_setDestination(fields.edges[i], graphs.graphids[i]));
    }
    return ZL_returnSuccess();
}

ZL_Report ZL_nToNFnGraph(ZL_Graph* graph, ZL_Edge** inputs, size_t numInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    ZL_ASSERT_NN(inputs);
    ZL_ASSERT_NN(graph);

    const ZL_GraphIDList graphs = ZL_Graph_getCustomGraphs(graph);
    ZL_ERR_IF_NE(graphs.nbGraphIDs, numInputs, parameter_invalid);

    for (size_t i = 0; i < numInputs; ++i) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputs[i], graphs.graphids[i]));
    }
    return ZL_returnSuccess();
}
