// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_ROLZ_DECODE_ROLZ_BINDING_H
#define ZSTRONG_TRANSFORMS_ROLZ_DECODE_ROLZ_BINDING_H

#include "openzl/codecs/common/graph_pipe.h" // PIPE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder

ZL_BEGIN_C_DECLS

/* new methods, based on typedTransform */
ZL_Report DI_rolz_typed(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_fastlz_typed(ZL_Decoder* dictx, const ZL_Input* in[]);

// Following ZL_TypedEncoderDesc declaration,
// presumed to be used as initializer only
#define DI_ROLZ(id) { .transform_f = DI_rolz_typed, .name = "rolz" }

#define DI_FASTLZ(id) { .transform_f = DI_fastlz_typed, .name = "fast lz" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_ROLZ_DECODE_ROLZ_BINDING_H
