// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_CONCAT_DECODE_CONCAT_BINDING_H
#define ZSTRONG_TRANSFORMS_CONCAT_DECODE_CONCAT_BINDING_H

#include "openzl/codecs/concat/graph_concat.h" // CONCAT_SERIAL_GRAPH, CONCAT_NUM_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

ZL_Report DI_concat(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs);

#define DI_CONCAT_SERIAL(id) \
    { .transform_f = DI_concat, .name = "concat_serial_decoder" }

#define DI_CONCAT_NUM(id) \
    { .transform_f = DI_concat, .name = "concat_num_decoder" }

#define DI_CONCAT_STRUCT(id) \
    { .transform_f = DI_concat, .name = "concat_struct_decoder" }

#define DI_CONCAT_STRING(id) \
    { .transform_f = DI_concat, .name = "concat_string_decoder" }

ZL_END_C_DECLS

#endif
