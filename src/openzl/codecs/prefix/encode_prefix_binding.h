// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_PREFIX_ENCODE_PREFIX_BINDING_H
#define ZSTRONG_TRANSFORMS_PREFIX_ENCODE_PREFIX_BINDING_H

#include "openzl/codecs/common/graph_prefix.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

ZL_Report EI_prefix(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_PREFIX(id) \
    { .gd = PREFIX_GRAPH(id), .transform_f = EI_prefix, .name = "!zl.prefix" }

ZL_END_C_DECLS

#endif
