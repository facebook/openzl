// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_DISPATCH_STRING_ENCODE_DISPATCH_STRING_BINDING_H
#define ZSTRONG_TRANSFORMS_DISPATCH_STRING_ENCODE_DISPATCH_STRING_BINDING_H

#include "openzl/codecs/dispatch_string/graph_dispatch_string.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

/**
 * Splits string input into N string streams based on an array (later,
 * generating function) which is transmitted through local parameter with (TODO)
 * paramID = ZL_DISPATCH_STRING_PARSINGF_PID.
 */
ZL_Report
EI_dispatch_string(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_DISPATCH_STRING(id)                    \
    {                                             \
        .gd          = GRAPH_DISPATCH_STRING(id), \
        .transform_f = EI_dispatch_string,        \
        .name        = "!zl.dispatch_string",     \
    }

ZL_END_C_DECLS

#endif
