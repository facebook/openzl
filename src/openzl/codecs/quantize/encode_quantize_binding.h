// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_QUANTIZE_ENCODE_QUANTIZE_BINDING_H
#define ZSTRONG_TRANSFORMS_QUANTIZE_ENCODE_QUANTIZE_BINDING_H

#include "openzl/codecs/quantize/graph_quantize.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

ZL_Report
EI_quantizeOffsets(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);
ZL_Report
EI_quantizeLengths(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_QUANTIZE_OFFSETS(id)          \
    { .gd          = QUANTIZE_GRAPH(id), \
      .transform_f = EI_quantizeOffsets, \
      .name        = "!zl.quantize_offsets" }

#define EI_QUANTIZE_LENGTHS(id)          \
    { .gd          = QUANTIZE_GRAPH(id), \
      .transform_f = EI_quantizeLengths, \
      .name        = "!zl.quantize_lengths" }

ZL_END_C_DECLS

#endif
