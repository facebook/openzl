// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_FLATPACK_GRAPH_FLATPACK_H
#define ZSTRONG_TRANSFORMS_FLATPACK_GRAPH_FLATPACK_H

#include "openzl/zl_data.h"

/// Graph definition for the flabpack transform
/// used by both the encoder and decoder side.

#define FLATPACK_GRAPH(id)                                               \
    {                                                                    \
        .CTid       = id,                                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial),                 \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial, ZL_Type_serial), \
    }

#endif
