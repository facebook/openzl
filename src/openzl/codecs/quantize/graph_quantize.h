// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_QUANTIZE_GRAPH_QUANTIZE_H
#define ZSTRONG_TRANSFORMS_QUANTIZE_GRAPH_QUANTIZE_H

#include "openzl/zl_data.h"

/// Graph definition for the quantize transforms
/// used by both the encoder and decoder side.

#define QUANTIZE_GRAPH(id)                                                \
    {                                                                     \
        .CTid       = id,                                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric),                 \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_serial), \
    }

#endif
