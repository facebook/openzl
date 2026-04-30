// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_FLOAT_DECONSTRUCT_ENCODE_FLOAT_DECONSTRUCT_BINDING_H
#define ZSTRONG_TRANSFORMS_FLOAT_DECONSTRUCT_ENCODE_FLOAT_DECONSTRUCT_BINDING_H

#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"

ZL_BEGIN_C_DECLS

ZL_Report
EI_float32_deconstruct(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);
ZL_Report
EI_bfloat16_deconstruct(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);
ZL_Report
EI_float16_deconstruct(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

// TODO(embg) Support float64

#define EI_FLOAT32_DECONSTRUCT(id)                                                     \
    { .gd          = { .CTid       = id,                                               \
                       .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric),               \
                       .soTypes    = ZL_STREAMTYPELIST(ZL_Type_struct, ZL_Type_serial) }, \
      .transform_f = EI_float32_deconstruct,                                           \
      .name        = "!zl.float32_deconstruct" }

#define EI_BFLOAT16_DECONSTRUCT(id)                                                    \
    { .gd          = { .CTid       = id,                                               \
                       .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric),               \
                       .soTypes    = ZL_STREAMTYPELIST(ZL_Type_struct, ZL_Type_serial) }, \
      .transform_f = EI_bfloat16_deconstruct,                                          \
      .name        = "!zl.bfloat16_deconstruct" }

#define EI_FLOAT16_DECONSTRUCT(id)                                                     \
    { .gd          = { .CTid       = id,                                               \
                       .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric),               \
                       .soTypes    = ZL_STREAMTYPELIST(ZL_Type_struct, ZL_Type_serial) }, \
      .transform_f = EI_float16_deconstruct,                                           \
      .name        = "!zl.float16_deconstruct" }

ZL_END_C_DECLS

#endif
