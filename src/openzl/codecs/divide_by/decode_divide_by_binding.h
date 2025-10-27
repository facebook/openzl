// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_DECODE_DIVIDE_BY_BINDING_H
#define ZSTRONG_TRANSFORMS_DECODE_DIVIDE_BY_BINDING_H

#include "openzl/codecs/common/graph_pipe.h" // PIPE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder

ZL_BEGIN_C_DECLS

// Use and generate Integer streams
ZL_Report DI_divide_by_int(ZL_Decoder* dictx, const ZL_Input* in[]);

#define DI_DIVIDE_BY_INT(id) \
    { .transform_f = DI_divide_by_int, .name = "divide by" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_DECODE_DIVIDE_BY_BINDING_H
