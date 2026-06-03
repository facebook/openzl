// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_SPARSE_NUM_ENCODE_SPARSE_NUM_BINDING_H
#define OPENZL_CODECS_SPARSE_NUM_ENCODE_SPARSE_NUM_BINDING_H

#include "openzl/codecs/sparse_num/graph_sparse_num.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

ZL_Report EI_sparse_num(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_SPARSE_NUM(id)                  \
    { .gd          = SPARSE_NUM_GRAPH(id), \
      .transform_f = EI_sparse_num,        \
      .name        = "!zl.sparse_num" }

ZL_END_C_DECLS

#endif
