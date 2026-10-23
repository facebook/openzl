// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_RANGE_PACK_H
#define ZSTRONG_CODECS_RANGE_PACK_H

#include <stdint.h>

#include "openzl/zl_graphs.h"
#include "openzl/zl_nodes.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Substracts the minimal (unsigned) element from all other elements in the
// stream and outputs the minimal sized numeric stream type that can contain the
// 0-based range of values.
#define ZL_NODE_RANGE_PACK ZL_MAKE_NODE_ID(ZL_StandardNodeID_range_pack)

/// If set, 8-byte local param holding an upper bound of the subtracted value:
/// range_pack then subtracts the smaller of this bound and the minimal element.
#define ZL_RANGE_PACK_MAX_SUBTRACTED_PID 113

/**
 * Creates a range_pack node that subtracts at most @p maxSubtracted. With 0,
 * the node only narrows the values to the smallest width that holds them.
 */
ZL_NodeID ZL_Compressor_registerRangePackNode(
        ZL_Compressor* compressor,
        uint64_t maxSubtracted);

#if defined(__cplusplus)
}
#endif

#endif
