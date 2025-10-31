// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_FLATPACK_ENCODE_FLATPACK_BINDING_H
#define ZSTRONG_TRANSFORMS_FLATPACK_ENCODE_FLATPACK_BINDING_H

#include "openzl/codecs/flatpack/graph_flatpack.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

ZL_Report EI_flatpack(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_FLATPACK(id)                  \
    { .gd          = FLATPACK_GRAPH(id), \
      .transform_f = EI_flatpack,        \
      .name        = "!zl.private.flatpack" }

ZL_END_C_DECLS

#endif
