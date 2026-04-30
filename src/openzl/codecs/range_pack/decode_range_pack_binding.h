// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_RANGE_PACK_DECODE_RANGE_PACK_BINDING_H
#define ZSTRONG_TRANSFORMS_RANGE_PACK_DECODE_RANGE_PACK_BINDING_H

#include "openzl/codecs/range_pack/graph_range_pack.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder

ZL_BEGIN_C_DECLS

ZL_Report DI_rangePack(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_RANGE_PACK(id) { .transform_f = DI_rangePack, .name = "range pack" }

ZL_END_C_DECLS

#endif
