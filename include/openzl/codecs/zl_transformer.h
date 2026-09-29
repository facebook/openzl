// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_TRANSFORMER_H
#define ZSTRONG_CODECS_TRANSFORMER_H

#include "openzl/zl_graphs.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Compresses numeric data by using a pretrained model to pick the next
 * transform to apply. Each input is handled independently: the model builds a
 * transform chain for it one decision at a time, re-consulting itself on the
 * transform's outputs. When the model cannot be consulted, e.g. because the
 * chain has grown too deep, a static heuristic selector takes over.
 *
 * Transforms that aren't available in the requested format version are skipped
 * in favor of the best-scoring supported alternative, so this graph works with
 * every supported format version.
 *
 * Inputs: One or more numeric streams of width 1, 2, 4, or 8.
 */
#define ZL_GRAPH_TRANSFORMER_NUMERIC \
    ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_transformer_numeric)

#if defined(__cplusplus)
}
#endif

#endif
