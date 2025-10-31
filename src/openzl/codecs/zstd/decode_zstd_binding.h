// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_ZSTD_DECODE_ZSTD_BINDING_H
#define ZSTRONG_TRANSFORMS_ZSTD_DECODE_ZSTD_BINDING_H

#include "openzl/codecs/common/graph_pipe.h"
#include "openzl/codecs/entropy/graph_entropy.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_Report DI_zstd(ZL_Decoder* dictx, const ZL_Input* ins[]);

/* state management */
void* DIZSTD_createDCtx(void);
void DIZSTD_freeDCtx(void* state);

#define DI_ZSTD(id)                                 \
    {                                               \
        .transform_f           = DI_zstd,           \
        .name                  = "zstd",            \
        .trStateMgr.stateAlloc = DIZSTD_createDCtx, \
        .trStateMgr.stateFree  = DIZSTD_freeDCtx,   \
    }

#define DI_ZSTD_FIXED(id)                                    \
    {                                                        \
        .transform_f           = DI_zstd,                    \
        .name                  = "zstd_for_fixedSizeFields", \
        .trStateMgr.stateAlloc = DIZSTD_createDCtx,          \
        .trStateMgr.stateFree  = DIZSTD_freeDCtx,            \
    }

#endif // ZSTRONG_TRANSFORMS_ZSTD_DECODE_ZSTD_BINDING_H
