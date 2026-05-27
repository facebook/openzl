// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_ZL_SPARSE_NUM_H
#define OPENZL_CODECS_ZL_SPARSE_NUM_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Sparse Num
 *
 * Input 0: Numeric stream.
 * Output 0: Numeric zero-run distances.
 * Output 1: Numeric literal values.
 *
 * `sparse_num` decomposes a numeric stream into runs of zero elements and the
 * intervening literal values. Literal values may be zero. The distance stream
 * contains the number of zero elements before each literal, and may contain one
 * final distance to represent trailing zeros.
 */
#define ZL_NODE_SPARSE_NUM ZL_MAKE_NODE_ID(ZL_StandardNodeID_sparse_num)

#if defined(__cplusplus)
}
#endif

#endif
