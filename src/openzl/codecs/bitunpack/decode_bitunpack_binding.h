// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITUNPACK_DECODE_BITUNPACK_BINDING_H
#define ZSTRONG_TRANSFORMS_BITUNPACK_DECODE_BITUNPACK_BINDING_H

#include "openzl/codecs/bitunpack/graph_bitunpack.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder

ZL_BEGIN_C_DECLS

ZL_Report DI_bitunpack(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_BITUNPACK(id) { .transform_f = DI_bitunpack, .name = "bitunpack" }

ZL_END_C_DECLS

#endif
