// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_BF16_BINDING_H
#define OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_BF16_BINDING_H

#include "openzl/codecs/common/graph_vo.h" /* GRAPH_VO_NUM */
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" /* ZL_Encoder */
#include "openzl/zl_errors.h"     /* ZL_Report */
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

/**
 * bitsplit_bf16 encoder
 *
 * Decomposes bfloat16 values into 3 separate streams:
 * sign (1 bit), exponent (8 bits), and mantissa (7 bits).
 *
 * bfloat16 layout (16 bits):
 * - sign:     1 bit  (bit 15)
 * - exponent: 8 bits (bits 7-14)
 * - mantissa: 7 bits (bits 0-6)
 *
 * Input: 1 numeric stream (2-byte elements)
 * Output: 1 variable output stream of 3 outputs
 *         (mantissa, exponent, sign)
 *
 * Parameters: none (parameter-free node)
 *
 * Wire format: reuses bitSplit transform ID and codec header.
 */
ZL_Report
EI_bitsplit_bf16(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_BITSPLIT_BF16(id)           \
    { .gd          = GRAPH_VO_NUM(id), \
      .transform_f = EI_bitsplit_bf16, \
      .name        = "!zl.bitsplit_bf16" }

ZL_END_C_DECLS

#endif // OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_BF16_BINDING_H
