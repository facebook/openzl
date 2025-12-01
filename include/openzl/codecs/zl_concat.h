// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_CONCAT_H
#define ZSTRONG_CODECS_CONCAT_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Concat Serial
// Input : Multiple Serial streams (this is a VI node)
// Output : 1 numeric stream, containing the size of each Input stream,
//        + 1 serial stream, concatenation of all Input Streams, preserving
//        order.
#define ZL_NODE_CONCAT_SERIAL ZL_MAKE_NODE_ID(ZL_StandardNodeID_concat_serial)

// Concat Numeric
// Input : Multiple Numeric streams (this is a VI node)
// Output : 1 numeric stream, containing the size of each Input stream,
//        + 1 numeric stream, concatenation of all Input Streams, preserving
//        order.
#define ZL_NODE_CONCAT_NUMERIC ZL_MAKE_NODE_ID(ZL_StandardNodeID_concat_num)

// Concat Struct
// Input : Multiple Struct streams (this is a VI node)
// Output : 1 numeric stream, containing the size of each Input stream,
//        + 1 struct stream, concatenation of all Input Streams, preserving
//        order.
#define ZL_NODE_CONCAT_STRUCT ZL_MAKE_NODE_ID(ZL_StandardNodeID_concat_struct)

#define ZL_NODE_CONCAT_STRING ZL_MAKE_NODE_ID(ZL_StandardNodeID_concat_string)

#if defined(__cplusplus)
}
#endif

#endif
