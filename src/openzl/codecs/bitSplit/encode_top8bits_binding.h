// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_BITSPLIT_ENCODE_TOP8BITS_BINDING_H
#define OPENZL_CODECS_BITSPLIT_ENCODE_TOP8BITS_BINDING_H

#include "openzl/codecs/common/graph_vo.h" /* GRAPH_VO_NUM */
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" /* ZL_Encoder */
#include "openzl/zl_errors.h"     /* ZL_Report */
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

/**
 * top8bits encoder
 *
 * Scans input for max value, determines effective bit width, and splits
 * the field into at most 2 streams: the top 8 significant bits and the
 * remainder lower bits.
 *
 * Input: 1 numeric stream (all widths supported: 1, 2, 4, 8 bytes)
 * Output: 1 or 2 numeric streams
 *   - effective width <= 8: 1 stream (8-bit)
 *   - effective width > 8:  2 streams (remainder bits, top 8 bits)
 *
 * Parameters: none (parameter-free node)
 *
 * Wire format: reuses bitSplit transform ID and codec header.
 */
ZL_Report EI_top8bits(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_TOP8BITS(id)                \
    { .gd          = GRAPH_VO_NUM(id), \
      .transform_f = EI_top8bits,      \
      .name        = "!zl.top8bits" }

ZL_END_C_DECLS

#endif // OPENZL_CODECS_BITSPLIT_ENCODE_TOP8BITS_BINDING_H
