// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_RANGE_PACK_GRAPH_RAGE_PACK_H
#define ZSTRONG_TRANSFORMS_RANGE_PACK_GRAPH_RAGE_PACK_H

/**
 * Contains graph definitions for range transforms
 * used by both encode and decoder sides
 */

#include "openzl/zl_data.h" // st_*

#define RANGE_PACK_GRAPH(id)                              \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric), \
    }

#endif
