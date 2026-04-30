// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_MERGE_SORTED_ENCODE_MERGE_SORTED_BINDING_H
#define ZSTRONG_TRANSFORMS_MERGE_SORTED_ENCODE_MERGE_SORTED_BINDING_H

#include "openzl/codecs/merge_sorted/graph_merge_sorted.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

ZL_Report
EI_mergeSorted(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_MERGE_SORTED(id)                  \
    { .gd          = MERGE_SORTED_GRAPH(id), \
      .transform_f = EI_mergeSorted,         \
      .name        = "!zl.merge_sorted" }

/**
 * Function graph that selects between the merge sorted graph and backup graph.
 * Selects mergeSortedGraph if input has <= 64 sorted runs, otherwise
 * backupGraph.
 *
 * Custom graphs are expected in order:
 *   [0]: mergeSortedNode graph (single output from ZL_NODE_MERGE_SORTED)
 *   [1]: backupGraph
 */
ZL_Report
mergeSortedSelectorFnGraph(ZL_Graph* graph, ZL_Edge* edges[], size_t nbEdges);

#define MIGRAPH_MERGE_SORTED                                   \
    {                                                          \
        .name           = "!zl.private.merge_sorted_selector", \
        .graph_f        = mergeSortedSelectorFnGraph,          \
        .inputTypeMasks = (ZL_Type[]){ ZL_Type_numeric },      \
        .nbInputs       = 1,                                   \
    }

ZL_END_C_DECLS

#endif
