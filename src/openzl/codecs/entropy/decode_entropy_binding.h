// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_ENTROPY_DECODE_ENTROPY_BINDING_H
#define ZSTRONG_TRANSFORMS_ENTROPY_DECODE_ENTROPY_BINDING_H

#include "openzl/codecs/entropy/graph_entropy.h" // *ENTROPY_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder

ZL_BEGIN_C_DECLS

ZL_Report DI_fse_v2(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_huffman_v2(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_huffman_struct_v2(ZL_Decoder* dictx, const ZL_Input* in[]);

ZL_Report DI_fse_ncount(ZL_Decoder* dictx, const ZL_Input* in[]);

ZL_Report DI_fse_typed(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_huffman_serialized(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_huffman_fixed(ZL_Decoder* dictx, const ZL_Input* in[]);

#define DI_FSE_V2(id) { .transform_f = DI_fse_v2, .name = "fse v2" }

#define DI_FSE_NCOUNT(id) { .transform_f = DI_fse_ncount, .name = "fse ncount" }

#define DI_HUFFMAN_V2(id) { .transform_f = DI_huffman_v2, .name = "huffman v2" }

#define DI_HUFFMAN_STRUCT_V2(id) \
    { .transform_f = DI_huffman_struct_v2, .name = "huffman struct v2" }

// Following ZL_TypedEncoderDesc declaration,
// presumed to be used as initializer only
#define DI_FSE(id) { .transform_f = DI_fse_typed, .name = "fse" }

#define DI_HUFFMAN(id) \
    { .transform_f = DI_huffman_serialized, .name = "huffman" }

#define DI_HUFFMAN_FIXED(id) { .transform_f = DI_huffman_fixed }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_ENTROPY_DECODE_ENTROPY_BINDING_H
