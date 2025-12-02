// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_DIVIDE_BY_H
#define ZSTRONG_CODECS_DIVIDE_BY_H

#include <stdint.h>

#include "openzl/zl_nodes.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Divide By
// Input : 1 numeric stream of unsigned integers. One unsigned integer
//         parameter as the divisor.
// Output : 1 numeric stream of unsigned integers. Header information with the
// divisor used. Outcome : this transform divides each value in the input stream
// by the divisor. If 0 is
//           provided or the divisor is unavailable, instead calculates the GCD
//           and uses that as the divisor.
#define ZL_NODE_DIVIDE_BY ZL_MAKE_NODE_ID(ZL_StandardNodeID_divide_by)

/// If set, 8-byte local param key containing the divisor
/// If unset, the divisor is computed to be the GCD
#define ZL_DIVIDE_BY_PID 112

/**
 * Creates the divide by node with its divisor set to @p divisor.
 *
 * @returns Returns the modified divide by node with its divisor set to @p
 * divisor.
 */
ZL_NodeID ZL_Compressor_registerDivideByNode(
        ZL_Compressor* cgraph,
        uint64_t divisor);

#if defined(__cplusplus)
}
#endif

#endif
