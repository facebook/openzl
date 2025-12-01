// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_ZIGZAG_H
#define ZSTRONG_CODECS_ZIGZAG_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Delta
// Input : 1 numeric stream, values are interpreted as Signed Integer,
//         using 2-complement convention for negative values.
// Output : 1 numeric stream (same width as input),
//          generated values are considered Unsigned Integers.
// Outcome : this transform convert a distribution of signed values centered
// around `0`
//           into a serie of purely numbers,
//           with expected decreasing distribution starting from 0.
// Note : It's unclear if this transform is really useful.
//        If not, it will be removed from Standard Transforms.
#define ZL_NODE_ZIGZAG ZL_MAKE_NODE_ID(ZL_StandardNodeID_zigzag)

#if defined(__cplusplus)
}
#endif

#endif
