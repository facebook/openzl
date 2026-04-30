// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_CODECS_LZ4_ENCODE_LZ4_BINDING_H
#define ZSTRONG_CODECS_LZ4_ENCODE_LZ4_BINDING_H

#include "openzl/codecs/common/graph_pipe.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/// Encode with lz4.
/// Takes either serialized or fixed size inputs.
ZL_Report EI_lz4(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

// /* state management */

#define EI_LZ4(id)                        \
    {                                     \
        .gd          = PIPE_GRAPH(id),    \
        .transform_f = EI_lz4,            \
        .name        = "!zl.private.lz4", \
    }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_LZ4_ENCODE_LZ4_BINDING_H
