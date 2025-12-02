// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_DEDUP_H
#define ZSTRONG_CODECS_DEDUP_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Deduplicate Numeric Streams - Dedup_Num
// Input : Multiple Numeric streams (this is a VI node)
// Condition: All input streams must be exactly identical
// Output : 1 numeric stream, one copy of Input Streams
#define ZL_NODE_DEDUP_NUMERIC ZL_MAKE_NODE_ID(ZL_StandardNodeID_dedup_num)

#if defined(__cplusplus)
}
#endif

#endif
