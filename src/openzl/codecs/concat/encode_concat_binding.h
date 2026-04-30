// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_CONCAT_ENCODE_CONCAT_BINDING_H
#define ZSTRONG_TRANSFORMS_CONCAT_ENCODE_CONCAT_BINDING_H

#include "openzl/codecs/concat/graph_concat.h" // CONCAT_SERIAL_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

ZL_Report EI_concat(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_CONCAT_SERIAL(id)                  \
    { .gd          = CONCAT_SERIAL_GRAPH(id), \
      .transform_f = EI_concat,               \
      .name        = "!zl.concat_serial" }

#define EI_CONCAT_NUM(id)                  \
    { .gd          = CONCAT_NUM_GRAPH(id), \
      .transform_f = EI_concat,            \
      .name        = "!zl.concat_num" }

#define EI_CONCAT_STRUCT(id)                  \
    { .gd          = CONCAT_STRUCT_GRAPH(id), \
      .transform_f = EI_concat,               \
      .name        = "!zl.concat_struct" }

#define EI_CONCAT_STRING(id)                  \
    { .gd          = CONCAT_STRING_GRAPH(id), \
      .transform_f = EI_concat,               \
      .name        = "!zl.concat_string" }

ZL_END_C_DECLS

#endif
