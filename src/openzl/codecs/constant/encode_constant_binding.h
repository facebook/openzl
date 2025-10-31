// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_CONSTANT_ENCODE_CONSTANT_BINDING_H
#define ZSTRONG_TRANSFORMS_CONSTANT_ENCODE_CONSTANT_BINDING_H

#include "openzl/codecs/common/graph_constant.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_selector.h"

ZL_BEGIN_C_DECLS

ZL_Report
EI_constant_typed(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_CONSTANT_SERIALIZED(id)                  \
    { .gd          = SERIALIZED_CONSTANT_GRAPH(id), \
      .transform_f = EI_constant_typed,             \
      .name        = "!zl.private.constant_serial" }

#define EI_CONSTANT_FIXED(id)                       \
    { .gd          = FIXED_SIZE_CONSTANT_GRAPH(id), \
      .transform_f = EI_constant_typed,             \
      .name        = "!zl.private.constant_fixed" }

ZL_INLINE bool ZL_Graph_isConstantSupported(const ZL_Graph* graph)
{
    return ZL_Graph_getCParam(graph, ZL_CParam_formatVersion) >= 11;
}

ZL_INLINE bool ZL_Selector_isConstantSupported(const ZL_Selector* selector)
{
    return ZL_Selector_getCParam(selector, ZL_CParam_formatVersion) >= 11;
}

ZL_END_C_DECLS

#endif
