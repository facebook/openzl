// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_FLOAT_DECONSTRUCT_DECODE_FLOAT_DECONSTRUCT_BINDING_H
#define ZSTRONG_TRANSFORMS_FLOAT_DECONSTRUCT_DECODE_FLOAT_DECONSTRUCT_BINDING_H

#include "openzl/shared/portability.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"

ZL_Report DI_float_deconstruct(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define FLOAT_DECONSTRUCT_GRAPH(id)                           \
    { .CTid       = ZL_StandardTransformID_float_deconstruct, \
      .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric),       \
      .soTypes    = ZL_STREAMTYPELIST(ZL_Type_struct, ZL_Type_serial) }

#define DI_FLOAT_DECONSTRUCT(id) \
    { .transform_f = DI_float_deconstruct, .name = "float deconstruct" }

#endif
