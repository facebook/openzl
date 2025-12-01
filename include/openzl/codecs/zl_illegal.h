// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_ILLEGAL_H
#define ZSTRONG_CODECS_ILLEGAL_H

#include "openzl/zl_graphs.h"
#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Error node, to specify an error in functions that return ZL_NodeID
#define ZL_NODE_ILLEGAL ZL_MAKE_NODE_ID(ZL_StandardNodeID_illegal)

// Error graph, to specify an error in functions that return ZL_GraphID
#define ZL_GRAPH_ILLEGAL ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_illegal)

#if defined(__cplusplus)
} // extern "C"
#endif

#endif
