// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_TOKENIZE_DECODE_TOKENIZE_BINDING_H
#define ZSTRONG_TRANSFORMS_TOKENIZE_DECODE_TOKENIZE_BINDING_H

#include "openzl/codecs/common/graph_tokenize.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

ZL_Report DI_tokenize(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_tokenizeVSF(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define TOKENIZE_FIXED_GRAPH(id) TOKENIZE_GRAPH(id, ZL_Type_struct)

#define DI_TOKENIZE_FIXED(id) { .transform_f = DI_tokenize, .name = "tokenize" }

#define TOKENIZE_NUMERIC_GRAPH(id) TOKENIZE_GRAPH(id, ZL_Type_numeric)

#define DI_TOKENIZE_NUMERIC(id) \
    { .transform_f = DI_tokenize, .name = "tokenize_numeric" }

#define TOKENIZE_VSF_GRAPH(id) TOKENIZE_GRAPH(id, ZL_Type_string)

#define DI_TOKENIZE_VSF(id) \
    { .transform_f = DI_tokenizeVSF, .name = "tokenize vsf" }

ZL_END_C_DECLS

#endif
