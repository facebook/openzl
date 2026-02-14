// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_BITSPLIT_H
#define OPENZL_CODECS_BITSPLIT_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// bitsplit transforms
// Input : 1 numeric stream (all widths supported: 1, 2, 4, 8 bytes)
// Output : 1 or 2 numeric streams (lower remainder bits, upper bits)
// Note : All variants share the same wire format (bitSplit).
//        Each variant bundles a different policy for choosing bit widths.
#define ZL_NODE_BITSPLIT_TOP8 ZL_MAKE_NODE_ID(ZL_StandardNodeID_bitsplit_top8)

#if defined(__cplusplus)
}
#endif

#endif
