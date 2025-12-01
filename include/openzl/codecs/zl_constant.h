// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_CONSTANT_H
#define ZSTRONG_CODECS_CONSTANT_H

#include "openzl/zl_graphs.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Constant
// Input : 1 stream of N repetitions of a single value
// Output : 1 stream containing a single instance of the repeated value
// Result : transforms a stream consisting of a single value (possibly repeated
// multiple times)
//          into a stream consisting of that single value
// Note : compression will fail if the stream is empty or isn't constant
// Example 1 : 1 1 1 1 1 as 5 fields of size 1 => 1 as 1 field of size 1
// Example 2 : 300 300 300 as 3 fields of size 3 => 300 as 1 field of size 3
#define ZL_GRAPH_CONSTANT ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_constant)

#if defined(__cplusplus)
}
#endif

#endif
