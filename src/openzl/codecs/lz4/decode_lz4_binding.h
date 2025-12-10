// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_LZ4_DECODE_LZ4_BINDING_H
#define ZSTRONG_TRANSFORMS_LZ4_DECODE_LZ4_BINDING_H

#include "openzl/zl_dtransform.h"

ZL_Report DI_lz4(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_LZ4(id)                        \
    {                                     \
        .transform_f = DI_lz4,            \
        .name        = "!zl.private.lz4", \
    }

#endif // ZSTRONG_TRANSFORMS_LZ4_DECODE_LZ4_BINDING_H
