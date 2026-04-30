// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_CONCAT_GRAPH_H
#define ZSTRONG_TRANSFORMS_CONCAT_GRAPH_H

#include "openzl/zl_data.h" // st_*

#define CONCAT_SERIAL_GRAPH(id)                                        \
    {                                                                  \
        .CTid                = id,                                     \
        .inputTypes          = ZL_STREAMTYPELIST(ZL_Type_serial),      \
        .lastInputIsVariable = 1,                                      \
        .soTypes = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_serial), \
    }

#define CONCAT_NUM_GRAPH(id)                                            \
    {                                                                   \
        .CTid                = id,                                      \
        .inputTypes          = ZL_STREAMTYPELIST(ZL_Type_numeric),      \
        .lastInputIsVariable = 1,                                       \
        .soTypes = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_numeric), \
    }

#define CONCAT_STRUCT_GRAPH(id)                                        \
    {                                                                  \
        .CTid                = id,                                     \
        .inputTypes          = ZL_STREAMTYPELIST(ZL_Type_struct),      \
        .lastInputIsVariable = 1,                                      \
        .soTypes = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_struct), \
    }

#define CONCAT_STRING_GRAPH(id)                                        \
    {                                                                  \
        .CTid                = id,                                     \
        .inputTypes          = ZL_STREAMTYPELIST(ZL_Type_string),      \
        .lastInputIsVariable = 1,                                      \
        .soTypes = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_string), \
    }

#endif
