// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_PREFIX_DECODE_PREFIX_BINDING_H
#define ZSTRONG_TRANSFORMS_PREFIX_DECODE_PREFIX_BINDING_H

#include "openzl/codecs/common/graph_prefix.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

ZL_Report DI_prefix(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_PREFIX(id) { .transform_f = DI_prefix, .name = "prefix" }

ZL_END_C_DECLS

#endif
