// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_FP_BINDING_H
#define OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_FP_BINDING_H

#include "openzl/codecs/common/graph_vo.h" /* GRAPH_VO_NUM */
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" /* ZL_Encoder */
#include "openzl/zl_errors.h"     /* ZL_Report */
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

/**
 * bitsplit_fp encoder
 *
 * Decomposes IEEE 754 floats into 3 separate streams:
 * sign (1 bit), exponent, and mantissa.
 *
 * Supported IEEE 754 widths:
 * - 2-byte (16-bit): sign (1 bit), exponent (5 bits), mantissa (10 bits)
 * - 4-byte (32-bit): sign (1 bit), exponent (8 bits), mantissa (23 bits)
 * - 8-byte (64-bit): sign (1 bit), exponent (11 bits), mantissa (52 bits)
 *
 * Input: 1 numeric stream (2, 4, or 8-byte elements)
 * Output: 1 variable output stream of 3 outputs
 *         (sign, exponent, mantissa)
 *
 * Parameters: none (parameter-free node)
 *
 * Wire format: reuses bitSplit transform ID and codec header.
 */
ZL_Report
EI_bitsplit_fp(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_BITSPLIT_FP(id)             \
    { .gd          = GRAPH_VO_NUM(id), \
      .transform_f = EI_bitsplit_fp,   \
      .name        = "!zl.bitsplit_fp" }

ZL_END_C_DECLS

#endif // OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_FP_BINDING_H
