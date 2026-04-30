// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_GRAPHS_SMALL_LENGTHS_GRAPH_H
#define OPENZL_COMPRESS_GRAPHS_SMALL_LENGTHS_GRAPH_H

#include "openzl/zl_graph_api.h"

/**
 * Backend for ZL_GRAPH_COMPRESS_SMALL_LENGTHS
 */
ZL_Report ZL_compressSmallLengthsGraph(
        ZL_Graph* graph,
        ZL_Edge** inputs,
        size_t numInputs);

#endif
