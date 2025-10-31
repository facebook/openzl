// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_COMMON_GRAPH_PIPE_H
#define ZSTRONG_TRANSFORMS_COMMON_GRAPH_PIPE_H

/* contains graph definitions for pipe transform equivalence,
 * used by both encode and decoder sides */

#include "openzl/zl_data.h" // st_*

#define PIPE_GRAPH(id)                                   \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial), \
    }

#define NUMPIPE_GRAPH(id)                                 \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric), \
    }

#endif
