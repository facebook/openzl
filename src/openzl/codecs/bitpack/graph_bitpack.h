// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITPACK_GRAPH_BITPACK_H
#define ZSTRONG_TRANSFORMS_BITPACK_GRAPH_BITPACK_H

/**
 * Contains graph definitions for bitpack transforms
 * used by both encode and decoder sides
 */

#include "openzl/zl_data.h" // st_*

#define INTEGER_BITPACK_GRAPH(id)                         \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial),  \
    }

#define SERIALIZED_BITPACK_GRAPH(id)                     \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial), \
    }

#endif
