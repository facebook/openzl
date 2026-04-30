// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_COMMON_GRAPH_TRANSPOSE_H
#define ZSTRONG_TRANSFORMS_COMMON_GRAPH_TRANSPOSE_H

#include "openzl/zl_data.h" // st_*

/* contains graph definition for transpose transform,
 * used by both encode and decoder sides */

#define TRANSPOSE_GRAPH(id)                              \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_struct), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_struct), \
    }

#define TRANSPOSE_GRAPH_SPLIT(id)                        \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_struct), \
        .voTypes    = ZL_STREAMTYPELIST(ZL_Type_serial), \
    }

#define TRANSPOSE_GRAPH_SPLIT2(id)                                       \
    {                                                                    \
        .CTid       = id,                                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_struct),                 \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial, ZL_Type_serial), \
    }

#define TRANSPOSE_GRAPH_SPLIT4(id)                       \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_struct), \
        .soTypes    = ZL_STREAMTYPELIST(                 \
                ZL_Type_serial,                       \
                ZL_Type_serial,                       \
                ZL_Type_serial,                       \
                ZL_Type_serial),                      \
    }

#define TRANSPOSE_GRAPH_SPLIT8(id)                       \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_struct), \
        .soTypes    = ZL_STREAMTYPELIST(                 \
                ZL_Type_serial,                       \
                ZL_Type_serial,                       \
                ZL_Type_serial,                       \
                ZL_Type_serial,                       \
                ZL_Type_serial,                       \
                ZL_Type_serial,                       \
                ZL_Type_serial,                       \
                ZL_Type_serial),                      \
    }

#endif
