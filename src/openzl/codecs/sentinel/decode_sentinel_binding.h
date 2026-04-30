// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_SENTINEL_DECODE_SENTINEL_BINDING_H
#define OPENZL_CODECS_SENTINEL_DECODE_SENTINEL_BINDING_H

#include "openzl/codecs/sentinel/graph_sentinel.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

/// Decoder binding for the sentinel transform.
/// Reads the codec header to determine the sentinel value, then decodes
/// values + exceptions back into the original input.
ZL_Report DI_sentinel(ZL_Decoder* dictx, const ZL_Input* ins[]);

/// Macro to register the sentinel decoder transform.
#define DI_SENTINEL(id) \
    { .transform_f = DI_sentinel, .name = "!zl.sentinel_num" }

ZL_END_C_DECLS

#endif
