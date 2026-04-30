// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_ENCODE_DIVIDE_BY_BINDING_H
#define ZSTRONG_TRANSFORMS_ENCODE_DIVIDE_BY_BINDING_H

#include "openzl/codecs/common/graph_pipe.h" // PIPE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_TypedGraphDesc

ZL_BEGIN_C_DECLS

ZL_Report
EI_divide_by_int(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_DIVIDE_BY_INT(id)            \
    { .gd          = NUMPIPE_GRAPH(id), \
      .transform_f = EI_divide_by_int,  \
      .name        = "!zl.divide_by" }

// Legacy
ZL_Report EI_divide_by_int_as_typedTransform(
        ZL_Encoder* eictx,
        const ZL_Input* in);

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_ENCODE_DIVIDE_BY_BINDING_H
