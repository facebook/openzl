// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_BITPACK_H
#define ZSTRONG_CODECS_BITPACK_H

#include "openzl/zl_graphs.h"

#if defined(__cplusplus)
extern "C" {
#endif

// These Graphs essentially call their namesake Node and then STORE the result
// into the frame
#define ZL_GRAPH_BITPACK ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_bitpack)

#if defined(__cplusplus)
}
#endif

#endif
