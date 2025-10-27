// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITUNPACK_GRAPH_BITUNPACK_H
#define ZSTRONG_TRANSFORMS_BITUNPACK_GRAPH_BITUNPACK_H

/**
 * Contains graph definitions for bitunpack transforms
 * used by both encode and decoder sides
 */

#include "openzl/zl_data.h" // st_*

#define BITUNPACK_GRAPH(id)                               \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial),  \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric), \
    }

#endif
