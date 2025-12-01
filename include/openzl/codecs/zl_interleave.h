// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_PARSE_INT_H
#define OPENZL_CODECS_PARSE_INT_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Interleave
// Input: Variable number of stream with the same type and number of elements
// Output: 1 stream with the same type consisting of the input streams
// interleaved in round-robin order
#define ZL_NODE_INTERLEAVE_STRING \
    ZL_MAKE_NODE_ID(ZL_StandardNodeID_interleave_string)

#if defined(__cplusplus)
}
#endif
#endif // OPENZL_CODECS_PARSE_INT_H
