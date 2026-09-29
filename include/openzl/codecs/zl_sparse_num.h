// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_ZL_SPARSE_NUM_H
#define OPENZL_CODECS_ZL_SPARSE_NUM_H

#include <stdint.h>

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Sparse Num
 *
 * Input 0: Numeric stream.
 * Output 0: Numeric zero run distances.
 * Output 1: Numeric literal values.
 *
 * `sparse_num` decomposes a numeric stream into runs of zeroes and the
 * intervening literal values. Literal values may equal zero. The distance
 * stream contains the number of zero elements before each literal, and may
 * contain one final distance to represent trailing zeroes.
 */
#define ZL_NODE_SPARSE_NUM ZL_MAKE_NODE_ID(ZL_StandardNodeID_sparse_num)

/**
 * Sparse Num Auto
 *
 * Input 0: Numeric stream.
 * Output 0: Numeric dominant-symbol run distances.
 * Output 1: Numeric literal values.
 *
 * `sparse_num_auto` decomposes a numeric stream into runs of a dominant symbol
 * and the intervening literal values. The encoder auto-detects a dominant
 * symbol from an input prefix unless one is explicitly provided through
 * ZL_SPARSE_NUM_DOMINANT_VALUE_PID. Literal values may equal the dominant
 * symbol. The distance stream contains the number of dominant-symbol elements
 * before each literal, and may contain one final distance to represent trailing
 * dominant-symbol values.
 */
#define ZL_NODE_SPARSE_NUM_AUTO \
    ZL_MAKE_NODE_ID(ZL_StandardNodeID_sparse_num_auto)

/**
 * Optional 8-byte uint64_t local parameter containing the dominant symbol for
 * ZL_NODE_SPARSE_NUM_AUTO.
 *
 * If unset, the encoder auto-detects a dominant symbol. If set to 0, the
 * encoder uses the canonical zero-dominant representation.
 */
#define ZL_SPARSE_NUM_DOMINANT_VALUE_PID 132

#if defined(__cplusplus)
}
#endif

#endif
