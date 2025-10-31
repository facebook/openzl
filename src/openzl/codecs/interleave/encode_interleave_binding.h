// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_INTERLEAVE_ENCODE_INTERLEAVE_BINDING_H
#define OPENZL_CODECS_INTERLEAVE_ENCODE_INTERLEAVE_BINDING_H

#include "openzl/codecs/interleave/graph_interleave.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"

ZL_BEGIN_C_DECLS

// Interleave
// Input: Variable number of inputs with the same type and number of elements
// Output: 1 output with the same type consisting of the inputs
// interleaved in round-robin order
ZL_Report EI_interleave(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_INTERLEAVE_STRING(id)                  \
    { .gd          = INTERLEAVE_STRING_GRAPH(id), \
      .transform_f = EI_interleave,               \
      .name        = "!zl.interleave_string" }

ZL_END_C_DECLS

#endif
