// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_ZSTD_ENCODE_ZSTD_BINDING_H
#define ZSTRONG_TRANSFORMS_ZSTD_ENCODE_ZSTD_BINDING_H

#include "openzl/codecs/common/graph_pipe.h"
#include "openzl/codecs/entropy/graph_entropy.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

/// Encode with zstd.
/// Takes either serialized or fixed size inputs.
/// If the input is fixed size and large enough, zstd will
/// cut a block for each element, assuming that the stats
/// will be different between elements. E.g. transpose.
/// TODO: Accept global & local parameters.
ZL_Report EI_zstd(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

/* state management */
void* EIZSTD_createCCtx(void);
void EIZSTD_freeCCtx(void* state);

#define EI_ZSTD(id)                                  \
    {                                                \
        .gd                    = PIPE_GRAPH(id),     \
        .transform_f           = EI_zstd,            \
        .name                  = "!zl.private.zstd", \
        .trStateMgr.stateAlloc = EIZSTD_createCCtx,  \
        .trStateMgr.stateFree  = EIZSTD_freeCCtx,    \
    }

#define EI_ZSTD_FIXED(id)                                             \
    {                                                                 \
        .gd                    = FIXED_ENTROPY_GRAPH(id),             \
        .transform_f           = EI_zstd,                             \
        .name                  = "!zl.private.zstd_fixed_deprecated", \
        .trStateMgr.stateAlloc = EIZSTD_createCCtx,                   \
        .trStateMgr.stateFree  = EIZSTD_freeCCtx,                     \
    }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_ZSTD_ENCODE_ZSTD_BINDING_H
