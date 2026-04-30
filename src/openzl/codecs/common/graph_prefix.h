// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_COMMON_GRAPH_PREFIX_H
#define ZSTRONG_TRANSFORMS_COMMON_GRAPH_PREFIX_H

#include "openzl/zl_data.h"

/// Graph definition for the prefix transform
/// used by both the encoder and decoder side

#define PREFIX_GRAPH(id)                                                  \
    {                                                                     \
        .CTid       = id,                                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_string),                  \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_string, ZL_Type_numeric), \
    }

#endif
