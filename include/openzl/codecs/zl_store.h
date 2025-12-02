// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_STORE_H
#define ZSTRONG_CODECS_STORE_H

#include "openzl/zl_graphs.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Storage operation. Final stage of any stored stream.
#define ZL_GRAPH_STORE ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_store)

#if defined(__cplusplus)
}
#endif

#endif
