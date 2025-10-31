// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_LZ_ENCODE_LZ_BINDING_H
#define ZSTRONG_TRANSFORMS_LZ_ENCODE_LZ_BINDING_H

#include "openzl/codecs/lz/graph_lz.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

ZL_Report EI_fieldLz(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

/**
 * Dynamic graph backing ZL_GRAPH_FIELD_LZ
 */
ZL_Report EI_fieldLzDynGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns);

/**
 * Dynamic graph backing the default literals graph for Field LZ
 */
ZL_Report
EI_fieldLzLiteralsDynGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns);

/**
 * Selector that handles each serialized stream after transpose in the
 * default literals graph for Field LZ.
 */
ZL_GraphID SI_fieldLzLiteralsChannelSelector(
        ZL_Selector const* selCtx,
        ZL_Input const* input,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

#define EI_FIELD_LZ(id)                    \
    {                                      \
        .gd          = FIELD_LZ_GRAPH(id), \
        .transform_f = EI_fieldLz,         \
        .name        = "!zl.field_lz",     \
    }

ZL_END_C_DECLS

#endif
