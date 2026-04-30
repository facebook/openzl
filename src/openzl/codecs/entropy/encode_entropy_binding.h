// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_ENTROPY_ENCODE_ENTROPY_BINDING_H
#define ZSTRONG_TRANSFORMS_ENTROPY_ENCODE_ENTROPY_BINDING_H

#include "openzl/codecs/entropy/graph_entropy.h" // *ENTROPY_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_Encoder

ZL_BEGIN_C_DECLS

ZL_Report EI_fse_v2(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);
ZL_Report EI_huffman_v2(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);
ZL_Report
EI_huffman_struct_v2(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

ZL_Report EI_fse_ncount(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

ZL_Report EI_fse_typed(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);
ZL_Report
EI_huffman_typed(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

ZL_Report EI_fseDynamicGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns);
ZL_Report
EI_huffmanDynamicGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns);
ZL_Report
EI_entropyDynamicGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns);

#define EI_FSE_V2(id)                  \
    { .gd          = FSE_V2_GRAPH(id), \
      .transform_f = EI_fse_v2,        \
      .name        = "!zl.private.fse_v2" }

#define EI_FSE_NCOUNT(id)                  \
    { .gd          = FSE_NCOUNT_GRAPH(id), \
      .transform_f = EI_fse_ncount,        \
      .name        = "!zl.private.fse_ncount" }

#define EI_HUFFMAN_V2(id)                  \
    { .gd          = HUFFMAN_V2_GRAPH(id), \
      .transform_f = EI_huffman_v2,        \
      .name        = "!zl.private.huffman_v2" }

#define EI_HUFFMAN_STRUCT_V2(id)                  \
    { .gd          = HUFFMAN_STRUCT_V2_GRAPH(id), \
      .transform_f = EI_huffman_struct_v2,        \
      .name        = "!zl.private.huffman_struct_v2" }

// Following ZL_TypedEncoderDesc declaration,
// presumed to be used as initializer only
#define EI_FSE(id)                                 \
    { .gd          = SERIALIZED_ENTROPY_GRAPH(id), \
      .transform_f = EI_fse_typed,                 \
      .name        = "!zl.private.fse_deprecated" }

#define EI_HUFFMAN(id)                             \
    { .gd          = SERIALIZED_ENTROPY_GRAPH(id), \
      .transform_f = EI_huffman_typed,             \
      .name        = "!zl.private.huffman_deprecated" }

#define EI_HUFFMAN_FIXED(id)                  \
    { .gd          = FIXED_ENTROPY_GRAPH(id), \
      .transform_f = EI_huffman_typed,        \
      .name        = "!zl.private.huffman_fixed_deprecated" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_ENTROPY_ENCODE_ENTROPY_BINDING_H
