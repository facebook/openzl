// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_MERGE_SORTED_GRAPH_MERGE_SORTED_H
#define ZSTRONG_TRANSFORMS_MERGE_SORTED_GRAPH_MERGE_SORTED_H

#include "openzl/zl_data.h"

/// Graph definition for the merge sorted transforms
/// used by both the encoder and decoder side.

#define MERGE_SORTED_GRAPH(id)                                             \
    {                                                                      \
        .CTid       = id,                                                  \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric),                  \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_numeric), \
    }

#endif
