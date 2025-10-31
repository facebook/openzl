// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_ENTROPY_GRAPH_ENTROPY_H
#define ZSTRONG_TRANSFORMS_ENTROPY_GRAPH_ENTROPY_H

/**
 * Contains graph definitions for entropy transforms
 * used by both encode and decoder sides
 */

#include "openzl/zl_data.h" // st_*

#define ENTROPY_V2_GRAPH(id, inType)                                      \
    {                                                                     \
        .CTid       = id,                                                 \
        .inputTypes = ZL_STREAMTYPELIST(inType),                          \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_serial), \
    }

#define FSE_V2_GRAPH(id) ENTROPY_V2_GRAPH(id, ZL_Type_serial)
#define HUFFMAN_V2_GRAPH(id) ENTROPY_V2_GRAPH(id, ZL_Type_serial)
#define HUFFMAN_STRUCT_V2_GRAPH(id) ENTROPY_V2_GRAPH(id, ZL_Type_struct)

#define FSE_NCOUNT_GRAPH(id)                              \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial),  \
    }

#define FIXED_ENTROPY_GRAPH(id)                          \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_struct), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial), \
    }

#define SERIALIZED_ENTROPY_GRAPH(id)                     \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial), \
    }

#endif
