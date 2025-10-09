// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/zl_ctransform.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Input: Zero or more thrift compact map<i32, float>
/// Output 0: numeric u32 size of each map
/// Output 1: numeric i32 keys
/// Output 2: numeric floats
ZL_NodeID ZL_ThriftKernel_registerCTransformMapI32Float(
        ZL_Compressor* cgraph,
        ZL_IDType transformID);

/// Input: Zero or more thrift compact map<i32, list<float>>
/// Output 0: numeric u32 size of each map
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 lengths
/// Output 3: numeric floats
ZL_NodeID ZL_ThriftKernel_registerCTransformMapI32ArrayFloat(
        ZL_Compressor* cgraph,
        ZL_IDType transformID);

/// Input: Zero or more thrift compact map<i32, list<i64>>
/// Output 0: numeric u32 size of each map
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 lengths
/// Output 3: numeric i64
ZL_NodeID ZL_ThriftKernel_registerCTransformMapI32ArrayI64(
        ZL_Compressor* cgraph,
        ZL_IDType transformID);

/// Input: Zero or more thrift compact map<i32, list<list<i64>>>
/// Output 0: numeric u32 size of each map
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 outter list lengths
/// Output 3: numeric u32 inner list lengths
/// Output 4: numeric i64
ZL_NodeID ZL_ThriftKernel_registerCTransformMapI32ArrayArrayI64(
        ZL_Compressor* cgraph,
        ZL_IDType transformID);

/// Input: Zero or more thrift compact map<i32, map<i64, float>>
/// Output 0: numeric u32 size of each map
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 lengths
/// Output 3: numeric i64 keys
/// Output 4: numeric float values
ZL_NodeID ZL_ThriftKernel_registerCTransformMapI32MapI64Float(
        ZL_Compressor* cgraph,
        ZL_IDType transformID);

/// Input: Zero or more thrift compact list<i64>
/// Output 0: numeric u32 size of each list
/// Output 1: numeric i64
ZL_NodeID ZL_ThriftKernel_registerCTransformArrayI64(
        ZL_Compressor* cgraph,
        ZL_IDType transformID);

/// Input: Zero or more thrift compact list<i32>
/// Output 0: numeric u32 size of each list
/// Output 1: numeric i32
ZL_NodeID ZL_ThriftKernel_registerCTransformArrayI32(
        ZL_Compressor* cgraph,
        ZL_IDType transformID);

/// Input: Zero or more thrift compact list<float>
/// Output 0: numeric u32 size of each list
/// Output 1: numeric float
ZL_NodeID ZL_ThriftKernel_registerCTransformArrayFloat(
        ZL_Compressor* cgraph,
        ZL_IDType transformID);

#ifdef __cplusplus
}
#endif
