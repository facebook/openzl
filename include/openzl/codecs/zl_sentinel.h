// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_ZL_SENTINEL_H
#define OPENZL_CODECS_ZL_SENTINEL_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Sentinel-byte node.
 *
 * Encodes each numeric input element into a 1-byte values stream and a
 * full-width exceptions stream. Values < 255 are stored directly as a
 * single byte; values >= 255 are replaced by the sentinel 255 in the values
 * stream and stored at the original width in the exceptions stream.
 *
 * Input:    1 numeric stream (width >= 2)
 * Output 0: numeric stream of 1-byte values (sentinel or narrowed value)
 * Output 1: numeric stream of exceptions at the original input width
 */
#define ZL_NODE_SENTINEL_BYTE ZL_MAKE_NODE_ID(ZL_StandardNodeID_sentinel_byte)

/**
 * General sentinel node.
 *
 * Encodes a numeric input by writing a sentinel marker at designated
 * exception positions and moving the original values to an exceptions
 * stream. The values stream retains the same element width as the input.
 *
 * This node is intended to be invoked within a function graph via
 * ZL_Edge_runSentinelNode() or ZL_Edge_runNode_withParams().
 *
 * Local parameters:
 *   ZL_SENTINEL_INDICES_PID (ref): sorted array of size_t exception indices
 *   ZL_SENTINEL_VALUE_PID   (ref): pointer to uint64_t sentinel value
 *                                  (optional, defaults to max for elem width)
 *
 * Input:    1 numeric stream
 * Output 0: numeric stream of values (same width, sentinel at exception pos)
 * Output 1: numeric stream of exceptions (same width as input)
 */
#define ZL_NODE_SENTINEL_NUM ZL_MAKE_NODE_ID(ZL_StandardNodeID_sentinel_num)

/// Ref/Copy param ID for the sorted array of exception indices (size_t[]).
#define ZL_SENTINEL_INDICES_PID 130

/// Ref/Copy param ID for the sentinel value (uint64_t) which will be masked to
/// the element width.
/// Optional: defaults to max value for the element width if absent.
#define ZL_SENTINEL_VALUE_PID 131

/**
 * Run the general sentinel node on @p input within a function graph.
 *
 * Convenience wrapper around ZL_Edge_runNode_withParams() that packages
 * exception indices and sentinel value as local params.
 *
 * @param input            The input edge to process
 * @param exceptionIndices Sorted array of indices to move to exceptions
 * @param numExceptions    Number of exception indices
 * @param sentinel         The sentinel value to use
 * @returns An EdgeList with 2 edges: [0] = values, [1] = exceptions
 */
ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runSentinelNode(
        ZL_Edge* input,
        const size_t* exceptionIndices,
        size_t numExceptions,
        uint64_t sentinel);

#if defined(__cplusplus)
}
#endif

#endif
