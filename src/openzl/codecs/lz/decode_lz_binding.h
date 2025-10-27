// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_LZ_DECODE_LZ_BINDING_H
#define ZSTRONG_TRANSFORMS_LZ_DECODE_LZ_BINDING_H

#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

ZL_Report DI_fieldLz(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_FIELD_LZ(id) { .transform_f = DI_fieldLz, .name = "field lz" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_LZ_DECODE_LZ_BINDING_H
