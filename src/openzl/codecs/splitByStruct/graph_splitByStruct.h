// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_SPLITBYSTRUCT_GRAPH_SPLITBYSTRUCT_H
#define ZSTRONG_TRANSFORMS_SPLITBYSTRUCT_GRAPH_SPLITBYSTRUCT_H

/// Graph definition for the splitByStruct transform
/// used by both the encoder and decoder sides.

#define GRAPH_SPLITBYSTRUCT_VO(id)                       \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial), \
        .soTypes    = NULL,                              \
        0,                                               \
        ZL_STREAMTYPELIST(ZL_Type_struct),               \
    }

#endif // ZSTRONG_TRANSFORMS_SPLITBYSTRUCT_GRAPH_SPLITBYSTRUCT_H
