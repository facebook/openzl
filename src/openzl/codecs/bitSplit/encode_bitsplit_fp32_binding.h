// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_FP32_BINDING_H
#define OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_FP32_BINDING_H

#include "openzl/codecs/common/graph_vo.h" /* GRAPH_VO_NUM */
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" /* ZL_Encoder */
#include "openzl/zl_errors.h"     /* ZL_Report */
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

/**
 * bitsplit_fp32 encoder
 *
 * Decomposes 32-bit IEEE 754 floats into 3 separate streams:
 * sign (1 bit), exponent (8 bits), and mantissa (23 bits).
 *
 * Input: 1 numeric stream (4-byte elements, interpreted as float32)
 * Output: 3 numeric streams (mantissa, exponent, sign)
 *
 * Parameters: none (parameter-free node)
 *
 * Wire format: reuses bitSplit transform ID and codec header.
 */
ZL_Report
EI_bitsplit_fp32(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_BITSPLIT_FP32(id)           \
    { .gd          = GRAPH_VO_NUM(id), \
      .transform_f = EI_bitsplit_fp32, \
      .name        = "!zl.bitsplit_fp32" }

ZL_END_C_DECLS

#endif // OPENZL_CODECS_BITSPLIT_ENCODE_BITSPLIT_FP32_BINDING_H
