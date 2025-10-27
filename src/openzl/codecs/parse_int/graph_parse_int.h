// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_PARSE_INT_H
#define ZSTRONG_TRANSFORMS_PARSE_INT_H

#include "openzl/zl_data.h" // ZS2_Type_*

#define PARSE_INT_GRAPH(id)                               \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_string),  \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric), \
    }

#endif
