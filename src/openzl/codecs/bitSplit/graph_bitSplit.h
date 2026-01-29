// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITSPLIT_GRAPH_BITSPLIT_H
#define ZSTRONG_TRANSFORMS_BITSPLIT_GRAPH_BITSPLIT_H

#include "openzl/zl_data.h"

// bitSplit decoder graph: N numeric inputs → 1 numeric output
// The number of inputs is variable (determined by codec header)
#define BITSPLIT_GRAPH(id)                                         \
    {                                                              \
        .CTid                = id,                                 \
        .inputTypes          = ZL_STREAMTYPELIST(ZL_Type_numeric), \
        .lastInputIsVariable = 1,                                  \
        .soTypes             = ZL_STREAMTYPELIST(ZL_Type_numeric), \
    }

#endif // ZSTRONG_TRANSFORMS_BITSPLIT_GRAPH_BITSPLIT_H
