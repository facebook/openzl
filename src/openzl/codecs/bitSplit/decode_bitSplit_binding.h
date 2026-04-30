// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_BITSPLIT_DECODE_BITSPLIT_BINDING_H
#define OPENZL_CODECS_BITSPLIT_DECODE_BITSPLIT_BINDING_H

#include "openzl/codecs/bitSplit/graph_bitSplit.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

/**
 * bitSplit decoder (MI - Multiple Input)
 *
 * Reconstructs the original numeric stream from multiple bit-range streams.
 *
 * Input: N numeric streams (one per bit range)
 * Output: 1 numeric stream (reconstructed original values)
 *
 * Parameters (via codec header):
 *   Array of bit widths [w₀, w₁, ..., wₙ₋₁] (1 byte each, LSB to MSB order)
 *   count = header_size (implicit)
 *
 * Output element width determined by sum of bit widths:
 *   1-8 bits → u8, 9-16 bits → u16, 17-32 bits → u32, 33-64 bits → u64
 */
ZL_Report DI_bitSplit(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs);

#define DI_BITSPLIT(id) \
    { .transform_f = DI_bitSplit, .name = "!zl.private.bit_split" }

ZL_END_C_DECLS

#endif // OPENZL_CODECS_BITSPLIT_DECODE_BITSPLIT_BINDING_H
