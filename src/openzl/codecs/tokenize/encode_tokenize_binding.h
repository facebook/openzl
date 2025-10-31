// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_TOKENIZE_ENCODE_TOKENIZE_BINDING_H
#define ZSTRONG_TRANSFORMS_TOKENIZE_ENCODE_TOKENIZE_BINDING_H

#include "openzl/codecs/common/graph_tokenize.h" // TOKENIZE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_Encoder

ZL_BEGIN_C_DECLS

ZL_Report EI_tokenize(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);
ZL_Report
EI_tokenizeVSF(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

ZL_NodeID ZS2_createNode_customTokenize(
        ZL_Compressor* cgraph,
        ZL_Type streamType,
        ZL_CustomTokenizeFn customTokenizeFn,
        void const* opaque);

#define EI_TOKENIZE_STRUCT(id)                           \
    { .gd          = TOKENIZE_GRAPH(id, ZL_Type_struct), \
      .transform_f = EI_tokenize,                        \
      .name        = "!zl.tokenize_struct" }

#define EI_TOKENIZE_NUMERIC(id)                           \
    { .gd          = TOKENIZE_GRAPH(id, ZL_Type_numeric), \
      .transform_f = EI_tokenize,                         \
      .name        = "!zl.tokenize_numeric" }

#define EI_TOKENIZE_STRING(id)                           \
    { .gd          = TOKENIZE_GRAPH(id, ZL_Type_string), \
      .transform_f = EI_tokenizeVSF,                     \
      .name        = "!zl.tokenize_string" }

#define EI_TOKENIZE_SORTED(id)                                 \
    { .gd          = TOKENIZE_GRAPH(id, ZL_Type_numeric),      \
      .transform_f = EI_tokenize,                              \
      .localParams = ZL_LP_1INTPARAM(ZL_TOKENIZE_SORT_PID, 1), \
      .name        = "!zl.private.tokenize_sorted" }

#define EI_TOKENIZE_VSF_SORTED(id)                             \
    { .gd          = TOKENIZE_GRAPH(id, ZL_Type_string),       \
      .transform_f = EI_tokenizeVSF,                           \
      .localParams = ZL_LP_1INTPARAM(ZL_TOKENIZE_SORT_PID, 1), \
      .name        = "!zl.private.tokenize_string_sorted" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_TOKENIZE_ENCODE_TOKENIZE_BINDING_H
