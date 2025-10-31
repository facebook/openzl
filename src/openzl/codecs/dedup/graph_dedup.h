// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_CONCAT_DEDUP_H
#define ZSTRONG_TRANSFORMS_CONCAT_DEDUP_H

#include "openzl/zl_data.h" // ZS2_Type_*

#define DEDUP_NUM_GRAPH(id)                                        \
    {                                                              \
        .CTid                = id,                                 \
        .inputTypes          = ZL_STREAMTYPELIST(ZL_Type_numeric), \
        .lastInputIsVariable = 1,                                  \
        .soTypes             = ZL_STREAMTYPELIST(ZL_Type_numeric), \
    }

#endif
