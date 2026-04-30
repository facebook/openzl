// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_COMMON_GRAPH_CONSTANT_H
#define ZSTRONG_TRANSFORMS_COMMON_GRAPH_CONSTANT_H

/**
 * Contains graph definitions for constant transforms
 * used by both encoder and decoder sides
 */

#include "openzl/zl_data.h"

#define SERIALIZED_CONSTANT_GRAPH(id)                    \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial), \
    }

#define FIXED_SIZE_CONSTANT_GRAPH(id)                    \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_struct), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_struct), \
    }

#endif
