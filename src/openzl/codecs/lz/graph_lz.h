// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_LZ_GRAPH_LZ_H
#define ZSTRONG_TRANSFORMS_LZ_GRAPH_LZ_H

/// Graph definition for the quantize transforms
/// used by both the encoder and decoder side.

/// Output streams are:
/// 1. Literal fields
/// 2. Tokens (2-byte field)
/// 3. Offsets
/// 4. Extra literal lengths
/// 5. Extra match lengths
#define FIELD_LZ_GRAPH(id)                               \
    {                                                    \
        .CTid       = id,                                \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_struct), \
        .soTypes    = ZL_STREAMTYPELIST(                 \
                ZL_Type_struct,                       \
                ZL_Type_struct,                       \
                ZL_Type_numeric,                      \
                ZL_Type_numeric,                      \
                ZL_Type_numeric),                     \
    }

#endif
