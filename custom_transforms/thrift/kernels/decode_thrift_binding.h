// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/zl_dtransform.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Input: Thrift Compact map<i32, float>
/// Output 1: numeric i32 keys
/// Output 2: numeric floats
ZL_Report ZL_ThriftKernel_registerDTransformMapI32Float(
        ZL_DCtx* dctx,
        ZL_IDType transformID);

/// Input: Thrift Compact map<i32, list<float>>
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 lengths
/// Output 3: numeric floats
ZL_Report ZL_ThriftKernel_registerDTransformMapI32ArrayFloat(
        ZL_DCtx* dctx,
        ZL_IDType transformID);

/// Input: Thrift Compact map<i32, list<i64>>
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 lengths
/// Output 3: numeric i64
ZL_Report ZL_ThriftKernel_registerDTransformMapI32ArrayI64(
        ZL_DCtx* dctx,
        ZL_IDType transformID);

/// Input: Thrift Compact map<i32, list<list<i64>>>
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 outter list lengths
/// Output 3: numeric u32 inner list lengths
/// Output 4: numeric i64
ZL_Report ZL_ThriftKernel_registerDTransformMapI32ArrayArrayI64(
        ZL_DCtx* dctx,
        ZL_IDType transformID);

/// Input: Thrift Compact map<i32, map<i64, float>>
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 lengths
/// Output 3: numeric i64 keys
/// Output 4: numeric float values
ZL_Report ZL_ThriftKernel_registerDTransformMapI32MapI64Float(
        ZL_DCtx* dctx,
        ZL_IDType transformID);

/// Input: Thrift Compact list<i64>
/// Output 1: numeric i64
ZL_Report ZL_ThriftKernel_registerDTransformArrayI64(
        ZL_DCtx* dctx,
        ZL_IDType transformID);

/// Input: Thrift Compact list<i32>
/// Output 1: numeric i32
ZL_Report ZL_ThriftKernel_registerDTransformArrayI32(
        ZL_DCtx* dctx,
        ZL_IDType transformID);

/// Input: Thrift Compact list<float>
/// Output 1: numeric float
ZL_Report ZL_ThriftKernel_registerDTransformArrayFloat(
        ZL_DCtx* dctx,
        ZL_IDType transformID);

#ifdef __cplusplus
}
#endif
