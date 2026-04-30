// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_DELTA_H
#define ZSTRONG_CODECS_DELTA_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Delta
// Input : 1 numeric stream (all width supported)
// Output : 1 numeric stream (same width as input)
// Result : this transform stores the first value "raw",
//          and then each value is a delta from previous value.
//          Negative values are stored using 2-complement convention.
#define ZL_NODE_DELTA_INT ZL_MAKE_NODE_ID(ZL_StandardNodeID_delta_int)

#if defined(__cplusplus)
}
#endif

#endif
