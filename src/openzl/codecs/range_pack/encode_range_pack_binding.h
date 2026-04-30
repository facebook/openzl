// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_RANGE_PACK_ENCODE_RANGE_PACK_BINDING_H
#define ZSTRONG_TRANSFORMS_RANGE_PACK_ENCODE_RANGE_PACK_BINDING_H

#include "openzl/codecs/range_pack/graph_range_pack.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_Encoder

ZL_BEGIN_C_DECLS

ZL_Report EI_rangePack(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_RANGE_PACK(id)                  \
    { .gd          = RANGE_PACK_GRAPH(id), \
      .transform_f = EI_rangePack,         \
      .name        = "!zl.range_pack" }

ZL_END_C_DECLS

#endif
