// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CONTRIB_CUSTOM_CODEC_YCOCG_GRAPH_H
#define CONTRIB_CUSTOM_CODEC_YCOCG_GRAPH_H

/**
 * Shared graph definition for YCoCg codec
 * used by both encode and decoder sides
 */

#include "openzl/zl_data.h" // ZL_Type_*

// Arbitrary value, presumed unique
#define YCOCG_GRAPH_ID 3194172

#define YCOCG_GRAPH                                                 \
    {                                                               \
        .CTid           = YCOCG_GRAPH_ID,                           \
        .inStreamType   = ZL_Type_serial,                           \
        .outStreamTypes = ZL_STREAMTYPELIST(                        \
                ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric), \
    }

#endif
