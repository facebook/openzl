// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_COMMON_GRAPH_TOKENIZE_H
#define ZSTRONG_TRANSFORMS_COMMON_GRAPH_TOKENIZE_H

#include "openzl/zl_data.h"

/// Graph definition for the tokenize transforms
/// used by both the encoder and decoder side.

#define TOKENIZE_GRAPH(id, inType)                                \
    {                                                             \
        .CTid       = id,                                         \
        .inputTypes = ZL_STREAMTYPELIST(inType),                  \
        .soTypes    = ZL_STREAMTYPELIST(inType, ZL_Type_numeric), \
    }

#endif
