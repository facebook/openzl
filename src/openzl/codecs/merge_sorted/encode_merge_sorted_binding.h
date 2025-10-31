// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_MERGE_SORTED_ENCODE_MERGE_SORTED_BINDING_H
#define ZSTRONG_TRANSFORMS_MERGE_SORTED_ENCODE_MERGE_SORTED_BINDING_H

#include "openzl/codecs/merge_sorted/graph_merge_sorted.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

ZL_Report
EI_mergeSorted(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_MERGE_SORTED(id)                  \
    { .gd          = MERGE_SORTED_GRAPH(id), \
      .transform_f = EI_mergeSorted,         \
      .name        = "!zl.merge_sorted" }

ZL_END_C_DECLS

#endif
