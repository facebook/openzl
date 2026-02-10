// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_BINDING_H
#define OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_BINDING_H

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

/**
 * Shared bitSplit encode logic used by both EI_bitSplit and EI_top8bits.
 *
 * Validates parameters, checks top bits, builds codec header,
 * creates output streams, runs the kernel, and commits outputs.
 *
 * @param eictx Encoder context
 * @param in Input stream (must be numeric)
 * @param bitWidths Array of bit widths (LSB to MSB order, 1 byte each)
 * @param nbWidths Number of bit widths (must be > 0, <= 64)
 * @return ZL_Report with number of outputs on success
 */
ZL_Report EI_bitSplit_withWidths(
        ZL_Encoder* eictx,
        const ZL_Input* in,
        const uint8_t* bitWidths,
        size_t nbWidths);

#define EI_BITSPLIT(id)                \
    { .gd          = GRAPH_VO_NUM(id), \
      .transform_f = EI_bitSplit,      \
      .name        = "!zl.private.bit_split" }

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
        ZL_Compressor* compressor,
        const uint8_t* bitWidths,
        size_t nbWidths);

/**
 * Same as ZL_Compressor_registerBitSplitNode(),
 * but @returns a ZL_RESULT_OF(ZL_NodeID) error type,
 * which can transport more information when there is an error.
 */
ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_buildBitSplitNode(
        ZL_Compressor* compressor,
        const uint8_t* bitWidths,
        size_t nbWidths);

ZL_END_C_DECLS

#endif // OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_BINDING_H
