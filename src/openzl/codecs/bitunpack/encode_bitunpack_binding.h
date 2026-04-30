// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITUNPACK_ENCODE_BITUNPACK_BINDING_H
#define ZSTRONG_TRANSFORMS_BITUNPACK_ENCODE_BITUNPACK_BINDING_H

#include "openzl/codecs/bitunpack/graph_bitunpack.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_Encoder

ZL_BEGIN_C_DECLS

ZL_Report EI_bitunpack(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_BITUNPACK(id)                  \
    { .gd          = BITUNPACK_GRAPH(id), \
      .transform_f = EI_bitunpack,        \
      .name        = "!zl.bitunpack" }

ZL_END_C_DECLS

#endif
