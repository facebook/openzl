// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITSPLIT_ENCODE_BITSPLIT_BINDING_H
#define ZSTRONG_TRANSFORMS_BITSPLIT_ENCODE_BITSPLIT_BINDING_H

#include "openzl/codecs/common/graph_vo.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

/**
 * bitSplit encoder
 *
 * Splits a numeric stream by bit ranges into multiple numeric output streams.
 *
 * Input: 1 numeric stream (all widths supported: 1, 2, 4, 8 bytes)
 * Output: N numeric streams (one per bit range specified in parameters)
 *
 * Parameters (via local params, use registration function below):
 *   Array of bit widths [w₀, w₁, ..., wₙ₋₁] (1 byte each, LSB to MSB order)
 *
 * Output element widths determined by bit range:
 *   1-8 bits → u8, 9-16 bits → u16, 17-32 bits → u32, 33-64 bits → u64
 *
 * Errors:
 *   - Empty parameters (no widths)
 *   - Any width == 0
 *   - sum(widths) > input element width in bits
 *   - Top bits non-zero when partial coverage
 */
ZL_Report EI_bitSplit(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_BITSPLIT(id)                \
    { .gd          = GRAPH_VO_NUM(id), \
      .transform_f = EI_bitSplit,      \
      .name        = "!zl.private.bitSplit" }

/**
 * Register a bitSplit node with specified bit widths.
 *
 * @param cgraph The compressor graph to register with
 * @param bitWidths Array of bit widths (LSB to MSB order, 1 byte each)
 * @param nbWidths Number of bit widths (must be > 0)
 * @return Node ID for the configured bitSplit transform, or ZL_NODE_ILLEGAL on
 * error
 *
 * Example:
 *   uint8_t widths[] = {4, 8, 12, 8};  // Split 32-bit into 4 ranges
 *   ZL_NodeID node = ZL_Compressor_registerBitSplitNode(cgraph, widths, 4);
 */
ZL_NodeID ZL_Compressor_registerBitSplitNode(
        ZL_Compressor* cgraph,
        const uint8_t* bitWidths,
        size_t nbWidths);

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_BITSPLIT_ENCODE_BITSPLIT_BINDING_H
