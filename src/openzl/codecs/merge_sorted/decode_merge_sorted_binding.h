// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_MERGE_SORTED_DECODE_MERGE_SORTED_BINDING_H
#define ZSTRONG_TRANSFORMS_MERGE_SORTED_DECODE_MERGE_SORTED_BINDING_H

#include "openzl/codecs/merge_sorted/graph_merge_sorted.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_Report DI_mergeSorted(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_MERGE_SORTED(id) \
    { .transform_f = DI_mergeSorted, .name = "merge sorted" }

#endif
