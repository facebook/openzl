// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_DECODE_PARSE_INT_BINDING_H
#define ZSTRONG_DECODE_PARSE_INT_BINDING_H

#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

ZL_Report DI_parseInt(ZL_Decoder* decoder, const ZL_Input* ins[]);

#define DI_PARSE_INT(id)                \
    {                                   \
        .transform_f = DI_parseInt,     \
        .name        = "!zl.parse_int", \
    }

ZL_END_C_DECLS

#endif
