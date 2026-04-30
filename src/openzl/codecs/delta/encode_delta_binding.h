// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_DELTA_ENCODE_DELTA_BINDING_H
#define ZSTRONG_TRANSFORMS_DELTA_ENCODE_DELTA_BINDING_H

#include "openzl/codecs/common/graph_pipe.h" // PIPE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_TypedGraphDesc

ZL_BEGIN_C_DECLS

// Use and generate Integer streams
ZL_Report EI_delta_int(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_DELTA_INT(id)                \
    { .gd          = NUMPIPE_GRAPH(id), \
      .transform_f = EI_delta_int,      \
      .name        = "!zl.delta_int" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_DELTA_ENCODE_DELTA_BINDING_H
