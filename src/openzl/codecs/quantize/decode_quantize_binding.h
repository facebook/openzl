// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_QUANTIZE_DECODE_QUANTIZE_BINDING_H
#define ZSTRONG_TRANSFORMS_QUANTIZE_DECODE_QUANTIZE_BINDING_H

#include "openzl/codecs/quantize/graph_quantize.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_Report DI_quantizeOffsets(ZL_Decoder* dictx, const ZL_Input* ins[]);
ZL_Report DI_quantizeLengths(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_QUANTIZE_OFFSETS(id) \
    { .transform_f = DI_quantizeOffsets, .name = "quantize offsets" }

#define DI_QUANTIZE_LENGTHS(id) \
    { .transform_f = DI_quantizeLengths, .name = "quantize lengths" }

#endif // ZSTRONG_TRANSFORMS_QUANTIZE_DECODE_QUANTIZE_BINDING_H
