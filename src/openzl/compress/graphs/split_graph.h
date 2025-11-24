// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_COMPRESS_GRAPHS_SPLIT_GRAPH_H
#define OPENZL_COMPRESS_GRAPHS_SPLIT_GRAPH_H

#include "openzl/zl_graph_api.h"

/**
 * Invokes the custom node, and then passes each output to the corresponding
 * custom graph.
 */
ZL_Report ZL_splitFnGraph(ZL_Graph* graph, ZL_Edge** inputs, size_t numInputs);

/**
 * Routes N input streams to N successor graphs.
 * Input[i] is routed to successor graph[i].
 */
ZL_Report ZL_nToNFnGraph(ZL_Graph* graph, ZL_Edge** inputs, size_t numInputs);

#define MIGRAPH_N_TO_N                                         \
    { .name                = "!zl.n_to_n",                     \
      .graph_f             = ZL_nToNFnGraph,                   \
      .inputTypeMasks      = (const ZL_Type[]){ ZL_Type_any }, \
      .nbInputs            = 1,                                \
      .lastInputIsVariable = 1 }

#endif
