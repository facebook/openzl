// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_SENTINEL_ENCODE_SENTINEL_BINDING_H
#define OPENZL_CODECS_SENTINEL_ENCODE_SENTINEL_BINDING_H

#include "openzl/codecs/sentinel/graph_sentinel.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

/// Encoder binding for sentinel_byte: narrows values < 255 to 1 byte,
/// exceptions (>= 255) stored at original width. Sentinel = 255.
ZL_Report
EI_sentinel_byte(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

/// Encoder binding for general sentinel: exception indices and sentinel value
/// are read from local params.
ZL_Report EI_sentinel(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

/// Macro to register the sentinel_byte encoder transform.
#define EI_SENTINEL_BYTE(id)             \
    { .gd          = SENTINEL_GRAPH(id), \
      .transform_f = EI_sentinel_byte,   \
      .name        = "!zl.sentinel_byte" }

/// Macro to register the general sentinel encoder transform.
#define EI_SENTINEL(id)                  \
    { .gd          = SENTINEL_GRAPH(id), \
      .transform_f = EI_sentinel,        \
      .name        = "!zl.sentinel_num" }

ZL_END_C_DECLS

#endif
