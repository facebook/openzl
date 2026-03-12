// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_PARTITION_ENCODE_PARTITION_BINDING_H
#define OPENZL_CODECS_PARTITION_ENCODE_PARTITION_BINDING_H

#include "openzl/codecs/partition/common_partition.h"
#include "openzl/codecs/partition/graph_partition.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

/// Encoder binding for the partition transform.
/// Reads partition parameters from local params, then encodes.
ZL_Report EI_partition(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

/// Encoder binding with explicit partition parameters and preset ID.
ZL_Report EI_partitionWithParams(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns,
        const ZL_PartitionParams* params,
        ZL_PartitionParamsPreset preset);

/// Parse partition parameters from a node's local params.
/// @returns The preset ID, or ZL_PartitionParamsPreset_custom for custom
/// params.
ZL_Report ZL_PartitionParams_fromLocalParams(
        ZL_PartitionParams* params,
        const ZL_LocalParams* localParams);

/// Macro to register the partition encoder transform.
#define EI_PARTITION(id)                  \
    { .gd          = PARTITION_GRAPH(id), \
      .transform_f = EI_partition,        \
      .name        = "!zl.partition" }

ZL_END_C_DECLS

#endif
