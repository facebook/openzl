// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_ENCODE_PARSE_INT_BINDING_H
#define ZSTRONG_ENCODE_PARSE_INT_BINDING_H

#include "openzl/codecs/parse_int/graph_parse_int.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

#define ZL_PARSE_INT_PREPARSED_PARAMS 350

ZL_Report EI_parseInt(ZL_Encoder* encoder, const ZL_Input* ins[], size_t nbIns);

#define EI_PARSE_INT(id)                  \
    { .gd          = PARSE_INT_GRAPH(id), \
      .transform_f = EI_parseInt,         \
      .name        = "!zl.parse_int" }

ZL_Report
parseIntSafeFnGraph(ZL_Graph* graph, ZL_Edge* edges[], size_t nbEdges);

#define MIGRAPH_TRY_PARSE_INT                            \
    {                                                    \
        .name           = "!zl.try_parse_int",           \
        .graph_f        = parseIntSafeFnGraph,           \
        .inputTypeMasks = (ZL_Type[]){ ZL_Type_string }, \
        .nbInputs       = 1,                             \
    }

ZL_END_C_DECLS

#endif
