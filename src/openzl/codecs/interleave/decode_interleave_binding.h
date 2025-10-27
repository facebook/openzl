// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_INTERLEAVE_DECODE_INTERLEAVE_BINDING_H
#define OPENZL_CODECS_INTERLEAVE_DECODE_INTERLEAVE_BINDING_H

#include "openzl/codecs/interleave/graph_interleave.h" // INTERLEAVE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

ZL_Report DI_interleave(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs);

#define DI_INTERLEAVE(id) \
    { .transform_f = DI_interleave, .name = "!zl.interleave/decode" }

ZL_END_C_DECLS

#endif
