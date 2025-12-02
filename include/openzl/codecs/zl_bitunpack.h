// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_
#define ZSTRONG_CODECS_

#include <stddef.h>

#include "openzl/zl_localParams.h"
#include "openzl/zl_nodes.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Converts a Serial stream of packed integers into a Numeric stream containing
// the packed integers. A local parameter `ZL_Bitunpack_numBits` specifying the
// number of bits must be supplied. For convenience prefer to use
// `ZL_CREATENODE_BITUNPACK` which would create a new parameterized node with
// the given nbBits in your graph. The node will try and unpack as many nbBits
// items from the source stream and expects the source stream to have no
// leftover bytes. Additionally, all leftover bits (in the last byte) must be
// zeroes. Both of these invariants are checked and compression would fail if
// the checks fail.
enum { ZL_Bitunpack_numBits = 1 };
#define ZS2_NODE_BITUNPACK ZL_MAKE_NODE_ID(ZL_StandardNodeID_bitunpack)
#define ZL_CREATENODE_BITUNPACK(graph, nbBits)                              \
    ZL_Compressor_cloneNode(                                                \
            graph,                                                          \
            ZS2_NODE_BITUNPACK,                                             \
            &(const ZL_LocalParams){                                        \
                    { &(const ZL_IntParam){ ZL_Bitunpack_numBits, nbBits }, \
                      1 },                                                  \
                    { NULL, 0 },                                            \
                    { NULL, 0 } })
ZL_NodeID ZL_Compressor_registerBitunpackNode(
        ZL_Compressor* cgraph,
        int nbBits);

#if defined(__cplusplus)
}
#endif

#endif
