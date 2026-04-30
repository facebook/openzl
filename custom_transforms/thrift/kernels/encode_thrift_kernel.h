// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CUSTOM_TRANSFORMS_THRIFT_KERNELS_ENCODE_THRIFT_KERNEL_H
#define ZSTRONG_CUSTOM_TRANSFORMS_THRIFT_KERNELS_ENCODE_THRIFT_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * All of these functions require knowing the containers size ahead of time.
 * This is reasonable, because in order to know the type of the container, you
 * have to parse the header, which contains the size.
 *
 * When the extracted stream has the same cardinality of the container, we take
 * a pointer to that output array.
 *
 * When the extracted streams cardinality could be much larger than the
 * container, e.g. a map<i32, array<i64>>, we use a
 * `ZS2_ThriftKernel_DynamicOutput{32,64}` to write the output. This is a vtable
 * that allows us to stream output. See "thrift_kernel_utils.h" for an
 * implementation wrapping std::vector<>.
 *
 * I've chosen the interface for the dynamic output to provide a minimal
 * interface that could be served by many different implementations, like
 * std::vector<> or folly::IOBuf, or streaming. It is also maximally efficient,
 * assuming that the dynamic output returns a large enough output that the
 * refilling cost is negligable.
 *
 * If these functions succeed, then we guarantee that we'll round trip
 * successfully, and produce exactly the original bytes. This means that we'll
 * reject places where the Thrift spec has ambuguity. We also cap inner
 * container lengths at 2^32-1, so extremely large inner containers will be
 * rejected.
 *
 * These functions are resilient to malformed thrift.
 */

typedef struct {
    uint32_t* ptr;
    uint32_t* end;
} ZS2_ThriftKernel_Slice32;

typedef struct {
    uint64_t* ptr;
    uint64_t* end;
} ZS2_ThriftKernel_Slice64;

typedef struct {
    void* opaque;
    /// Commit the current slice, and returns a new slice.
    /// The first size_t is the current element we're processing.
    /// The second size_t is the total number of elements we need to process.
    /// This may be used as a hint of how much space to allocate.
    /// WARNING: This invalides previously returned slices.
    ZS2_ThriftKernel_Slice32 (*next)(void*, size_t, size_t);
    /// Commit values up to the pointer in the final slice.
    /// WARNING: This invalides all slices.
    void (*finish)(void*, uint32_t*);
} ZS2_ThriftKernel_DynamicOutput32;

typedef struct {
    void* opaque;
    /// Commit the current slice, and returns a new slice.
    /// The first size_t is the current element we're processing.
    /// The second size_t is the total number of elements we need to process.
    /// This may be used as a hint of how much space to allocate.
    /// WARNING: This invalides previously returned slices.
    ZS2_ThriftKernel_Slice64 (*next)(void*, size_t, size_t);
    /// Commit values up to the pointer in the final slice.
    /// WARNING: This invalides all slices.
    void (*finish)(void*, uint64_t*);
} ZS2_ThriftKernel_DynamicOutput64;

/// @returns the number of bytes consumed from the source.
ZL_Report ZS2_ThriftKernel_deserializeMapI32Float(
        uint32_t* keys,
        uint32_t* floats,
        void const* src,
        size_t srcSize,
        size_t mapSize);

/// @returns the number of bytes consumed from the source.
ZL_Report ZS2_ThriftKernel_deserializeMapI32ArrayFloat(
        uint32_t* keys,
        uint32_t* lengths,
        ZS2_ThriftKernel_DynamicOutput32 innerValues,
        void const* src,
        size_t srcSize,
        size_t mapSize);

/// @returns the number of bytes consumed from the source.
ZL_Report ZS2_ThriftKernel_deserializeMapI32ArrayI64(
        uint32_t* keys,
        uint32_t* lengths,
        ZS2_ThriftKernel_DynamicOutput64 innerValues,
        void const* src,
        size_t srcSize,
        size_t mapSize);

/// @returns the number of bytes consumed from the source.
ZL_Report ZS2_ThriftKernel_deserializeMapI32ArrayArrayI64(
        uint32_t* keys,
        uint32_t* lengths,
        ZS2_ThriftKernel_DynamicOutput32 innerLengths,
        ZS2_ThriftKernel_DynamicOutput64 innerInnerValues,
        void const* src,
        size_t srcSize,
        size_t mapSize);

/// @returns the number of bytes consumed from the source.
ZL_Report ZS2_ThriftKernel_deserializeMapI32MapI64Float(
        uint32_t* keys,
        uint32_t* lengths,
        ZS2_ThriftKernel_DynamicOutput64 innerKeys,
        ZS2_ThriftKernel_DynamicOutput32 innerValues,
        void const* src,
        size_t srcSize,
        size_t mapSize);

/// @returns the number of bytes consumed from the source.
ZL_Report ZS2_ThriftKernel_deserializeArrayI64(
        uint64_t* values,
        void const* src,
        size_t srcSize,
        size_t arraySize);

/// @returns the number of bytes consumed from the source.
ZL_Report ZS2_ThriftKernel_deserializeArrayI32(
        uint32_t* values,
        void const* src,
        size_t srcSize,
        size_t arraySize);

/// @returns the number of bytes consumed from the source.
ZL_Report ZS2_ThriftKernel_deserializeArrayFloat(
        uint32_t* values,
        void const* src,
        size_t srcSize,
        size_t arraySize);

/// @returns The size of the map starting at src.
ZL_Report ZS2_ThriftKernel_getMapSize(void const* src, size_t srcSize);

/// @returns The size of the array starting at src.
ZL_Report ZS2_ThriftKernel_getArraySize(void const* src, size_t srcSize);

#ifdef __cplusplus
}
#endif

#endif
