// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_PREFIX_H
#define ZSTRONG_CODECS_PREFIX_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Prefix
// Input: A variable-size field stream of strings with N elements
// Output 1: A variable-size field stream of strings with N elements and
//           each field is the remaining unmatched portion between consecutive
//           fields
// Output 2: A numeric stream with N elements and each field is the size
//           of the shared prefix between consecutive fields
// Example 1: "app", "apple", "apple pie", "apple pies"
//            => "app", "le", " pie", "s"
//            => 0, 3, 5, 9
// Example 2: "a", "b", "c"
//            => "a", "b", "c"
//            => 0, 0, 0
// Example 3: "a", "aa", "aaa"
//            => "a", "a", "a"
//            => 0, 1, 2
// Note: This transform specializes in compressing a stream of sorted strings.
// If the
//       stream is not sorted or there is not much overlap between the strings,
//       another transform may be more performant (see example 2)
#define ZL_NODE_PREFIX ZL_MAKE_NODE_ID(ZL_StandardNodeID_prefix)

#if defined(__cplusplus)
}
#endif

#endif
