// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_GRAPHS_SDDL_V2_H
#define OPENZL_GRAPHS_SDDL_V2_H

#include "openzl/zl_graph_api.h"

ZL_Report ZL_SDDLv2_compress(ZL_Graph* graph, 
      ZL_Edge* inputs[],
      size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR;

#endif // OPENZL_GRAPHS_SDDL_V2_H
