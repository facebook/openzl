// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_DEDUP_DECODE_DEDUP_BINDING_H
#define ZSTRONG_TRANSFORMS_DEDUP_DECODE_DEDUP_BINDING_H

#include "openzl/codecs/dedup/graph_dedup.h" // DEDUP_NUM_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

// dedup_num decoder: duplicates the numeric input into X regenerated streams
ZL_Report DI_dedup_num(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs);

#define DI_DEDUP_NUM(id) \
    { .transform_f = DI_dedup_num, .name = "dedup_num_decoder" }

ZL_END_C_DECLS

#endif
