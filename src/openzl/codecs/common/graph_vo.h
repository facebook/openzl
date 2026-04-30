// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_COMMON_GRAPH_VO_H
#define ZSTRONG_TRANSFORMS_COMMON_GRAPH_VO_H

#include "openzl/zl_data.h" // st_*

/* contains graph definition for common vo transforms,
 * used by both encode and decoder sides */

#define GRAPH_VO_SERIAL(id)                              \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial), \
        .soTypes    = NULL,                              \
        0,                                               \
        ZL_STREAMTYPELIST(ZL_Type_serial),               \
    }

#define GRAPH_VO_STRUCT(id)                              \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_struct), \
        .soTypes    = NULL,                              \
        0,                                               \
        ZL_STREAMTYPELIST(ZL_Type_struct),               \
    }

#define GRAPH_VO_NUM(id)                                  \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric), \
        .soTypes    = NULL,                               \
        0,                                                \
        ZL_STREAMTYPELIST(ZL_Type_numeric),               \
    }

#endif
