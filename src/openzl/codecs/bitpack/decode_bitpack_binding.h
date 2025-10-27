// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITPACK_DECODE_BITPACK_BINDING_H
#define ZSTRONG_TRANSFORMS_BITPACK_DECODE_BITPACK_BINDING_H

#include "openzl/codecs/bitpack/graph_bitpack.h" // *BITPACK_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder

ZL_BEGIN_C_DECLS

/* new methods, based on typedTransform */
ZL_Report DI_bitpack_numeric(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_bitpack_serialized(ZL_Decoder* dictx, const ZL_Input* in[]);

// Following ZL_TypedEncoderDesc declaration,
// presumed to be used as initializer only
#define DI_BITPACK_INTEGER(id) \
    { .transform_f = DI_bitpack_numeric, .name = "bitpack" }
#define DI_BITPACK_SERIALIZED(id) \
    { .transform_f = DI_bitpack_serialized, .name = "bitpack" }

ZL_END_C_DECLS

#endif
