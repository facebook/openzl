// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_DISPATCHN_BYTAG_GRAPH_DISPATCHN_BYTAG_H
#define ZSTRONG_TRANSFORMS_DISPATCHN_BYTAG_GRAPH_DISPATCHN_BYTAG_H

/// Graph definition for the dispatchN_byTag transform
/// used by both the encoder and decoder sides.

typedef enum {
    dnbt_tags     = 0,
    dnbt_segSizes = 1,
    dnbt_segments = 2
} DNBT_streamIDs_e;

#define GRAPH_DIPATCHNBYTAG(id)                                            \
    {                                                                      \
        .CTid       = id,                                                  \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial),                   \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_numeric), \
        ZL_STREAMTYPELIST(ZL_Type_serial),                                 \
    }

#endif // ZSTRONG_TRANSFORMS_DISPATCHN_BYTAG_GRAPH_DISPATCHN_BYTAG_H
