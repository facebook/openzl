// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_RANGE_PACK_H
#define ZSTRONG_CODECS_RANGE_PACK_H

#include "openzl/zl_graphs.h"
#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Substracts the minimal (unsigned) element from all other elements in the
// stream and outputs the minimal sized numeric stream type that can contain the
// 0-based range of values.
#define ZL_NODE_RANGE_PACK ZL_MAKE_NODE_ID(ZL_StandardNodeID_range_pack)

#if defined(__cplusplus)
}
#endif

#endif
