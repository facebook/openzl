// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_DELTA_DECODE_DELTA_BINDING_H
#define ZSTRONG_TRANSFORMS_DELTA_DECODE_DELTA_BINDING_H

#include "openzl/codecs/common/graph_pipe.h" // PIPE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder

ZL_BEGIN_C_DECLS

// Ingests and generates a Numeric stream
ZL_Report DI_delta_int(ZL_Decoder* dictx, const ZL_Input* in[]);

#define DI_DELTA_INT(id) { .transform_f = DI_delta_int, .name = "delta" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_DELTA_DECODE_DELTA_BINDING_H
