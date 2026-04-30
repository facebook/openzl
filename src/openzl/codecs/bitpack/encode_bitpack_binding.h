// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITPACK_ENCODE_BITPACK_BINDING_H
#define ZSTRONG_TRANSFORMS_BITPACK_ENCODE_BITPACK_BINDING_H

#include "openzl/codecs/bitpack/graph_bitpack.h" // *BITPACK_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_Encoder
#include "openzl/zl_data.h"

ZL_BEGIN_C_DECLS

ZL_Report
EI_bitpack_typed(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_BITPACK_INTEGER(id)                  \
    { .gd          = INTEGER_BITPACK_GRAPH(id), \
      .transform_f = EI_bitpack_typed,          \
      .name        = "!zl.private.bitpack_int" }

#define EI_BITPACK_SERIALIZED(id)                  \
    { .gd          = SERIALIZED_BITPACK_GRAPH(id), \
      .transform_f = EI_bitpack_typed,             \
      .name        = "!zl.private.bitpack_serial" }

// Trivial redirector based on input type
// .selector_f   = SI_selector_bitpack,
// .inStreamType = ZL_Type_serial | ZL_Type_numeric,
ZL_GraphID SI_selector_bitpack(
        ZL_Selector const* selCtx,
        const ZL_Input* in,
        ZL_GraphID const* customSuccessors,
        size_t nbCustomSuccessors);

ZL_END_C_DECLS

#endif
