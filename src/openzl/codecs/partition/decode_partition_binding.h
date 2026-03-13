// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_PARTITION_DECODE_PARTITION_BINDING_H
#define OPENZL_CODECS_PARTITION_DECODE_PARTITION_BINDING_H

#include "openzl/codecs/partition/graph_partition.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

/// Decoder binding for the partition transform.
/// Reads the codec header, then decodes bucket IDs + extra bits back into
/// the original values.
ZL_Report DI_partition(ZL_Decoder* dictx, const ZL_Input* ins[]);

/// Macro to register the partition decoder transform.
#define DI_PARTITION(id) \
    { .transform_f = DI_partition, .name = "!zl.partition" }

ZL_END_C_DECLS

#endif
