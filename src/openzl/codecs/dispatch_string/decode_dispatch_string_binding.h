// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_DISPATCH_STRING_DECODE_DISPATCH_STRING_BINDING_H
#define ZSTRONG_TRANSFORMS_DISPATCH_STRING_DECODE_DISPATCH_STRING_BINDING_H

#include "openzl/codecs/dispatch_string/graph_dispatch_string.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

/**
 * Joins N input string streams and 1 input numeric stream into 1 output string
 * stream. Specifically, strings from the N string streams are interleaved based
 * on the indices specified in the numeric stream.
 */
ZL_Report DI_dispatch_string(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs);

#define DI_DISPATCH_STRING(id)                   \
    {                                            \
        .transform_f = DI_dispatch_string,       \
        .name        = "dispatch_string decode", \
    }

ZL_END_C_DECLS

#endif
