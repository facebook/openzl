// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_TOP8BITS_H
#define OPENZL_CODECS_TOP8BITS_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// top8bits
// Input : 1 numeric stream (all widths supported: 1, 2, 4, 8 bytes)
// Output : 1 or 2 numeric streams
// Result : Scans for maximum value to determine effective bit width,
//          then splits into the top 8 significant bits and the remainder.
//          If effective width <= 8: produces 1 output stream (8-bit).
//          If effective width > 8:  produces 2 output streams
//            (lower remainder bits, upper 8 bits).
// Note : Parameter-free. Uses the same wire format as bitSplit.
//        All-zero input produces a single 8-bit stream of zeros.
#define ZL_NODE_TOP8BITS ZL_MAKE_NODE_ID(ZL_StandardNodeID_top8bits)

#if defined(__cplusplus)
}
#endif

#endif
