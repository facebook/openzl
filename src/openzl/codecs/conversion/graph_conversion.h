// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_CONVERSION_GRAPH_CONVERSION_H
#define ZSTRONG_TRANSFORMS_CONVERSION_GRAPH_CONVERSION_H

#include "openzl/codecs/zl_conversion.h"
#include "openzl/zl_data.h" // ZS2_Type_*

/* contains graph definition for conversion operations,
 * used by both encoder and decoder sides */

#define CONVERT_TOKEN_NUM_GRAPH(id)                       \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_struct),  \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric), \
    }

#define CONVERT_NUM_TOKEN_GRAPH(id)                       \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_struct),  \
    }

#define CONVERT_SERIAL_NUM_GRAPH(id)                      \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial),  \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric), \
    }

#define CONVERT_NUM_SERIAL_GRAPH(id)                      \
    {                                                     \
        .CTid       = id,                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial),  \
    }

#define CONVERT_SERIAL_TOKEN_GRAPH(id)                   \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_struct), \
    }

#define CONVERT_TOKEN_SERIAL_GRAPH(id)                   \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_struct), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial), \
    }

#define CONVERT_SERIAL_STRING_GRAPH(id)                  \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_string), \
    }

#define SEPARATE_VSF_COMPONENTS_GRAPH(id)                                 \
    {                                                                     \
        .CTid       = id,                                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_string),                  \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial, ZL_Type_numeric), \
    }

#endif // ZSTRONG_TRANSFORMS_CONVERSION_GRAPH_CONVERSION_H
