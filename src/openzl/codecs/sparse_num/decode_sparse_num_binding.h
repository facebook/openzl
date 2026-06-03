// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_SPARSE_NUM_DECODE_SPARSE_NUM_BINDING_H
#define OPENZL_CODECS_SPARSE_NUM_DECODE_SPARSE_NUM_BINDING_H

#include "openzl/codecs/sparse_num/graph_sparse_num.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

ZL_Report DI_sparse_num(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_SPARSE_NUM(id) \
    { .transform_f = DI_sparse_num, .name = "!zl.sparse_num" }

ZL_END_C_DECLS

#endif
