// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_DISPATCH_STRING_GRAPH_DISPATCH_STRING_H
#define ZSTRONG_TRANSFORMS_DISPATCH_STRING_GRAPH_DISPATCH_STRING_H

#include "openzl/zl_data.h" // st_*

#define GRAPH_DISPATCH_STRING(id)                         \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_string),  \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric), \
        .voTypes    = ZL_STREAMTYPELIST(ZL_Type_string),  \
    }

#endif
